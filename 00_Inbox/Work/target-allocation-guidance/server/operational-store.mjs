import { DatabaseSync } from "node:sqlite";
import { mkdirSync } from "node:fs";
import { dirname } from "node:path";
import { randomUUID } from "node:crypto";
import { isIP } from "node:net";
import { hasGeographicPosition } from "../src/js/core/coordinates.js";
import { appendTrajectoryPoint } from "../src/js/core/target-trajectory.js";

export function dataError(status, message) {
  return Object.assign(new Error(message), { status });
}

export function fieldText(value, label, limit = 80) {
  if (typeof value !== "string" || value.trim().length > limit) {
    throw dataError(400, `${label}格式无效或过长`);
  }
  return value.trim();
}

export function readPosition(value, optional = false) {
  const { longitude, latitude, altitude } = value;
  if (optional && [longitude, latitude, altitude].every(item => item == null)) return null;
  if (!hasGeographicPosition(value)) throw dataError(400, "请填写有效的经度、纬度和高程");
  return { longitude, latitude, altitude };
}

function readPoint(point) {
  if (!point || typeof point.id !== "string" || !/^[a-zA-Z0-9][a-zA-Z0-9_-]{0,63}$/.test(point.id)) {
    throw dataError(400, "火力点编号无效");
  }
  const name = fieldText(point.name ?? "", "名称");
  if (!name) throw dataError(400, "请填写名称");
  const ip = fieldText(point.ip || point.host || "127.0.0.1", "通信 IP", 45);
  if (!isIP(ip)) throw dataError(400, "请填写有效的通信 IP");
  const port = point.port ?? 5567;
  if (!Number.isInteger(port) || port < 1 || port > 65535) throw dataError(400, "端口必须在 1 至 65535 之间");
  const maxTargets = point.maxTargets ?? 1;
  if (!Number.isInteger(maxTargets) || maxTargets < 1 || maxTargets > 50) throw dataError(400, "通道容量必须在 1 至 50 之间");
  const mountedDeviceId = fieldText(point.mountedDeviceId ?? "", "挂载设备编号", 400);
  const unmountedIp = typeof point.unmountedIp === "string" && isIP(point.unmountedIp) ? point.unmountedIp : undefined;
  return {
    id: point.id, name, ...readPosition(point),
    battalion: fieldText(point.battalion || "1营", "营名称"),
    company: fieldText(point.company || "1连", "连名称"),
    strikeRange: fieldText(String(point.strikeRange ?? (point.maxRangeKm ? `${point.maxRangeKm} km` : "40 km")), "范围"),
    maxTargets, ip, port, mountedDeviceId, unmountedIp
  };
}

function targetIdentity(type, targetNumber) {
  if (!["TARGET_RECON", "BURST_POINT"].includes(type) || !Number.isSafeInteger(targetNumber) || targetNumber <= 0) {
    throw dataError(400, "请选择目标类型并填写正整数编号");
  }
  return `${type === "BURST_POINT" ? "burst" : "recon"}-${targetNumber}`;
}

