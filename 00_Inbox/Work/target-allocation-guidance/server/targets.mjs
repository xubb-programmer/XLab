import { readFile } from "node:fs/promises";
import { randomUUID } from "node:crypto";
import { isIPv4 } from "node:net";
import { createEdgeActionSender } from "./edge-actions.mjs";
import { appendTrajectoryPoint } from "../src/js/core/target-trajectory.js";

const DEFAULT_TOPIC = "edge/v1/devices/plc_udp_dev/data";
const DEFAULT_DEVICE_STATUS_TOPIC = "edge/v1/devices/plc_udp_multicast_dev/data";
const HEADER_LENGTH = 26;
const MAX_RECORDS = 300;

export async function loadTargetsConfig(file) {
  try {
    const config = JSON.parse((await readFile(file, "utf8")).replace(/^\uFEFF/, ""));
    const {
      brokerUrl = "", topic = DEFAULT_TOPIC, deviceStatusTopic = DEFAULT_DEVICE_STATUS_TOPIC,
      deviceStatusRelayIp = "",
      username = "", password = ""
    } = config ?? {};
    if (!config || typeof config !== "object" || Array.isArray(config)
        || ![brokerUrl, topic, deviceStatusTopic, deviceStatusRelayIp, username, password].every(value => typeof value === "string")
        || !/^edge\/v1\/devices\/[^/+#\s]+\/data$/.test(topic)
        || !/^edge\/v1\/devices\/[^/+#\s]+\/data$/.test(deviceStatusTopic)
        || (deviceStatusRelayIp !== "" && !isIPv4(deviceStatusRelayIp))) return { state: "invalid" };
    if (!brokerUrl.trim()) return { state: "unconfigured" };
    const url = new URL(brokerUrl.trim());
    if (!["mqtt:", "mqtts:"].includes(url.protocol) || !url.hostname || url.username || url.password
        || (url.pathname && url.pathname !== "/") || url.search || url.hash) return { state: "invalid" };
    return { state: "connecting", brokerUrl: url.href, topic, deviceStatusTopic, deviceStatusRelayIp, username, password };
  } catch (error) {
    return { state: error.code === "ENOENT" ? "unconfigured" : "invalid" };
  }
}

export function decode1501Envelope(message) {
  if (message.length > 16_384) return null;
  let envelope;
  try {
    envelope = JSON.parse(message.toString("utf8"));
  } catch {
    return null;
  }
  const hex = envelope?.data;
  if (envelope?.transport !== "udp" || typeof hex !== "string"
      || ![156, 328].includes(hex.length) || !/^[0-9a-f]+$/i.test(hex)) return null;

  const packet = Buffer.from(hex, "hex");
  if (packet[0] !== 0x90 || packet[1] !== 0xeb || packet[4] !== 80) return null;
  const isBurst = packet[5] === 60;
  if (!isBurst && packet[5] !== 69) return null;
  const length = isBurst ? 52 : 138;
  if (packet.length !== HEADER_LENGTH + length || packet.readUInt16LE(9) !== length) return null;

  const body = packet.subarray(HEADER_LENGTH);
  if (body[0] !== Number(isBurst) || (!isBurst && body[9] !== 1)) return null;
  const targetNumber = body.readDoubleLE(1);
  const longitude = body.readDoubleLE(19);
  const latitude = body.readDoubleLE(27);
  const altitude = isBurst ? body.readInt32LE(35) : body.readDoubleLE(35);
  if (!Number.isSafeInteger(targetNumber) || targetNumber <= 0
      || !Number.isFinite(longitude) || Math.abs(longitude) > 180
      || !Number.isFinite(latitude) || Math.abs(latitude) > 90 || !Number.isFinite(altitude)) return null;

  return {
    id: `${isBurst ? "burst" : "recon"}-${targetNumber}`,
    type: isBurst ? "BURST_POINT" : "TARGET_RECON",
    name: isBurst ? "炸点目标" : "侦察目标",
    targetNumber, longitude, latitude, altitude
  };
}

export async function openTargetFeed(config, operations) {
  const instance = randomUUID();
  let state = config.state;
  let revision = 0;
  let client;
  const records = new Map(operations.recentLiveTargets(MAX_RECORDS).reverse().map(target => [target.id, target]));
  const feed = {
    snapshot: () => ({
      state, instance, revision, targets: [...records.values()].reverse()
    }),
    sendEdgeAction: () => Promise.resolve({ state: "not_sent", pause: true, error: "MQTT 未配置或不可用，上报未发出" }),
    close: () => client?.end(true)
  };
  if (state !== "connecting") return feed;

  const mqtt = await import("mqtt").catch(() => null);
  if (!mqtt) {
    state = "dependency_missing";
    return feed;
  }
  const { topic, brokerUrl, username, password } = config;
  client = mqtt.connect(brokerUrl, {
    protocolVersion: 4, clean: true, reconnectPeriod: 2000, connectTimeout: 10_000,
    resubscribe: false, queueQoSZero: false, username: username || undefined, password: password || undefined
  });
  feed.sendEdgeAction = createEdgeActionSender(client);
  client.on("connect", () => {
    client.subscribe(topic, { qos: 1 }, (error, granted) => {
      if (!client.connected) return;
      state = !error && granted?.some(entry => entry.topic === topic && entry.qos <= 1)
        ? "subscribed" : "subscription_failed";
    });
  });
  client.on("message", (receivedTopic, message, packet) => {
    if (receivedTopic !== topic || packet.retain) return;
    const target = decode1501Envelope(message);
    if (!target) return;
    try {
      const cached = records.get(target.id);
      const previousTrajectory = (cached ?? operations.getLiveTarget(target.id))?.trajectory;
      const trajectory = appendTrajectoryPoint(previousTrajectory, target);
      if (cached && trajectory === previousTrajectory) return;
      const saved = operations.saveLiveTarget(target, trajectory);
      records.delete(target.id);
      records.set(target.id, saved);
      if (records.size > MAX_RECORDS) records.delete(records.keys().next().value);
      revision++;
    } catch (error) {
      console.error("实时目标入库失败", error);
    }
  });
  client.on("error", () => { state = "connection_error"; });
  client.on("offline", () => { state = "disconnected"; });
  client.on("reconnect", () => { state = "connecting"; });
  return feed;
}
