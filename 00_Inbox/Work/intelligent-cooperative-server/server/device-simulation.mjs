import { randomUUID } from "node:crypto";
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

function buildSituationReportPayload(reportHost, reportPort = 28080) {
  return JSON.stringify({
    "定时上报": true,
    "上报间隔毫秒": 2000,
    "上报地址": reportHost,
    "上报端口": reportPort,
    "目标编号": null,
    "目标类型": null,
    "最大速度米每秒": null
  });
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

export async function openDeviceSimulation(rtspTimeoutOption, mqttConfig, scenario, dbInstance) {
  const rawSource = process.env.DEVICE_SIM_SOURCE_IPS ?? (mqttConfig.deviceSimSourceIps || "");
  const sourceIps = rawSource.split(",").map(address => address.trim()).filter(Boolean);
  const allowAnySource = sourceIps.length === 0;
  const source = allowAnySource ? "自动接收局域网设备 (未设来源限制)" : sourceIps.join(", ");
  const relaySource = process.env.DEVICE_SIM_RELAY_IP || mqttConfig.deviceStatusRelayIp || "";
  const onlineMs = integerSetting("DEVICE_SIM_ONLINE_MS", 5000, 1000, 300000);
  const topic = process.env.DEVICE_STATUS_TOPIC || mqttConfig.deviceStatusTopic;
  const registry = openRegistry(
    dbInstance ?? resolve(fileURLToPath(new URL("../", import.meta.url)), "data/cooperative.sqlite"),
    onlineMs
  );
  const dataDir = resolve(fileURLToPath(new URL("../", import.meta.url)), "data");
  const portable = resolve(dataDir, "ffmpeg.exe");
  const ffmpegPath = process.env.DEVICE_SIM_FFMPEG_PATH || (process.platform === "win32" && existsSync(portable) ? portable : "ffmpeg");

  const valid = (allowAnySource || sourceIps.every(isIPv4))
    && (!relaySource || isIPv4(relaySource)) && Boolean(topic);
  const targetDeviceId = (mqttConfig.topic || "").match(/^edge\/v1\/devices\/([^/]+)\/data$/)?.[1] || "target-input-device";
  const commandTopic = `edge/v1/devices/${targetDeviceId}/commands`;
  const resultTopic = `edge/v1/devices/${targetDeviceId}/command-results`;
  const pendingCommands = new Map();

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
    const subscribeTopics = [topic, resultTopic];
    client.subscribe(subscribeTopics, { qos: 1 }, (error, granted) => {
      if (!client.connected) return;
      state = !error && granted?.some(entry => entry.topic === topic && entry.qos <= 1)
        ? "subscribed" : "subscription_failed";
      receiveError = state === "subscription_failed" ? "无法订阅EdgeAgent设备状态" : null;
    });
  });
  client?.on("message", (receivedTopic, message, packet) => {
    if (packet.retain) return;
    if (receivedTopic === resultTopic) {
      if (pendingCommands.size === 0 || message.length > 65_536) return;
      try {
        const envelope = JSON.parse(message.toString("utf8"));
        const reqId = envelope?.id;
        const pending = pendingCommands.get(reqId);
        if (pending) {
          if (envelope.status === "sent") {
            pending.resolve(envelope);
          } else {
            const errCode = envelope.error?.code || "send_failed";
            const errMsg = envelope.error?.message || "EdgeAgent 指令发送未被接收";
            pending.reject(new Error(`EdgeAgent ${errCode}: ${errMsg}`));
          }
        }
      } catch {
        // 忽略非标准格式回执
      }
      return;
    }
    if (receivedTopic !== topic || message.length > MAX_UDP_BYTES * 2 + 512) return;
    let envelope;
    try {
      envelope = JSON.parse(message.toString("utf8"));
    } catch {
      return;
    }
    const hex = envelope?.data;
    const peerAddress = envelope?.meta?.endpoint?.address;
    if (envelope?.transport !== "udp" || typeof hex !== "string" || hex.length > MAX_UDP_BYTES * 2
        || hex.length % 2 !== 0 || !/^[0-9a-f]*$/i.test(hex)) return;
    const isAllowedPeer = allowAnySource
      ? (peerAddress && isIPv4(peerAddress) && !peerAddress.startsWith("127."))
      : (sourceIps.includes(peerAddress) || peerAddress === relaySource);
    if (!isAllowedPeer) return;
    const reportSource = peerAddress || (sourceIps[0] || "127.0.0.1");
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
  client?.on("close", () => {
    for (const pending of pendingCommands.values()) {
      pending.reject(new Error("MQTT 已断开，态势上报指令未发出"));
    }
    pendingCommands.clear();
  });
  client?.on("offline", () => { state = "disconnected"; });
  client?.on("reconnect", () => { state = "connecting"; });
  if (client) console.log(`[Device Simulation] MQTT订阅 ${topic} 与 ${resultTopic}，设备来源 ${source}，转发来源 ${relaySource}`);

  function sendSituationReport(address, reportHost, reportPort = 28080, timeoutMs = 4000) {
    if (!client || !client.connected) {
      return Promise.reject(new Error("MQTT 未连接，无法通过 EdgeAgent 下发指令"));
    }
    const host = (typeof reportHost === "string" && isIPv4(reportHost))
      ? reportHost
      : getLocalIPv4Address(address);
    const jsonPayload = buildSituationReportPayload(host, reportPort);
    const hex = Buffer.from(jsonPayload, "utf8").toString("hex");
    const requestId = randomUUID();
    const commandEnvelope = {
      id: requestId,
      transport: "udp",
      data: hex,
      meta: {
        endpoint: {
          address,
          port: SITUATION_REPORT_PORT
        }
      }
    };

    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        if (pendingCommands.has(requestId)) {
          pendingCommands.delete(requestId);
          reject(new Error("等待 EdgeAgent 回执超时"));
        }
      }, timeoutMs);

      pendingCommands.set(requestId, {
        resolve: result => {
          clearTimeout(timer);
          pendingCommands.delete(requestId);
          resolve(result);
        },
        reject: error => {
          clearTimeout(timer);
          pendingCommands.delete(requestId);
          reject(error);
        }
      });

      client.publish(commandTopic, JSON.stringify(commandEnvelope), { qos: 1, retain: false }, error => {
        if (error) {
          clearTimeout(timer);
          pendingCommands.delete(requestId);
          reject(new Error(`向 EdgeAgent 发布命令失败: ${error.message}`));
        }
      });
    });
  }

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
          const manualIp = typeof body.deviceIp === "string" && isIPv4(body.deviceIp.trim()) ? body.deviceIp.trim() : null;
          const address = manualIp || registry.deviceIpForKey(body.key);
          if (!address || !isIPv4(address) || Number(address.split(".")[0]) < 1 || Number(address.split(".")[0]) > 223) {
            return json(res, 409, { error: "该监测席没有有效的设备单播 IP，请在席位配置中指定或等待广播" });
          }
          const autoSubnetHost = getLocalIPv4Address(address);
          const defaultReportHost = process.env.SITUATION_REPORT_HOST ||
            process.env.INTELLIGENT_COOPERATIVE_SERVER_BIND_HOST ||
            autoSubnetHost ||
            (mqttConfig.deviceStatusRelayIp && isIPv4(mqttConfig.deviceStatusRelayIp) ? mqttConfig.deviceStatusRelayIp : null) ||
            "127.0.0.1";
          const reportHost = (typeof body.reportHost === "string" && isIPv4(body.reportHost))
            ? body.reportHost : defaultReportHost;
          const reportPort = Number.isInteger(body.reportPort) && body.reportPort >= 1 && body.reportPort <= 65535
            ? body.reportPort : 28080;
          try {
            await sendSituationReport(address, reportHost, reportPort);
          } catch (error) {
            console.error("通过 EdgeAgent 发送态势上报指令失败", error);
            return json(res, 502, { error: error.message || "发送态势上报指令失败" });
          }
          return json(res, 200, { sent: true, via: "edgeagent", address, port: SITUATION_REPORT_PORT, reportHost, reportPort });
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
      for (const pending of pendingCommands.values()) {
        pending.reject(new Error("服务正在关闭"));
      }
      pendingCommands.clear();
      client?.end(true);
      registry.close();
    }
  };
}
