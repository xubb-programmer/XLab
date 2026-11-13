import { isIPv4 } from "node:net";
import { DatabaseSync } from "node:sqlite";

const utf8 = new TextDecoder("utf-8", { fatal: true });
const text = value => typeof value === "string" ? value.trim() : "";
const timestamp = value => typeof value === "string" && Number.isFinite(Date.parse(value)) ? new Date(value).toISOString() : null;

function readVideoChannels(sources, source) {
  if (!Array.isArray(sources)) return [];
  const channels = new Map();
  for (const [index, entry] of sources.entries()) {
    if (!entry || typeof entry !== "object") continue;
    const id = text(entry["视频源ID"] ?? entry.id) || `source-${index + 1}`;
    if (channels.has(id)) continue;
    const label = text(entry["视频名称"] ?? entry.name) || `视频 ${index + 1}`;
    const published = entry["发布中"];
    const connection = text(entry["连接状态"]).toLowerCase();
    const unavailable = ["idle", "disconnected", "disabled", "stopped", "error"].includes(connection);
    let rtspUrl = "";
    if (entry["视频源开关"] !== false && published !== false && (published === true || !unavailable)) {
      for (const value of [entry["播放地址"], entry["视频源地址"] ?? entry.url]) {
        try {
          const url = new URL(text(value));
          if (url.protocol !== "rtsp:") continue;
          if (["127.0.0.1", "localhost"].includes(url.hostname)) url.hostname = source;
          if (url.hostname === source) {
            rtspUrl = url.href;
            break;
          }
        } catch {
          // 广播可同时包含其他媒体协议。
        }
      }
    }
    channels.set(id, { id, label, rtspUrl });
  }
  return [...channels.values()];
}

function publicReport(report, active = true) {
  return {
    ...report,
    videoChannels: (report.videoChannels ?? []).map(channel => ({
      id: channel.id, label: channel.label, available: active && Boolean(channel.rtspUrl)
    }))
  };
}

export function decodeReport(datagram, source, now = Date.now()) {
  let packet;
  try {
    packet = JSON.parse(utf8.decode(datagram));
  } catch {
    return null;
  }
  if (!packet || typeof packet !== "object" || Array.isArray(packet)) return null;
  const legacy = !Object.hasOwn(packet, "协议版本");
  if (!legacy && packet["协议版本"] !== "2.0") return null;
  const body = legacy ? packet : packet["数据"];
  const deviceId = text(packet[legacy ? "设备编号" : "设备ID"]);
  if (!deviceId || !body || typeof body !== "object" || Array.isArray(body)) return null;

  const serial = text(packet[legacy ? "序列号" : "设备序列号"]);
  const identity = serial ? `serial:${serial}` : `source:${source}:${deviceId}`;
  const sources = body[legacy ? "外部视频源列表" : "视频源列表"];
  const streams = body["推流通道列表"];
  const deviceIp = text(packet["设备IP"]);
  return {
    key: JSON.stringify([deviceId, identity]), identity, deviceId, serial,
    name: text(packet["设备名称"]),
    deviceIp: isIPv4(deviceIp) ? deviceIp : null,
    source, version: legacy ? "旧版" : "2.0",
    lastSeen: now, snapshotAt: legacy ? null : timestamp(packet["快照时间"]),
    videoSources: Array.isArray(sources) ? sources.length : 0,
    streamingChannels: Array.isArray(streams) ? streams.length : 0,
    videoChannels: readVideoChannels(sources, source)
  };
}

function changeError(status, message) {
  return Object.assign(new Error(message), { status });
}

