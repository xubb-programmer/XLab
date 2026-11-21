import { isIPv4 } from "node:net";
import { DatabaseSync } from "node:sqlite";
import { hasGeographicPosition } from "../src/js/core/coordinates.js";
import { appendTrajectoryPoint } from "../src/js/core/target-trajectory.js";
import { getCooperativeDatabase } from "./db.mjs";

const utf8 = new TextDecoder("utf-8", { fatal: true });
const text = value => typeof value === "string" ? value.trim() : "";
const timestamp = value => typeof value === "string" && Number.isFinite(Date.parse(value)) ? new Date(value).toISOString() : null;

function readAircraftUpdate(value) {
  if (!value || typeof value["有效"] !== "boolean") return null;
  const updatedAt = timestamp(value["更新时间"]);
  if (!value["有效"]) return { valid: false, updatedAt };
  const position = { longitude: value["经度"], latitude: value["纬度"], altitude: value["高程"], updatedAt };
  return updatedAt && hasGeographicPosition(position) ? { valid: true, updatedAt, position } : null;
}

function updateAircraftTrack(previous, update) {
  if (!update) return previous;
  if (update.updatedAt && previous?.statusAt &&
      (update.updatedAt < previous.statusAt || (update.valid && update.updatedAt === previous.statusAt))) return previous;
  if (!update.valid) {
    return { ...previous, valid: false, statusAt: update.updatedAt ?? previous?.statusAt ?? null };
  }
  return {
    valid: true,
    statusAt: update.updatedAt,
    position: update.position,
    trajectory: appendTrajectoryPoint(previous?.trajectory, update.position)
  };
}

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
          if (url.hostname) {
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
    videoChannels: readVideoChannels(sources, source),
    aircraftUpdate: readAircraftUpdate(Object.hasOwn(body, "飞机位置") ? body["飞机位置"] : packet["飞机位置"])
  };
}

function changeError(status, message) {
  return Object.assign(new Error(message), { status });
}

