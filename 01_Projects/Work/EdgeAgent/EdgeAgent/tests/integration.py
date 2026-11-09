#!/usr/bin/env python3
"""Linux/WSL: real MQTT, UDP/TCP sockets and a raw pseudo-terminal.

Only loopback listeners, temporary files and this test's own child processes are used.
Usage: python3 integration.py /absolute/path/to/edgeagent
"""
import json
import os
import queue
import select
import socket
import subprocess
import sys
import tempfile
import threading
import time
import tty
from pathlib import Path


def free_port(kind=socket.SOCK_STREAM):
    with socket.socket(socket.AF_INET, kind) as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def recv_exact(sock, length):
    data = b""
    while len(data) < length:
        block = sock.recv(length - len(data))
        if not block:
            raise EOFError("TCP connection closed")
        data += block
    return data


def read_exact(fd, length, timeout=3):
    data = b""
    deadline = time.monotonic() + timeout
    while len(data) < length:
        remaining = deadline - time.monotonic()
        if remaining <= 0 or not select.select([fd], [], [], remaining)[0]:
            raise AssertionError("serial bytes not received: " + repr(data))
        data += os.read(fd, length - len(data))
    return data


def run(binary):
    children, logs = [], []
    messages, history = queue.Queue(), []
    with tempfile.TemporaryDirectory(prefix="edgeagent-proxy-") as directory:
        folder = Path(directory)
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp, socket.socket() as server:
            master, slave = os.openpty()
            tty.setraw(slave)
            udp.bind(("127.0.0.1", 0))
            udp.settimeout(3)
            server.bind(("127.0.0.1", 0))
            server.listen()
            server.settimeout(5)

            def start(args, name, env=None):
                log = open(folder / (name + ".log"), "w")
                logs.append(log)
                process = subprocess.Popen(args, stdout=log, stderr=subprocess.STDOUT, env=env)
                children.append(process)
                return process

            def wait(predicate, timeout=5):
                deadline = time.monotonic() + timeout
                while time.monotonic() < deadline:
                    for topic, value in history:
                        if predicate(topic, value):
                            return value
                    try:
                        history.append(messages.get(timeout=.05))
                    except queue.Empty:
                        pass
                raise AssertionError("expected MQTT message absent; tail=" + repr(history[-8:]))

            try:
                broker_port = free_port()
                broker_file = folder / "mosquitto.conf"
                broker_file.write_text("listener %d 127.0.0.1\nallow_anonymous true\npersistence false\n" % broker_port)
                broker = start(["mosquitto", "-c", str(broker_file)], "broker")
                for _ in range(100):
                    try:
                        with socket.create_connection(("127.0.0.1", broker_port), timeout=.1):
                            break
                    except OSError:
                        time.sleep(.02)
                mqtt = ["-h", "127.0.0.1", "-p", str(broker_port)]
                # Hex output preserves binary bodies, NULs and newlines without extra Python dependencies.
                subscriber = subprocess.Popen(["mosquitto_sub"] + mqtt + ["-t", "edge/v1/#", "-q", "1", "-F", "%t %x"],
                                              stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
                children.append(subscriber)

                def collect():
                    for line in subscriber.stdout:
                        topic, _, text = line.rstrip("\n").partition(" ")
                        payload = bytes.fromhex(text)
                        try:
                            value = json.loads(payload)
                            if not isinstance(value, dict):
                                value = payload
                        except (ValueError, UnicodeDecodeError):
                            value = payload
                        messages.put((topic, value))

                reader = threading.Thread(target=collect, daemon=True)
                reader.start()

                def publish_topic(topic, payload, retained=False):
                    body = json.dumps(payload).encode() if isinstance(payload, dict) else payload
                    args = ["mosquitto_pub"] + mqtt + ["-q", "1", "-t", topic, "-s"]
                    if retained:
                        args.append("-r")
                    subprocess.run(args, input=body, check=True, timeout=5)

                def publish(device, payload, retained=False):
                    publish_topic("edge/v1/devices/" + device + "/commands", payload, retained)

                def result(message, stage="sent", code="ok"):
                    return wait(lambda topic, value: topic.endswith("/command-results") and isinstance(value, dict)
                                and value.get("id", value.get("msg_id")) == message
                                and value.get("status", value.get("stage")) == stage
                                and value.get("error", {}).get("code", value.get("code", "ok")) == code)

                # Explicit subscriber readiness avoids losing short-lived startup results.
                for _ in range(20):
                    publish_topic("edge/v1/test/ready", b"ready")
                    try:
                        wait(lambda topic, value: topic == "edge/v1/test/ready" and value == b"ready", .15)
                        break
                    except AssertionError:
                        if subscriber.poll() is not None:
                            raise AssertionError(subscriber.stderr.read())
                else:
                    raise AssertionError("subscriber not ready")

                udp_port = free_port(socket.SOCK_DGRAM)
                tcp_port = free_port()
                unified_udp_port = free_port(socket.SOCK_DGRAM)
                unified_tcp_port = free_port()
                publish("udp_device", dict(msg_id="retained", target_ip="127.0.0.1",
                                           target_port=udp.getsockname()[1], data="DEAD"), True)
                legacy = dict(config_version=1, node_id="legacy_proxy", broker=dict(host="127.0.0.1", port=broker_port), channels=[
                    dict(channel_id="udp", device_id="udp_device", type="udp", bind_address="127.0.0.1", bind_port=udp_port,
                         allowed_peers=["127.0.0.1:%d" % udp.getsockname()[1]], qos=1, message_format="legacy"),
                    dict(channel_id="tcp", device_id="server_device", type="tcp", role="server", listen_address="127.0.0.1",
                         listen_port=tcp_port, frame=dict(kind="delimiter", delimiter="\n", max_bytes=4), message_format="legacy")])
                proxy = dict(config_version=2, node_id="proxy", broker=dict(host="127.0.0.1", port=broker_port), channels=[
                    dict(channel_id="client", type="tcp", role="client", remote_host="127.0.0.1", remote_port=server.getsockname()[1],
                         reconnect_initial_ms=100, reconnect_max_ms=200, frame=dict(kind="length_prefix", length_bytes=2)),
                    dict(channel_id="serial", type="serial", port_name=os.ttyname(slave), baud_rate=9600,
                         frame=dict(kind="idle_timeout", idle_ms=20)),
                    dict(channel_id="udp", type="udp", bind_address="127.0.0.1", bind_port=unified_udp_port,
                         allowed_peers=["127.0.0.1:%d" % udp.getsockname()[1]]),
                    dict(channel_id="server", type="tcp", role="server", listen_address="127.0.0.1", listen_port=unified_tcp_port,
                         frame=dict(kind="delimiter", delimiter="\n")),
                    dict(channel_id="disabled_link", enabled=False, type="tcp", role="server", listen_address="127.0.0.1",
                         listen_port=unified_tcp_port, frame=dict(kind="delimiter", delimiter="\n"))], devices=[
                    dict(device_id="client_device", channel_id="client", qos=1),
                    dict(device_id="serial_device", channel_id="serial", qos=1),
                    dict(device_id="json_udp", channel_id="udp", qos=1),
                    dict(device_id="json_server", channel_id="server", qos=1),
                    dict(device_id="disabled_device", channel_id="disabled_link", qos=1)])
                agents = []
                for name, config in (("legacy", legacy), ("proxy", proxy)):
                    config_file = folder / (name + ".json")
                    config_file.write_text(json.dumps(config))
                    env = dict(os.environ, EDGEAGENT_LOG_FILE=str(folder / (name + "-structured.log")))
                    agents.append(start([binary, "-c", str(config_file)], name, env))
                for node in ("legacy_proxy", "proxy"):
                    wait(lambda topic, value: topic == "edge/v1/gateways/" + node + "/status" and value == b"online")
                wait(lambda topic, value: isinstance(value, dict) and value.get("code") == "retained_command")
                publish("disabled_device", dict(id="disabled-probe", data="AA"))
                udp.settimeout(.25)
                try:
                    udp.recvfrom(100)
                    raise AssertionError("retained command reached UDP peer")
                except socket.timeout:
                    pass
                udp.settimeout(3)
                print("PASS retained command rejected before physical send", flush=True)

                udp.sendto(b"\x00\xff\x01", ("127.0.0.1", udp_port))
                value = wait(lambda topic, value: topic == "edge/v1/devices/udp_device/data"
                             and isinstance(value, dict) and value.get("data") == "00FF01")
                assert value["schema"] == "edge.udp.v1" and value["src_port"] == udp.getsockname()[1] and value["len"] == 3
                publish("udp_device", dict(msg_id="udp-down", target_ip="127.0.0.1", target_port=udp.getsockname()[1], data="A1B2"))
                assert udp.recvfrom(100)[0] == b"\xa1\xb2"
                result("udp-down")
                publish("udp_device", dict(msg_id="udp-rejected", target_ip="127.0.0.1", target_port=free_port(socket.SOCK_DGRAM), data="AA"))
                result("udp-rejected", "failed", "send_failed")
                print("PASS v1 UDP binary uplink, targeted downlink, metadata and allowlist", flush=True)

                with socket.create_connection(("127.0.0.1", tcp_port), timeout=3) as client:
                    client.sendall(b"AB")
                    time.sleep(.02)
                    client.sendall(b"CD\nEFGH\n")
                    for body in (b"ABCD", b"EFGH"):
                        wait(lambda topic, value: topic == "edge/v1/devices/server_device/data" and value == body)
                    publish("server_device", dict(msg_id="server-down", data="0048490A"))
                    assert recv_exact(client, 4) == b"\0HI\n"
                    result("server-down")
                    publish("server_device", dict(msg_id="bad-type", data=123))
                    result("bad-type", "failed", "invalid_request")
                    publish("server_device", dict(msg_id="bad-operation", operation="exchange", data="AA"))
                    result("bad-operation", "failed", "invalid_request")
                    publish("server_device", dict(msg_id="bad-target", connection_id="absent", data="AA"))
                    result("bad-target", "failed", "send_failed")
                    publish("server_device", b"RAW\x00\xff")
                    # Also proves none of the rejected messages leaked onto the physical connection.
                    assert recv_exact(client, 5) == b"RAW\x00\xff"
                print("PASS v1 TCP server partial/coalesced frames, HEX/raw sends and invalid-command rejection", flush=True)

                with server.accept()[0] as client:
                    client.settimeout(3)
                    client.sendall(b"\x00")
                    time.sleep(.02)
                    client.sendall(b"\x04A\x00\xffB\x00\x02CD")
                    for body in (b"A\x00\xffB", b"CD"):
                        value = wait(lambda topic, value: topic == "edge/v1/devices/client_device/data"
                                     and isinstance(value, dict) and value.get("data") == body.hex().upper())
                        assert value["transport"] == "tcp" and value["meta"]["connection"].startswith("client-")
                        assert value["meta"]["endpoint"] == dict(address="127.0.0.1", port=server.getsockname()[1])
                    publish("client_device", dict(id="client-down", transport="tcp", data="000358595A", meta=value["meta"]))
                    assert recv_exact(client, 5) == b"\0\x03XYZ"
                    result("client-down")
                    previous_client_connection = value["meta"]["connection"]
                print("PASS unified TCP client HEX envelope, endpoint/connection and addressed reply", flush=True)

                with server.accept()[0] as reconnected:
                    reconnected.settimeout(3)
                    reconnected.sendall(b"\0\x02RE")
                    value = wait(lambda topic, value: topic == "edge/v1/devices/client_device/data"
                                 and isinstance(value, dict) and value.get("data") == "5245")
                    assert value["meta"]["connection"] != previous_client_connection
                    publish("client_device", dict(id="old-client-connection", data="FF", meta=dict(connection=previous_client_connection)))
                    result("old-client-connection", "failed", "send_failed")
                    publish("client_device", dict(id="new-client-connection", data="AA", meta=value["meta"]))
                    assert recv_exact(reconnected, 1) == b"\xaa"
                    result("new-client-connection")
                print("PASS TCP client reconnect creates a new connection ID and rejects stale targets", flush=True)

                os.write(master, b"serial\x00\xff")
                value = wait(lambda topic, value: topic == "edge/v1/devices/serial_device/data"
                             and isinstance(value, dict) and value.get("data") == b"serial\x00\xff".hex().upper())
                assert value["transport"] == "serial" and value["meta"] == dict(endpoint=dict(address=os.ttyname(slave)))
                publish("serial_device", dict(id="serial-down", data="00FE4849", meta=value["meta"]))
                assert read_exact(master, 4) == b"\0\xfeHI"
                result("serial-down")
                publish("serial_device", dict(id="wrong-interface", data="AA", meta=dict(endpoint=dict(address="/dev/absent"))))
                result("wrong-interface", "failed", "invalid_request")
                publish("serial_device", b"RAW-SERIAL")
                assert read_exact(master, 10) == b"RAW-SERIAL"
                print("PASS unified serial HEX envelope, endpoint validation and legacy raw input", flush=True)

                udp.sendto(b"\x00\xab\xff", ("127.0.0.1", unified_udp_port))
                value = wait(lambda topic, value: topic == "edge/v1/devices/json_udp/data"
                             and isinstance(value, dict) and value.get("data") == "00ABFF")
                assert value["transport"] == "udp" and set(value) == {"id", "ts", "transport", "data", "meta"}
                assert value["meta"] == dict(endpoint=dict(address="127.0.0.1", port=udp.getsockname()[1]))
                reply = dict(value, id="json-udp-down", data="0102")
                publish("json_udp", reply)
                assert udp.recvfrom(100)[0] == b"\x01\x02"
                assert result("json-udp-down") == dict(id="json-udp-down", status="sent")
                for message, fields in (
                        ("bad-meta", dict(meta=None)),
                        ("bad-endpoint", dict(meta=dict(endpoint=dict(address="127.0.0.1", port=1.5)))),
                        ("wrong-transport", dict(transport="serial")),
                        ("wrong-connection", dict(meta=dict(endpoint=reply["meta"]["endpoint"], connection="client"))),
                        ("wrong-frame", dict(meta=dict(endpoint=reply["meta"]["endpoint"], frame=dict(id="0x123")))),
                        ("mixed-format", dict(msg_id="old"))):
                    publish("json_udp", dict(reply, id=message, **fields))
                    result(message, "failed", "invalid_request")
                # Legacy downlink aliases still work while receipts follow the configured output format.
                publish("json_udp", dict(msg_id="old-udp-input", target_ip="127.0.0.1", target_port=udp.getsockname()[1], data="ABCD"))
                assert udp.recvfrom(100)[0] == b"\xab\xcd"
                result("old-udp-input")
                print("PASS unified UDP endpoint roundtrip, metadata validation and legacy JSON input", flush=True)

                with socket.create_connection(("127.0.0.1", unified_tcp_port), timeout=3) as first, \
                        socket.create_connection(("127.0.0.1", unified_tcp_port), timeout=3) as second:
                    first.sendall(b"ONE\n")
                    second.sendall(b"TWO\n")
                    received = []
                    for text in ("ONE", "TWO"):
                        value = wait(lambda topic, value: topic == "edge/v1/devices/json_server/data"
                                     and isinstance(value, dict) and value.get("data") == text.encode().hex().upper())
                        received.append(value)
                    assert received[0]["meta"]["connection"] != received[1]["meta"]["connection"]
                    publish("json_server", dict(id="ambiguous", data="FF"))
                    result("ambiguous", "failed", "send_failed")
                    publish("json_server", dict(id="mismatch", data="FF", meta=dict(
                        connection=received[0]["meta"]["connection"], endpoint=received[1]["meta"]["endpoint"])))
                    result("mismatch", "failed", "send_failed")
                    publish("json_server", dict(id="first-only", data="0102", meta=received[0]["meta"]))
                    assert recv_exact(first, 2) == b"\x01\x02"
                    result("first-only")
                    publish("json_server", dict(id="second-only", data="0304", meta=dict(endpoint=received[1]["meta"]["endpoint"])))
                    assert recv_exact(second, 2) == b"\x03\x04"
                    result("second-only")
                    second.settimeout(.2)
                    try:
                        second.recv(1)
                        raise AssertionError("unexpected bytes on second TCP connection")
                    except socket.timeout:
                        pass
                print("PASS unified TCP server endpoint/connection routing and ambiguous/mismatched target rejection", flush=True)

                with socket.create_connection(("127.0.0.1", unified_tcp_port), timeout=3) as replacement:
                    replacement.sendall(b"NEW\n")
                    value = wait(lambda topic, value: topic == "edge/v1/devices/json_server/data"
                                 and isinstance(value, dict) and value.get("data") == "4E4557")
                    current = value["meta"]["connection"]
                    assert current not in [item["meta"]["connection"] for item in received]
                    publish("json_server", dict(id="stale-connection", data="FF", meta=dict(connection=received[0]["meta"]["connection"])))
                    result("stale-connection", "failed", "send_failed")
                    publish("json_server", dict(id="current-connection", data="AA", meta=dict(connection=current)))
                    assert recv_exact(replacement, 1) == b"\xaa"
                    result("current-connection")
                print("PASS disconnected TCP connection IDs cannot target replacement sessions", flush=True)

                assert broker.poll() is None and all(agent.poll() is None for agent in agents)
                assert "disabled_link" not in (folder / "proxy-structured.log").read_text()
                assert not any(topic.endswith("/command-results") and isinstance(value, dict)
                               and value.get("id") == "disabled-probe" for topic, value in history)
                print("PASS disabled channel neither binds its port nor subscribes to commands", flush=True)
                assert not any(isinstance(value, dict) and value.get("stage") in ("accepted", "completed") for _, value in history)
                print("PASS transparent MQTT integration", flush=True)
            except Exception:
                for path in folder.glob("*.log"):
                    print("DIAGNOSTIC", path.name, path.read_text(errors="replace")[-3000:], file=sys.stderr)
                raise
            finally:
                for process in reversed(children):
                    if process.poll() is None:
                        process.terminate()
                    try:
                        process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
                for log in logs:
                    log.close()
                os.close(master)
                os.close(slave)


if __name__ == "__main__":
    run(str(Path(sys.argv[1]).resolve()))