export function openRegistry(databasePath, onlineMs) {
  const database = new DatabaseSync(databasePath);
  const observations = new Map();
  database.exec(`
    CREATE TABLE IF NOT EXISTS simulation_devices (
      key TEXT PRIMARY KEY,
      device_id TEXT NOT NULL,
      firepower INTEGER NOT NULL DEFAULT 0,
      last_report TEXT NOT NULL,
      coordinates TEXT,
      custom_name TEXT
    );
    CREATE UNIQUE INDEX IF NOT EXISTS simulation_firepower_id
      ON simulation_devices(device_id) WHERE firepower = 1;
  `);
  const columns = new Set(database.prepare("PRAGMA table_info(simulation_devices)").all().map(column => column.name));
  if (!columns.has("custom_name")) {
    database.exec("ALTER TABLE simulation_devices ADD COLUMN custom_name TEXT");
  }
  const select = database.prepare("SELECT * FROM simulation_devices WHERE key = ?");
  const selected = database.prepare("SELECT * FROM simulation_devices WHERE firepower = 1");
  const selectById = database.prepare("SELECT key FROM simulation_devices WHERE device_id = ? AND firepower = 1");
  const updateReport = database.prepare("UPDATE simulation_devices SET last_report = ? WHERE key = ? AND firepower = 1");
  const setRole = database.prepare(`
    INSERT INTO simulation_devices (key, device_id, firepower, last_report) VALUES (?, ?, ?, ?)
    ON CONFLICT(key) DO UPDATE SET firepower = excluded.firepower, last_report = excluded.last_report
  `);
  const setCoordinates = database.prepare(`
    INSERT INTO simulation_devices (key, device_id, last_report, coordinates) VALUES (?, ?, ?, ?)
    ON CONFLICT(key) DO UPDATE SET coordinates = excluded.coordinates
  `);
  const setName = database.prepare(`
    INSERT INTO simulation_devices (key, device_id, last_report, custom_name) VALUES (?, ?, ?, ?)
    ON CONFLICT(key) DO UPDATE SET last_report = excluded.last_report, custom_name = excluded.custom_name
  `);

  const online = (report, now) => now - report.lastSeen < onlineMs;

  function getObservation(key) {
    return [...observations.values()].find(report => report.key === key);
  }

  function duplicateIds(now) {
    const ids = new Set();
    const duplicates = new Set();
    for (const report of observations.values()) {
      if (!online(report, now)) continue;
      if (ids.has(report.deviceId)) duplicates.add(report.deviceId);
      ids.add(report.deviceId);
    }
    return duplicates;
  }

  function display(report, settings, now, duplicates, role) {
    const active = observations.get(report.identity)?.key === report.key && online(report, now);
    const customName = text(settings?.custom_name);
    const roleDefault = role === "firepower" ? "火力点" : "侦察席";
    return {
      ...publicReport(report, active),
      name: customName || text(report.name) || `${roleDefault}-${report.deviceId}`,
      customName: customName || null,
      online: active,
      coordinates: settings?.coordinates ? JSON.parse(settings.coordinates) : null,
      canAssign: active && !duplicates.has(report.deviceId) && !selectById.get(report.deviceId),
      duplicateId: duplicates.has(report.deviceId),
      staleSnapshot: active && report.snapshotAt !== null && now - Date.parse(report.snapshotAt) > onlineMs
    };
  }

  function snapshot(now = Date.now()) {
    const duplicates = duplicateIds(now);
    const assigned = selected.all();
    const assignedKeys = new Set(assigned.map(row => row.key));
    const firepower = assigned.map(row => display(getObservation(row.key) || JSON.parse(row.last_report), row, now, duplicates, "firepower"));
    // 保留已发现但已超时的侦察设备，让前端能显示真实的离线状态；是否在线仍由心跳超时计算。
    const uavs = [...observations.values()].filter(report => !assignedKeys.has(report.key))
      .map(report => display(report, select.get(report.key), now, duplicates, "recon"));
    const byId = (a, b) => a.deviceId.localeCompare(b.deviceId, "zh-CN", { numeric: true }) || a.key.localeCompare(b.key);
    return { uavs: uavs.sort(byId), firepower: firepower.sort(byId) };
  }

  return {
    ingest(report) {
      observations.set(report.identity, report);
      updateReport.run(JSON.stringify(publicReport(report)), report.key);
    },
    expire() {
      // 不删除最后一份组播快照，以便离线设备仍可在卡片中显示为离线。
    },
    snapshot,
    videoConfig(key) {
      const report = getObservation(key);
      const usable = report && online(report, Date.now()) && !select.get(key)?.firepower;
      const streams = usable ? report.videoChannels.map(channel => ({ ...channel, uavId: key })) : [];
      return { state: streams.some(channel => channel.rtspUrl) ? "configured" : "unconfigured", streams };
    },
    changeRole(key, role, now = Date.now()) {
      const settings = select.get(key);
      const latest = getObservation(key);
      if (role === "firepower") {
        if (!latest || !online(latest, now)) throw changeError(409, "设备已离线，请等待下一次广播");
        if (duplicateIds(now).has(latest.deviceId)) throw changeError(409, "检测到重复设备 ID，请先核对设备");
        const assigned = selectById.get(latest.deviceId);
        if (assigned && assigned.key !== key) throw changeError(409, "该 ID 已绑定另一台设备，请先移回原设备");
        setRole.run(key, latest.deviceId, 1, JSON.stringify(publicReport(latest)));
      } else if (role === "uav" && settings?.firepower) {
        setRole.run(key, settings.device_id, 0, latest ? JSON.stringify(publicReport(latest)) : settings.last_report);
      } else {
        throw changeError(400, "请选择有效的设备分类");
      }
      return snapshot(now);
    },
    saveCoordinates(key, coordinates, customName) {
      const settings = select.get(key);
      const latest = getObservation(key);
      const report = latest || (settings?.last_report ? JSON.parse(settings.last_report) : null);
      if (!report) throw changeError(404, "设备已离线，请等待下一次广播");
      if (customName !== undefined && customName !== null && typeof customName !== "string") {
        throw changeError(400, "设备名称格式无效");
      }
      const normalizedName = customName == null ? null : text(customName);
      if (normalizedName && normalizedName.length > 80) throw changeError(400, "设备名称不能超过 80 个字符");
      if (coordinates !== null) {
        const { longitude, latitude, altitude } = coordinates ?? {};
        if (!Number.isFinite(longitude) || Math.abs(longitude) > 180 || !Number.isFinite(latitude) ||
            Math.abs(latitude) > 90 || !Number.isFinite(altitude)) {
          throw changeError(400, "请填写有效的经度、纬度和高度");
        }
        coordinates = {
          longitude, latitude, altitude,
          ...(coordinates.strikeRange ? { strikeRange: coordinates.strikeRange } : {}),
          ...(coordinates.maxTargets != null ? { maxTargets: Number(coordinates.maxTargets) || 1 } : {}),
          ...(coordinates.ip ? { ip: coordinates.ip } : {}),
          ...(coordinates.port ? { port: Number(coordinates.port) || 5567 } : {})
        };
      }
      setCoordinates.run(key, report.deviceId, JSON.stringify(publicReport(report)), coordinates === null ? null : JSON.stringify(coordinates));
      if (customName !== undefined) {
        setName.run(key, report.deviceId, JSON.stringify(publicReport(report)), normalizedName || null);
      }
      return snapshot();
    },
    close: () => database.close()
  };
}
