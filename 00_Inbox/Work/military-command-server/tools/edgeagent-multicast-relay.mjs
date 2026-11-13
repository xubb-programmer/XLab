import { execFileSync } from "node:child_process";
import { createSocket } from "node:dgram";
import { isIPv4 } from "node:net";
import { networkInterfaces } from "node:os";

const group = process.env.DEVICE_SIM_GROUP || "239.255.0.1";
const sourceIp = process.env.DEVICE_SIM_SOURCE_IP || "192.168.10.130";
const multicastPort = Number(process.env.DEVICE_SIM_UDP_PORT || 5678);
const agentPort = Number(process.env.EDGEAGENT_UDP_PORT || 18082);
const distro = process.env.EDGEAGENT_WSL_DISTRO || "Ubuntu-20.04";

function ipv4Number(address) {
  return address.split(".").reduce((value, part) => ((value << 8) | Number(part)) >>> 0, 0);
}

function findInterfaceIp(address) {
  if (process.env.DEVICE_SIM_INTERFACE) return process.env.DEVICE_SIM_INTERFACE;
  const addressNumber = ipv4Number(address);
  for (const entries of Object.values(networkInterfaces())) {
    for (const entry of entries ?? []) {
      if (entry.family !== "IPv4" || !isIPv4(entry.netmask)) continue;
      const mask = ipv4Number(entry.netmask);
      if ((ipv4Number(entry.address) & mask) === (addressNumber & mask)) return entry.address;
    }
  }
  throw new Error(`找不到与设备源 IP ${address} 同网段的网卡，请设置 DEVICE_SIM_INTERFACE`);
}

function findEdgeAgentHost() {
  if (process.env.EDGEAGENT_UDP_HOST) return process.env.EDGEAGENT_UDP_HOST;
  const output = execFileSync("wsl.exe", ["-d", distro, "--exec", "hostname", "-I"], {
    encoding: "utf8", windowsHide: true
  });
  const address = output.trim().split(/\s+/).find(isIPv4);
  if (!address) throw new Error(`无法读取 WSL ${distro} 的 IPv4 地址`);
  return address;
}

if (!isIPv4(group) || Number(group.split(".")[0]) < 224 || Number(group.split(".")[0]) > 239
    || !isIPv4(sourceIp) || !Number.isInteger(multicastPort) || multicastPort < 1 || multicastPort > 65535
    || !Number.isInteger(agentPort) || agentPort < 1 || agentPort > 65535) {
  throw new Error("组播转发配置无效");
}

const interfaceIp = findInterfaceIp(sourceIp);
const agentHost = findEdgeAgentHost();
if (!isIPv4(interfaceIp) || !isIPv4(agentHost)) throw new Error("转发网卡或 EdgeAgent 地址无效");

const receiver = createSocket({ type: "udp4", reuseAddr: true });
const sender = createSocket("udp4");
let closing = false;

function close() {
  if (closing) return;
  closing = true;
  try { receiver.dropMembership(group, interfaceIp); } catch {}
  receiver.close();
  sender.close();
}

receiver.on("message", (message, peer) => {
  if (peer.address !== sourceIp) return;
  sender.send(message, agentPort, agentHost, error => {
    if (error) console.error(`[EdgeAgent组播转发] 转发失败: ${error.message}`);
  });
});
receiver.on("error", error => {
  console.error(`[EdgeAgent组播转发] 接收失败: ${error.message}`);
  close();
  process.exitCode = 1;
});
receiver.bind(multicastPort, "0.0.0.0", () => {
  try {
    receiver.addMembership(group, interfaceIp);
    console.log(`[EdgeAgent组播转发] ${group}:${multicastPort} (${interfaceIp}) -> ${agentHost}:${agentPort}; source=${sourceIp}`);
  } catch (error) {
    console.error(`[EdgeAgent组播转发] 加入组播失败: ${error.message}`);
    close();
    process.exitCode = 1;
  }
});

process.on("SIGINT", close);
process.on("SIGTERM", close);
