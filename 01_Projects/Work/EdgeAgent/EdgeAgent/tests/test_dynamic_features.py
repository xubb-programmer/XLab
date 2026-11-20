#!/usr/bin/env python3
"""Integration tests for EdgeAgent dynamic features:
- TCP Client Dynamic Connection Pool (routing by meta.endpoint to arbitrary hosts/ports)
- Dynamic reply routing back to MQTT
- Gateway network interfaces auto-discovery retained announcement
- UDP Wildcard allowlist (0.0.0.0:0 / *:*)
"""
import json
import os
import queue
import socket
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path


def free_port(kind=socket.SOCK_STREAM):
    with socket.socket(socket.AF_INET, kind) as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def run_test(binary):
    children, logs = [], []
    messages, history = queue.Queue(), []
    with tempfile.TemporaryDirectory(prefix="edgeagent-dyn-") as directory:
        folder = Path(directory)
        
        def start(args, name):
            log = open(folder / (name + ".log"), "w")
            logs.append(log)
            p = subprocess.Popen(args, stdout=log, stderr=subprocess.STDOUT)
            children.append(p)
            return p

        def wait_msg(predicate, timeout=6):
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                for topic, value in history:
                    if predicate(topic, value):
                        return value
                try:
                    history.append(messages.get(timeout=0.05))
                except queue.Empty:
                    pass
            raise AssertionError("Expected MQTT message not received; tail=" + repr(history[-10:]))

        try:
            # 1. 启动临时 Mosquitto Broker
            broker_port = free_port()
            broker_conf = folder / "mosquitto.conf"
            broker_conf.write_text(f"listener {broker_port} 127.0.0.1\nallow_anonymous true\npersistence false\n")
            start(["mosquitto", "-c", str(broker_conf)], "broker")
            
            for _ in range(100):
                try:
                    with socket.create_connection(("127.0.0.1", broker_port), timeout=0.1):
                        break
                except OSError:
                    time.sleep(0.02)
            
            # 启动 MQTT 订阅监听器
            mqtt_args = ["-h", "127.0.0.1", "-p", str(broker_port)]
            sub = subprocess.Popen(["mosquitto_sub"] + mqtt_args + ["-t", "edge/v1/#", "-q", "1", "-F", "%t %x"],
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            children.append(sub)

            def collect():
                for line in sub.stdout:
                    topic, _, hex_val = line.rstrip("\n").partition(" ")
                    try:
                        val = bytes.fromhex(hex_val).decode("utf-8", errors="replace")
                    except Exception:
                        val = hex_val
                    messages.put((topic, val))
            
            threading.Thread(target=collect, daemon=True).start()

            # 2. 准备两个独立的 TCP 模拟目标终端 (Server A, Server B)
            srv_a = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            srv_a.bind(("127.0.0.1", 0))
            srv_a.listen(5)
            port_a = srv_a.getsockname()[1]

            srv_b = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            srv_b.bind(("127.0.0.1", 0))
            srv_b.listen(5)
            port_b = srv_b.getsockname()[1]

            # 3. 准备 UDP 接收端与发送端
            udp_recv_port = free_port(socket.SOCK_DGRAM)

            # 4. 生成 EdgeAgent 配置文件 (使用 dynamic TCP 与 0.0.0.0:0 UDP)
            config_data = {
                "config_version": 2,
                "node_id": "test_dynamic_node",
                "broker": {
                    "host": "127.0.0.1",
                    "port": broker_port,
                    "protocol": "mqtt311",
                    "keepalive_s": 30,
                    "clean_session": True
                },
                "channels": [
                    {
                        "channel_id": "udp_wildcard",
                        "enabled": True,
                        "type": "udp",
                        "bind_address": "127.0.0.1",
                        "bind_port": udp_recv_port,
                        "allowed_peers": ["0.0.0.0:0"],
                        "max_datagram_bytes": 65507,
                        "multicast": {"enabled": False}
                    },
                    {
                        "channel_id": "tcp_dynamic_client",
                        "enabled": True,
                        "type": "tcp",
                        "role": "client",
                        "remote_host": "dynamic",
                        "remote_port": 5567,
                        "frame": {
                            "kind": "delimiter",
                            "delimiter": "\n"
                        }
                    }
                ],
                "devices": [
                    {
                        "device_id": "udp_device",
                        "channel_id": "udp_wildcard",
                        "qos": 0,
                        "message_format": "json"
                    },
                    {
                        "device_id": "tcp_device",
                        "channel_id": "tcp_dynamic_client",
                        "qos": 1,
                        "message_format": "json"
                    }
                ]
            }
            cfg_path = folder / "test_config.json"
            cfg_path.write_text(json.dumps(config_data, indent=2))

            # 5. 启动 EdgeAgent
            start([binary, "-c", str(cfg_path)], "edgeagent")

            # === 测试点 1: 网关物理网卡自发现发布 ===
            print("[TEST 1] Testing Gateway Network Interfaces Announcement...", flush=True)
            iface_val = wait_msg(lambda t, v: t == "edge/v1/gateways/test_dynamic_node/interfaces")
            iface_json = json.loads(iface_val)
            assert iface_json.get("gateway") == "test_dynamic_node", "Expected gateway test_dynamic_node"
            assert "interfaces" in iface_json, "Expected interfaces in json"
            print("  -> Gateway reported interfaces:", [f"{i['name']}:{i.get('address')}" for i in iface_json.get("interfaces", [])])
            print("  -> PASS Test 1: Gateway Interfaces Announcement OK", flush=True)

            # === 测试点 2: UDP 0.0.0.0:0 通配白名单接收 ===
            print("[TEST 2] Testing UDP Wildcard Allowlist (0.0.0.0:0)...", flush=True)
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp_sender:
                test_payload = json.dumps({"sensor": "temperature", "val": 36.5})
                udp_sender.sendto(test_payload.encode(), ("127.0.0.1", udp_recv_port))
            
            udp_val = wait_msg(lambda t, v: t == "edge/v1/devices/udp_device/data")
            udp_obj = json.loads(udp_val)
            # data 为 Hex 编码的载荷
            recv_bytes = bytes.fromhex(udp_obj["data"])
            assert json.loads(recv_bytes.decode()) == {"sensor": "temperature", "val": 36.5}, "UDP payload mismatch"
            assert udp_obj["meta"]["endpoint"]["address"] == "127.0.0.1", "Endpoint address mismatch"
            print("  -> Received UDP Frame via MQTT envelope:", udp_obj["meta"])
            print("  -> PASS Test 2: UDP Wildcard Allowlist OK", flush=True)

            # === 测试点 3: TCP 动态路由到 Server A ===
            print("[TEST 3] Testing TCP Dynamic Routing to Target A (127.0.0.1:%d)..." % port_a, flush=True)
            cmd_a = {
                "id": "cmd-001",
                "transport": "tcp",
                "data": "Hello Server A\n".encode().hex(),
                "meta": {
                    "endpoint": {
                        "address": "127.0.0.1",
                        "port": port_a
                    }
                }
            }
            subprocess.run(["mosquitto_pub"] + mqtt_args + ["-t", "edge/v1/devices/tcp_device/commands", "-m", json.dumps(cmd_a), "-q", "1"], check=True)
            
            srv_a.settimeout(3.0)
            client_conn_a, client_addr_a = srv_a.accept()
            print("  -> Server A accepted connection from EdgeAgent:", client_addr_a)
            data_a = client_conn_a.recv(1024)
            assert data_a == b"Hello Server A\n", f"Server A received mismatch: {data_a}"
            print("  -> Server A received payload successfully!")

            # Server A 发送回执给 EdgeAgent，验证回执转发至 MQTT data topic
            client_conn_a.sendall(b"ACK From Server A\n")
            def is_ack_a(t, v):
                if t != "edge/v1/devices/tcp_device/data": return False
                try:
                    return bytes.fromhex(json.loads(v)["data"]).decode() == "ACK From Server A"
                except Exception:
                    return False
            reply_val_a = wait_msg(is_ack_a)
            print("  -> EdgeAgent forwarded Server A's reply to MQTT:", reply_val_a.strip())
            print("  -> PASS Test 3: TCP Dynamic Routing to Target A OK", flush=True)

            # === 测试点 4: TCP 动态路由到 Server B (同一 Channel，不同目标 IP:Port) ===
            print("[TEST 4] Testing TCP Dynamic Routing to Target B (127.0.0.1:%d)..." % port_b, flush=True)
            cmd_b = {
                "id": "cmd-002",
                "transport": "tcp",
                "data": "Hello Server B\n".encode().hex(),
                "meta": {
                    "endpoint": {
                        "address": "127.0.0.1",
                        "port": port_b
                    }
                }
            }
            subprocess.run(["mosquitto_pub"] + mqtt_args + ["-t", "edge/v1/devices/tcp_device/commands", "-m", json.dumps(cmd_b), "-q", "1"], check=True)
            
            srv_b.settimeout(3.0)
            client_conn_b, client_addr_b = srv_b.accept()
            print("  -> Server B accepted connection from EdgeAgent:", client_addr_b)
            data_b = client_conn_b.recv(1024)
            assert data_b == b"Hello Server B\n", f"Server B received mismatch: {data_b}"
            print("  -> Server B received payload successfully!")

            # Server B 发送回执
            client_conn_b.sendall(b"ACK From Server B\n")
            def is_ack_b(t, v):
                if t != "edge/v1/devices/tcp_device/data": return False
                try:
                    return bytes.fromhex(json.loads(v)["data"]).decode() == "ACK From Server B"
                except Exception:
                    return False
            reply_val_b = wait_msg(is_ack_b)
            print("  -> EdgeAgent forwarded Server B's reply to MQTT:", reply_val_b.strip())
            print("  -> PASS Test 4: TCP Dynamic Routing to Target B OK", flush=True)

            # === 测试点 5: 连接池长连接复用验证 ===
            print("[TEST 5] Testing Connection Pool Reuse (Sending to Server A again)...", flush=True)
            cmd_a2 = {
                "id": "cmd-003",
                "transport": "tcp",
                "data": "Second Message to A\n".encode().hex(),
                "meta": {
                    "endpoint": {
                        "address": "127.0.0.1",
                        "port": port_a
                    }
                }
            }
            subprocess.run(["mosquitto_pub"] + mqtt_args + ["-t", "edge/v1/devices/tcp_device/commands", "-m", json.dumps(cmd_a2), "-q", "1"], check=True)
            data_a2 = client_conn_a.recv(1024)
            assert data_a2 == b"Second Message to A\n", f"Server A second recv mismatch: {data_a2}"
            print("  -> Server A received second message on EXISTING socket connection!")
            print("  -> PASS Test 5: Connection Pool Reuse OK", flush=True)

            print("\n==========================================")
            print("ALL DYNAMIC & ZERO-CONFIG TESTS PASSED!")
            print("==========================================")

        finally:
            for p in reversed(children):
                if p.poll() is None:
                    p.terminate()
                    try:
                        p.wait(timeout=1)
                    except subprocess.TimeoutExpired:
                        p.kill()
            for l in logs:
                l.close()


if __name__ == "__main__":
    binary_path = sys.argv[1] if len(sys.argv) > 1 else str(Path(__file__).resolve().parent.parent / "edgeagent")
    run_test(binary_path)
