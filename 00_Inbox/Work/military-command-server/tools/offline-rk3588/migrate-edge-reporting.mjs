import { createHash } from "node:crypto";
import { readFile, writeFile } from "node:fs/promises";

const [edgePath, scenarioPath, reportingPath, outputPath, templatePath] = process.argv.slice(2);
const readJson = async path => JSON.parse((await readFile(path, "utf8")).replace(/^\uFEFF/, ""));
const [edge, scenario, reporting] = await Promise.all([
  readJson(edgePath), readJson(scenarioPath), readJson(reportingPath).catch(() => ({}))
]);
const template = templatePath ? await readJson(templatePath) : null;
const v2 = edge.config_version === 2;
const devices = v2 ? edge.devices : edge.channels;
const deviceIds = new Set(devices.map(device => device.device_id));
const channelIds = new Set(edge.channels.map(channel => channel.channel_id));
const points = scenario.firepowerPoints ?? [scenario.firepowerPoint].filter(Boolean);
let added = 0;

for (const point of points) {
  // 显式设备 ID 也可能指向已接入同一 Broker 的另一台 EdgeAgent。
  if (point.deviceId || typeof point.id !== "string" ||
      !/^[a-zA-Z0-9][a-zA-Z0-9._-]{0,63}$/.test(point.id) || deviceIds.has(point.id)) continue;
  const endpoint = reporting?.endpoints?.[point.id] ?? reporting?.defaultEndpoint;
  const host = point.ip || point.host || endpoint?.host || "127.0.0.1";
  const port = Number(point.port || endpoint?.port || 5567);
  if (typeof host !== "string" || !host.trim() || !Number.isInteger(port) || port < 1 || port > 65535) {
    console.warn(`阵位 ${point.id} 的旧地址无效，请在 EdgeAgent 中配置通道`);
    continue;
  }
  const channelId = "firepower_" + createHash("sha256").update(point.id).digest("hex").slice(0, 16);
  if (channelIds.has(channelId)) throw new Error(`EdgeAgent 通道已存在：${channelId}`);
  const channel = {
    channel_id: channelId,
    type: "tcp",
    role: "client",
    remote_host: host.trim(),
    remote_port: port,
    frame: { kind: "delimiter", delimiter: "\n" }
  };
  const binding = { device_id: point.id, qos: 1, message_format: "json" };
  if (v2) devices.push({ ...binding, channel_id: channelId });
  edge.channels.push(v2 ? channel : { ...channel, ...binding });
  deviceIds.add(point.id);
  channelIds.add(channelId);
  added++;
}

if (edge.config_version === 2 && template?.config_version === 2) {
  const targetChannelId = "target-input-udp";
  const targetDeviceId = "target-input-device";
  const statusChannelId = "device-status-udp";
  const statusDeviceId = "device-status-device";
  const templateTargetChannel = template.channels.find(channel => channel.channel_id === targetChannelId);
  const currentTargetChannel = edge.channels.find(channel => channel.channel_id === targetChannelId);
  if (!currentTargetChannel && templateTargetChannel) {
    edge.channels.push(structuredClone(templateTargetChannel));
    channelIds.add(targetChannelId);
    added++;
  } else if (currentTargetChannel?.bind_port === 18080 && templateTargetChannel?.bind_port === 28080) {
    currentTargetChannel.bind_port = 28080;
    added++;
  }
  const templateTargetDevice = template.devices.find(device => device.device_id === targetDeviceId);
  if (templateTargetDevice && !deviceIds.has(targetDeviceId)) {
    edge.devices.push(structuredClone(templateTargetDevice));
    deviceIds.add(targetDeviceId);
    added++;
  }
  const templateChannel = template.channels.find(channel => channel.channel_id === statusChannelId);
  if (templateChannel && !channelIds.has(statusChannelId)) {
    edge.channels.push(structuredClone(templateChannel));
    channelIds.add(statusChannelId);
    added++;
  }
  const templateDevice = template.devices.find(device => device.device_id === statusDeviceId);
  if (templateDevice && !deviceIds.has(statusDeviceId)) {
    edge.devices.push(structuredClone(templateDevice));
    deviceIds.add(statusDeviceId);
    added++;
  }
}

if (added) {
  await writeFile(outputPath, JSON.stringify(edge, null, 2) + "\n", { flag: "wx" });
  console.log(`已补齐 ${added} 项 EdgeAgent 默认配置`);
}