export function openOperationalStore(path) {
  if (path !== ":memory:") mkdirSync(dirname(path), { recursive: true });
  const db = new DatabaseSync(path);
  db.exec(`
    PRAGMA journal_mode = WAL;
    PRAGMA busy_timeout = 3000;
    CREATE TABLE IF NOT EXISTS firepower_points (
      id TEXT PRIMARY KEY, sort_order INTEGER NOT NULL, data TEXT NOT NULL
    );
    CREATE TABLE IF NOT EXISTS targets (
      id TEXT PRIMARY KEY, target_type TEXT NOT NULL, target_number INTEGER NOT NULL,
      name TEXT NOT NULL, longitude REAL NOT NULL, latitude REAL NOT NULL, altitude REAL NOT NULL,
      trajectory TEXT NOT NULL, source TEXT NOT NULL, updated_at INTEGER NOT NULL,
      deleted INTEGER NOT NULL DEFAULT 0, UNIQUE (target_type, target_number)
    );
    CREATE INDEX IF NOT EXISTS targets_visible ON targets(deleted, updated_at DESC);
  `);

  const pointRows = db.prepare("SELECT data FROM firepower_points ORDER BY sort_order, id");
  const pointRow = db.prepare("SELECT data FROM firepower_points WHERE id = ?");
  const insertPoint = db.prepare("INSERT INTO firepower_points VALUES (?, ?, ?)");
  const updatePoint = db.prepare("UPDATE firepower_points SET data = ? WHERE id = ?");
  const nextOrder = db.prepare("SELECT COALESCE(MAX(sort_order), -1) + 1 AS value FROM firepower_points");
  const deletePoint = db.prepare("DELETE FROM firepower_points WHERE id = ?");
  const targetRow = db.prepare("SELECT * FROM targets WHERE id = ?");
  const listTargetRows = db.prepare(`
    SELECT * FROM targets WHERE deleted = 0 AND instr(lower(name || ' ' || target_number || ' ' || id), lower(?)) > 0
    ORDER BY updated_at DESC, rowid DESC LIMIT ? OFFSET ?
  `);
  const countTargets = db.prepare(`
    SELECT count(*) AS total FROM targets WHERE deleted = 0
    AND instr(lower(name || ' ' || target_number || ' ' || id), lower(?)) > 0
  `);
  const putTarget = db.prepare(`
    INSERT INTO targets (id, target_type, target_number, name, longitude, latitude, altitude, trajectory, source, updated_at, deleted)
    VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 0)
    ON CONFLICT(id) DO UPDATE SET name = excluded.name, longitude = excluded.longitude,
      latitude = excluded.latitude, altitude = excluded.altitude, trajectory = excluded.trajectory,
      source = excluded.source, updated_at = excluded.updated_at, deleted = 0
  `);
  const deleteTarget = db.prepare("UPDATE targets SET deleted = 1, trajectory = '[]' WHERE id = ? AND deleted = 0");

  function transaction(work) {
    db.exec("BEGIN IMMEDIATE");
    try {
      const result = work();
      db.exec("COMMIT");
      return result;
    } catch (error) {
      db.exec("ROLLBACK");
      throw error;
    }
  }

  const points = () => pointRows.all().map(row => JSON.parse(row.data));

  function ensureUniqueMounts(records) {
    const mounted = new Set();
    for (const { mountedDeviceId } of records) {
      if (!mountedDeviceId) continue;
      if (mounted.has(mountedDeviceId)) throw dataError(409, "同一设备只能挂载到一个火力点");
      mounted.add(mountedDeviceId);
    }
  }

  function savePoint(value, id) {
    return transaction(() => {
      const key = id ?? `FIRE-${randomUUID()}`;
      const current = pointRow.get(key);
      if (id && !current) throw dataError(404, "火力点不存在");
      const old = current ? JSON.parse(current.data) : {};
      const merged = { ...old, ...value, id: key };
      if (old.mountedDeviceId && value.mountedDeviceId === "") {
        if (value.ip === undefined) merged.ip = old.unmountedIp ?? "127.0.0.1";
        delete merged.unmountedIp;
      }
      const point = readPoint(merged);
      ensureUniqueMounts([...points().filter(item => item.id !== key), point]);
      if (current) updatePoint.run(JSON.stringify(point), key);
      else insertPoint.run(key, nextOrder.get().value, JSON.stringify(point));
      return point;
    });
  }

  function toTarget(row) {
    return {
      id: row.id, type: row.target_type, targetNumber: row.target_number, name: row.name,
      longitude: row.longitude, latitude: row.latitude, altitude: row.altitude,
      trajectory: JSON.parse(row.trajectory), source: row.source, updatedAt: row.updated_at
    };
  }

  function writeTarget(target, source, trajectory) {
    putTarget.run(target.id, target.type, target.targetNumber, target.name, target.longitude, target.latitude,
      target.altitude, JSON.stringify(trajectory), source, Date.now());
    return toTarget(targetRow.get(target.id));
  }

  return {
    listPoints: points,
    savePoint,
    removePoint(id) {
      if (!deletePoint.run(id).changes) throw dataError(404, "火力点不存在");
    },
    listTargets(query, limit, offset) {
      return { records: listTargetRows.all(query, limit, offset).map(toTarget), total: countTargets.get(query).total };
    },
    saveTarget(value, id) {
      const row = id ? targetRow.get(id) : null;
      if (id && (!row || row.deleted)) throw dataError(404, "目标不存在");
      const target = { ...(row ? toTarget(row) : {}), ...value };
      target.id = targetIdentity(target.type, target.targetNumber);
      if (id && target.id !== id) throw dataError(400, "已有目标的类型和编号不能修改");
      const existing = targetRow.get(target.id);
      if (!id && existing && !existing.deleted) throw dataError(409, "该类型和编号的目标已存在");
      target.name = fieldText(target.name ?? "", "名称") || (target.type === "BURST_POINT" ? "炸点目标" : "侦察目标");
      Object.assign(target, readPosition(target));
      const trajectory = appendTrajectoryPoint(row ? JSON.parse(row.trajectory) : [], target);
      return writeTarget(target, row?.source ?? "manual", trajectory);
    },
    removeTarget(id) {
      if (!deleteTarget.run(id).changes) throw dataError(404, "目标不存在");
    },
    close: () => db.close()
  };
}
