import { existsSync } from "node:fs";
import { mkdir } from "node:fs/promises";
import { isIPv4 } from "node:net";
import { resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { decodeReport, openRegistry } from "./device-simulation-registry.mjs";
import { serveVideo } from "./video.mjs";
import { mountedDeviceIds } from "./device-mounts.mjs";

const namespace = "/api/device-simulation";
const MAX_UDP_BYTES = 65_507;

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
  const stateDir = process.env.DEVICE_SIM_STATE_DIR || fileURLToPath(new URL("../dist/device-status-simulation/", import.meta.url));
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
      if (req.method !== "POST" || !["/role", "/coordinates"].includes(path)) {
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
        const result = await scenario.enqueue(async () => {
          const current = await scenario.read();
          const mounts = mountedDeviceIds(current.firepowerPoints);
          const deviceId = registry.deviceIdForKey(body.key);
          if (path === "/role" && body.role === "firepower" && mounts.has(deviceId)) {
            return { status: 409, error: "该设备已挂载到火力阵位，如需设置独立火力点请先解除挂载" };
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