export function openRegistry(databasePath, onlineMs) {
  const database = databasePath && typeof databasePath === "object" && typeof databasePath.prepare === "function"
    ? databasePath
    : getCooperativeDatabase(databasePath);
  const observations = new Map();
  const received = new Set();
  const select = database.prepare("SELECT * FROM simulation_devices WHERE key = ?");
  const selected = database.prepare("SELECT * FROM simulation_devices WHERE firepower = 1");
  const selectById = database.prepare("SELECT key FROM simulation_devices WHERE device_id = ? AND firepower = 1");
  const storeSnapshot = database.prepare(`
    INSERT INTO device_snapshots (identity, report) VALUES (?, ?)
    ON CONFLICT(identity) DO UPDATE SET report = excluded.report, deleted = 0
  `);
  const deleteSnapshot = database.prepare("UPDATE device_snapshots SET deleted = 1 WHERE identity = ? AND deleted = 0");
  const deletedSnapshots = new Set(database.prepare("SELECT identity FROM device_snapshots WHERE deleted = 1")
    .all().map(row => row.identity));
  database.exec(`
    INSERT OR IGNORE INTO device_snapshots (identity, report)
    SELECT json_extract(last_report, '$.identity'), last_report FROM simulation_devices
    WHERE json_extract(last_report, '$.identity') IS NOT NULL ORDER BY rowid DESC;
  `);
  for (const { report } of database.prepare("SELECT report FROM device_snapshots WHERE deleted = 0").all()) {
    const restored = JSON.parse(report);
    observations.set(restored.identity, restored);
  }
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

  // 同步更新统一业务设备主表 (devices)
  const upsertDevice = database.prepare(`
    INSERT INTO devices (
      device_id, identity, device_key, name, custom_name, serial, device_ip,
      role, is_firepower, coordinates, video_channels, aircraft_position, aircraft_trajectory,
      last_report, last_seen, snapshot_at, source, deleted
    ) VALUES (
      ?, ?, ?, ?, ?, ?, ?,
      ?, ?, ?, ?, ?, ?,
      ?, ?, ?, ?, 0
    )
    ON CONFLICT(device_id) DO UPDATE SET
      identity = excluded.identity,
      device_key = excluded.device_key,
      name = excluded.name,
      custom_name = COALESCE(excluded.custom_name, devices.custom_name),
      serial = excluded.serial,
      device_ip = excluded.device_ip,
      role = excluded.role,
      is_firepower = excluded.is_firepower,
      coordinates = COALESCE(excluded.coordinates, devices.coordinates),
      video_channels = excluded.video_channels,
      aircraft_position = excluded.aircraft_position,
      aircraft_trajectory = excluded.aircraft_trajectory,
      last_report = excluded.last_report,
      last_seen = excluded.last_seen,
      snapshot_at = excluded.snapshot_at,
      source = excluded.source,
      deleted = 0
  `);
  const updateDeviceRole = database.prepare("UPDATE devices SET is_firepower = ?, role = ? WHERE device_id = ?");
  const updateDeviceCoords = database.prepare("UPDATE devices SET coordinates = ? WHERE device_id = ?");
  const updateDeviceName = database.prepare("UPDATE devices SET custom_name = ? WHERE device_id = ?");
  const markDeviceDeleted = database.prepare("UPDATE devices SET deleted = 1 WHERE device_id = ?");

  const online = (report, now) => received.has(report.identity) && now - report.lastSeen < onlineMs;

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
    const roleDefault = role === "firepower" ? "作业单元" : "监测席";
    const { aircraftTrack, ...device } = publicReport(report, active);
    return {
      ...device,
      name: customName || text(report.name) || `${roleDefault}-${report.deviceId}`,
      customName: customName || null,
      online: active,
      coordinates: settings?.coordinates ? JSON.parse(settings.coordinates) : null,
      canAssign: active && !duplicates.has(report.deviceId) && !selectById.get(report.deviceId),
      duplicateId: duplicates.has(report.deviceId),
      staleSnapshot: active && report.snapshotAt !== null && now - Date.parse(report.snapshotAt) > onlineMs
    };
  }

  function snapshot(now = Date.now(), mounts = new Set()) {
    const duplicates = duplicateIds(now);
    const assigned = selected.all();
    const firepower = assigned.filter(row => !mounts.has(row.device_id))
      .map(row => ({ row, report: getObservation(row.key) || JSON.parse(row.last_report) }))
      .filter(({ report }) => !deletedSnapshots.has(report.identity))
      .map(({ row, report }) => display(report, row, now, duplicates, "firepower"));
    const mountedById = new Map();
    for (const report of observations.values()) {
      if (!mounts.has(report.deviceId)) continue;
      const assignedKey = selectById.get(report.deviceId)?.key;
      if (assignedKey && assignedKey !== report.key) continue;
      const prior = mountedById.get(report.deviceId);
      if (!prior || report.lastSeen > prior.lastSeen) mountedById.set(report.deviceId, report);
    }
    const mounted = [...mountedById.values()].map(report => display(report, select.get(report.key), now, duplicates, "firepower"));
    // 挂载只改变工位的通信绑定，不改变设备的监测席身份。
    // 因此已挂载设备仍保留在监测列表，视频和飞机轨迹也继续使用同一份快照。
    const uavs = [...observations.values()]
      .map(report => display(report, select.get(report.key), now, duplicates, "recon"));
    const aircraft = [];
    for (const report of observations.values()) {
      const track = report.aircraftTrack;
      if (!track?.position) continue;
      const name = text(select.get(report.key)?.custom_name) || text(report.name) || report.deviceId;
      aircraft.push({
        id: report.identity,
        identity: report.identity,
        deviceId: report.deviceId,
        name,
        ...track.position,
        trajectory: track.trajectory,
        current: track.valid && online(report, now)
      });
    }
    const byId = (a, b) => a.deviceId.localeCompare(b.deviceId, "zh-CN", { numeric: true }) || a.key.localeCompare(b.key);
    return { uavs: uavs.sort(byId), firepower: firepower.sort(byId), mounted: mounted.sort(byId), aircraft };
  }

  return {
    isFirepower(deviceId) {
      return Boolean(selectById.get(deviceId));
    },
    standaloneIds() {
      return new Set(selected.all().map(row => row.device_id));
    },
    deviceIdForKey(key) {
      return getObservation(key)?.deviceId ?? select.get(key)?.device_id;
    },
    deviceIpForKey(key) {
      return getObservation(key)?.deviceIp;
    },
    importSnapshots(reports) {
      const insert = database.prepare("INSERT OR IGNORE INTO device_snapshots (identity, report) VALUES (?, ?)");
      database.exec("BEGIN IMMEDIATE");
      try {
        let imported = 0;
        for (const report of reports) {
          if (!report.identity || !report.key || !Number.isFinite(report.lastSeen)) continue;
          imported += insert.run(report.identity, JSON.stringify(publicReport(report, false))).changes;
        }
        database.exec("COMMIT");
        return imported;
      } catch (error) {
        database.exec("ROLLBACK");
        throw error;
      }
    },
    ingest({ aircraftUpdate, ...report }) {
      report.aircraftTrack = updateAircraftTrack(observations.get(report.identity)?.aircraftTrack, aircraftUpdate);
      const publicRep = publicReport(report);
      storeSnapshot.run(report.identity, JSON.stringify(publicRep));
      deletedSnapshots.delete(report.identity);
      observations.set(report.identity, report);
      received.add(report.identity);

      try {
        const settings = select.get(report.key);
        const isFp = settings ? settings.firepower : 0;
        upsertDevice.run(
          report.deviceId,
          report.identity,
          report.key,
          report.name || report.deviceId,
          settings?.custom_name || null,
          report.serial || null,
          report.deviceIp || null,
          isFp ? 'firepower' : 'recon',
          isFp,
          settings?.coordinates || null,
          JSON.stringify(publicRep.videoChannels || []),
          report.aircraftTrack?.position ? JSON.stringify(report.aircraftTrack.position) : null,
          report.aircraftTrack?.trajectory ? JSON.stringify(report.aircraftTrack.trajectory) : null,
          JSON.stringify(publicRep),
          report.lastSeen || Date.now(),
          report.snapshotAt || null,
          report.source || 'udp'
        );
      } catch (_) {}
    },
    removeSnapshot(key, mounts) {
      const settings = select.get(key);
      const report = getObservation(key) || (settings?.firepower && JSON.parse(settings.last_report));
      if (!report) throw changeError(404, "设备快照不存在");
      if (mounts.has(report.deviceId)) throw changeError(409, "设备已挂载，请先解除挂载");
      // 设备改过 ID 时，旧的独立作业单元仍可能引用同一个硬件身份。
      if (observations.has(report.identity) && observations.get(report.identity).key !== key) {
        throw changeError(409, "设备编号已变化，请核对设备身份");
      }
      if (!deleteSnapshot.run(report.identity).changes) throw changeError(404, "设备快照不存在");
      deletedSnapshots.add(report.identity);
      observations.delete(report.identity);
      received.delete(report.identity);
      try {
        markDeviceDeleted.run(report.deviceId);
      } catch (_) {}
    },
    expire() {
      // 不删除最后一份组播快照，以便离线设备仍可在卡片中显示为离线。
    },
    snapshot,
    videoConfig(key, mounts = new Set()) {
      const report = getObservation(key);
      // 独立作业标记和点位挂载都不影响监测视频访问。
      const usable = report && online(report, Date.now());
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
        try {
          updateDeviceRole.run(1, "firepower", latest.deviceId);
        } catch (_) {}
      } else if (role === "uav" && settings?.firepower) {
        setRole.run(key, settings.device_id, 0, latest ? JSON.stringify(publicReport(latest)) : settings.last_report);
        try {
          updateDeviceRole.run(0, "recon", settings.device_id);
        } catch (_) {}
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
      if (coordinates !== null) {
        try {
          updateDeviceCoords.run(JSON.stringify(coordinates), report.deviceId);
        } catch (_) {}
      }
      if (customName !== undefined) {
        setName.run(key, report.deviceId, JSON.stringify(publicReport(report)), normalizedName || null);
        try {
          updateDeviceName.run(normalizedName || null, report.deviceId);
        } catch (_) {}
      }
      return snapshot();
    },
    close: () => {
      if (typeof databasePath === "string" && databasePath === ":memory:") database.close();
    }
  };
}
