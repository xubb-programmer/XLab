import { DatabaseSync } from "node:sqlite";
import { randomUUID } from "node:crypto";
import { isIPv4 } from "node:net";
import { dataError, fieldText, readPosition } from "./operational-store.mjs";

export function openPreviewRegistry(path) {
  const db = new DatabaseSync(path);
  db.exec(`
    PRAGMA busy_timeout = 3000;
    CREATE TABLE IF NOT EXISTS recon_seats (
      key TEXT PRIMARY KEY, device_id TEXT NOT NULL UNIQUE, name TEXT NOT NULL,
      ip TEXT NOT NULL, coordinates TEXT, deleted INTEGER NOT NULL DEFAULT 0
    );
  `);
  const all = db.prepare("SELECT * FROM recon_seats WHERE deleted = 0 ORDER BY name, key");
  const byKey = db.prepare("SELECT * FROM recon_seats WHERE key = ?");
  const byId = db.prepare("SELECT * FROM recon_seats WHERE device_id = ?");
  const save = db.prepare(`
    INSERT INTO recon_seats VALUES (?, ?, ?, ?, ?, 0)
    ON CONFLICT(key) DO UPDATE SET device_id = excluded.device_id, name = excluded.name,
      ip = excluded.ip, coordinates = excluded.coordinates, deleted = 0
  `);
  const remove = db.prepare("UPDATE recon_seats SET deleted = 1 WHERE key = ? AND deleted = 0");
  return {
    snapshot: () => ({
      uavs: all.all().map(row => ({
        key: row.key, identity: row.key, deviceId: row.device_id, name: row.name, customName: row.name,
        deviceIp: row.ip, coordinates: JSON.parse(row.coordinates ?? "null"), online: false, videoSources: 0
      })),
      firepower: []
    }),
    managedDevice(key) {
      const row = byKey.get(key);
      return row && !row.deleted ? { deviceId: row.device_id } : null;
    },
    saveManaged(value, key) {
      const old = key ? byKey.get(key) : null;
      if (key && (!old || old.deleted)) throw dataError(404, "侦察席不存在");
      const id = fieldText(value.deviceId ?? old?.device_id ?? "", "设备 ID");
      const name = fieldText(value.name ?? old?.name ?? "", "名称");
      if (!id || !name) throw dataError(400, "请填写设备 ID 和名称");
      const ip = fieldText(value.ip ?? old?.ip ?? "", "设备 IP", 45);
      if (ip && !isIPv4(ip)) throw dataError(400, "请填写有效的 IPv4 地址");
      const duplicate = byId.get(id);
      if (duplicate && duplicate.key !== key && (!duplicate.deleted || key)) throw dataError(409, "设备 ID 已存在");
      const recordKey = key ?? duplicate?.key ?? randomUUID();
      const coordinates = readPosition({ ...JSON.parse(old?.coordinates ?? "null"), ...value }, true);
      save.run(recordKey, id, name, ip, coordinates ? JSON.stringify(coordinates) : null);
      return recordKey;
    },
    removeManaged(key) {
      if (!remove.run(key).changes) throw dataError(404, "侦察席不存在");
    },
    close: () => db.close()
  };
}
