import { createSocket } from "node:dgram";
import { existsSync } from "node:fs";
import { mkdir } from "node:fs/promises";
import { isIPv4 } from "node:net";
import { networkInterfaces } from "node:os";
import { resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { decodeReport, openRegistry } from "./device-simulation-registry.mjs";
import { serveVideo } from "./video.mjs";
import { mountedDeviceIds } from "./device-mounts.mjs";

const namespace = "/api/device-simulation";
const MAX_UDP_BYTES = 65_507;
const SITUATION_REPORT_PORT = 28081;

export function getLocalIPv4Address(targetPeerAddress) {
  const interfaces = networkInterfaces();
  const allNonInternal = [];
  for (const list of Object.values(interfaces)) {
    if (!list) continue;
    for (const info of list) {
      if (info.family === "IPv4" && !info.internal) {
        allNonInternal.push(info.address);
      }
    }
  }
  if (targetPeerAddress && isIPv4(targetPeerAddress)) {
    const targetParts = targetPeerAddress.split(".");
    const match24 = allNonInternal.find(ip => {
      const parts = ip.split(".");
      return parts[0] === targetParts[0] && parts[1] === targetParts[1] && parts[2] === targetParts[2];
    });
    if (match24) return match24;
    const match16 = allNonInternal.find(ip => {
      const parts = ip.split(".");
      return parts[0] === targetParts[0] && parts[1] === targetParts[1];
    });
    if (match16) return match16;
  }
  return allNonInternal[0] || "127.0.0.1";
}

function buildSituationReportCommand(reportHost, reportPort = 28080) {
  return Buffer.from(JSON.stringify({
    "定时上报": true,
    "上报间隔毫秒": 2000,
    "上报地址": reportHost || getLocalIPv4Address(),
    "上报端口": reportPort,
    "目标编号": null,
    "目标类型": null,
    "最大速度米每秒": null
  }), "utf8");
}

async function sendSituationReport(address, reportHost, reportPort = 28080) {
  const host = (typeof reportHost === "string" && isIPv4(reportHost))
    ? reportHost
    : getLocalIPv4Address(address);
  const socket = createSocket("udp4");
  const command = buildSituationReportCommand(host, reportPort);
  try {
    await new Promise((resolve, reject) => {
      socket.once("error", reject);
      socket.send(command, SITUATION_REPORT_PORT, address, error => {
        if (error) reject(error);
        else resolve();
      });
    });
  } finally {
    socket.close();
  }
}

function integerSetting(name, fallback, minimum, maximum) {
  const value = Number(process.env[name] ?? fallback);
  if (!Number.isInteger(value) || value < minimum || value > maximum) throw new Error(`${name} 配置无效`);
  return value;
}

function json(res, status, payload) {
  res.writeHead(status, { "Content-Type": "application/json; charset=utf-8", "Cache-Control": "no-store" });
  res.end(JSON.stringify(payload));
}

export async function openDeviceSimulation(rtspTimeoutOption, mqttConfig, scenario) {
  const sourceIps = (process.env.DEVICE_SIM_SOURCE_IPS || "192.168.10.130")
    .split(",").map(address => address.trim()).filter(Boolean);
  const source = sourceIps.join(", ");
  const relaySource = process.env.DEVICE_SIM_RELAY_IP || mqttConfig.deviceStatusRelayIp || "";
  const onlineMs = integerSetting("DEVICE_SIM_ONLINE_MS", 5000, 1000, 300000);
  const topic = process.env.DEVICE_STATUS_TOPIC || mqttConfig.deviceStatusTopic;
  const envStateDir = process.env.DEVICE_SIM_STATE_DIR;
  const defaultStateDir = fileURLToPath(new URL("../dist/device-status-simulation/", import.meta.url));
  const stateDir = envStateDir && !envStateDir.startsWith("/var/lib/") && !envStateDir.startsWith("/etc/")
    ? resolve(fileURLToPath(new URL("../", import.meta.url)), envStateDir)
    : defaultStateDir;
  await mkdir(stateDir, { recursive: true });
  const registry = openRegistry(resolve(stateDir, "devices.sqlite"), onlineMs);
  const portable = resolve(stateDir, "ffmpeg.exe");
  const ffmpegPath = process.env.DEVICE_SIM_FFMPEG_PATH || (process.platform === "win32" && existsSync(portable) ? portable : "ffmpeg");

  const valid = sourceIps.length > 0 && sourceIps.every(isIPv4)
    && (!relaySource || isIPv4(relaySource)) && Boolean(topic);
  const mqtt = mqttConfig.brokerUrl && valid ? await import("mqtt").catch(() => null) : null;
  let state = !mqttConfig.brokerUrl ? mqttConfig.state : !valid ? "invalid" : mqtt ? "connecting" : "dependency_missing";
  let receiveError = null;
  const client = mqtt?.connect(mqttConfig.brokerUrl, {
    protocolVersion: 4, clean: true, reconnectPeriod: 2000, connectTimeout: 10_000,
    resubscribe: false, queueQoSZero: false,
    username: mqttConfig.username || undefined, password: mqttConfig.password || undefined
  });
  client?.on("connect", () => {
    state = "connecting";
    client.subscribe(topic, { qos: 1 }, (error, granted) => {
      if (!client.connected) return;
      state = !error && granted?.some(entry => entry.topic === topic && entry.qos <= 1)
        ? "subscribed" : "subscription_failed";
      receiveError = state === "subscription_failed" ? "无法订阅EdgeAgent设备状态" : null;
    });
  });
  client?.on("message", (receivedTopic, message, packet) => {
    if (receivedTopic !== topic || packet.retain || message.length > MAX_UDP_BYTES * 2 + 512) return;
    let envelope;
    try {
      envelope = JSON.parse(message.toString("utf8"));
    } catch {
      return;
    }
    const hex = envelope?.data;
    const peerAddress = envelope?.meta?.endpoint?.address;
    if (envelope?.transport !== "udp" || typeof hex !== "string" || hex.length > MAX_UDP_BYTES * 2
        || hex.length % 2 !== 0 || !/^[0-9a-f]*$/i.test(hex)
        || (!sourceIps.includes(peerAddress) && peerAddress !== relaySource)) return;
    const reportSource = sourceIps.includes(peerAddress) ? peerAddress : sourceIps[0];
    const report = decodeReport(Buffer.from(hex, "hex"), reportSource);
    if (!report) return;
    try {
      registry.ingest(report);
      receiveError = null;
    } catch (error) {
      receiveError = "设备状态保存失败";
      console.error(receiveError, error);
    }
  });
  client?.on("error", error => {
    state = "connection_error";
    receiveError = "EdgeAgent设备状态连接失败";
    console.error(receiveError, error);
  });
  client?.on("offline", () => { state = "disconnected"; });
  client?.on("reconnect", () => { state = "connecting"; });
  if (client) console.log(`[Device Simulation] MQTT订阅 ${topic}，设备来源 ${source}，转发来源 ${relaySource}`);

  return {
    isFirepower: registry.isFirepower,
    standaloneIds: registry.standaloneIds,
    snapshot: registry.snapshot,
    removeSnapshot: registry.removeSnapshot,
    async serve(req, res, url) {
      const path = url.pathname.slice(namespace.length);
      if (req.method === "GET" && path === "/devices") {
        const current = await scenario.read();
        return json(res, 200, { state, source, relaySource, topic, onlineMs, receiveError,
          ...registry.snapshot(Date.now(), mountedDeviceIds(current.firepowerPoints)), ...current });
      }
      if (["GET", "HEAD"].includes(req.method) && ["/video/status", "/video/stream"].includes(path)) {
        const key = url.searchParams.get("uav");
        const config = { ...registry.videoConfig(key, mountedDeviceIds((await scenario.read()).firepowerPoints)), ffmpegPath, rtspTimeoutOption };
        return serveVideo(req, res, config, `/api${path}`, url.searchParams.get("id"), key);
      }
      if (req.method !== "POST" || !["/role", "/coordinates", "/situation-report"].includes(path)) {
        return json(res, 404, { error: "找不到此模拟操作" });
      }
      if (req.headers.origin && req.headers.origin !== `http://${req.headers.host}`) {
        return json(res, 403, { error: "请求来源无效" });
      }
      if (req.headers["content-type"]?.split(";")[0] !== "application/json") {
        return json(res, 415, { error: "请求应为 JSON" });
      }
      try {
        req.setEncoding("utf8");
        let content = "";
        for await (const chunk of req) {
          content += chunk;
          if (content.length > 16_384) return json(res, 413, { error: "模拟配置请求过大" });
        }
        let body;
        try { body = JSON.parse(content); } catch { return json(res, 400, { error: "请求内容无效" }); }
        if (typeof body?.key !== "string" || !body.key) return json(res, 400, { error: "请选择一个设备" });
        if (path === "/situation-report") {
          const address = registry.deviceIpForKey(body.key);
          if (!address || !isIPv4(address) || Number(address.split(".")[0]) < 1 || Number(address.split(".")[0]) > 223) {
            return json(res, 409, { error: "该监测席没有有效的设备单播 IP" });
          }
          const defaultReportHost = process.env.SITUATION_REPORT_HOST ||
            process.env.INTELLIGENT_COOPERATIVE_SERVER_BIND_HOST ||
            (mqttConfig.deviceStatusRelayIp && isIPv4(mqttConfig.deviceStatusRelayIp) ? mqttConfig.deviceStatusRelayIp : null) ||
            getLocalIPv4Address(address);
          const reportHost = (typeof body.reportHost === "string" && isIPv4(body.reportHost))
            ? body.reportHost : defaultReportHost;
          const reportPort = Number.isInteger(body.reportPort) && body.reportPort >= 1 && body.reportPort <= 65535
            ? body.reportPort : 28080;
          try {
            await sendSituationReport(address, reportHost, reportPort);
          } catch (error) {
            console.error("发送态势上报指令失败", error);
            return json(res, 502, { error: "发送态势上报指令失败" });
          }
          return json(res, 200, { sent: true, address, port: SITUATION_REPORT_PORT, reportHost, reportPort });
        }
        const result = await scenario.enqueue(async () => {
          const current = await scenario.read();
          const mounts = mountedDeviceIds(current.firepowerPoints);
          const deviceId = registry.deviceIdForKey(body.key);
          if (path === "/role" && body.role === "firepower" && mounts.has(deviceId)) {
            return { status: 409, error: "该设备已挂载到作业点位，如需设置独立作业单元请先解除挂载" };
          }
          if (path === "/role") registry.changeRole(body.key, body.role);
          else registry.saveCoordinates(body.key, body.coordinates, body.name);
          return { ...registry.snapshot(Date.now(), mounts), ...current };
        });
        if (result.status) return json(res, result.status, { error: result.error });
        return json(res, 200, result);
      } catch (error) {
        if (!error.status) console.error(error);
        return json(res, error.status || 500, { error: error.status ? error.message : "保存模拟设备配置失败" });
      }
    },
    close() {
      client?.end(true);
      registry.close();
    }
  };
}
