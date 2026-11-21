import { DatabaseSync } from "node:sqlite";
import { mkdirSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const root = fileURLToPath(new URL("../", import.meta.url));
export const DEFAULT_DATABASE_PATH = resolve(root, "data/cooperative.sqlite");

const dbInstances = new Map();

/**
 * 初始化并返回统一业务数据库连接 (单例模式)
 * 涵盖：设备状态台账 (devices)、阵位作战单元 (firepower_points)、态势目标 (targets)、
 * 协同上报批次 (report_batches)、上报分发明细 (report_items)、重试流水 (report_retries) 等。
 * 全面开启外键互联约束 (foreign_keys = ON) 与 WAL 高性能事务日志。
 */
export function getCooperativeDatabase(customPath) {
  const dbPath = customPath && customPath !== ":memory:"
    ? resolve(root, customPath)
    : (customPath === ":memory:" ? ":memory:" : DEFAULT_DATABASE_PATH);

  if (dbInstances.has(dbPath)) {
    return dbInstances.get(dbPath);
  }

  if (dbPath !== ":memory:") {
    mkdirSync(dirname(dbPath), { recursive: true });
  }

  const db = new DatabaseSync(dbPath);

  // 基础运行环境与性能 PRAGMA
  db.exec(`
    PRAGMA foreign_keys = ON;
    PRAGMA journal_mode = WAL;
    PRAGMA busy_timeout = 5000;
    PRAGMA synchronous = NORMAL;
  `);

  // 初始化统一核心业务数据表结构 (主键互联、外键级联)
  db.exec(`
    -- 1. 设备主表 (终端硬件/侦察席/组播在线设备)
    CREATE TABLE IF NOT EXISTS devices (
      device_id TEXT PRIMARY KEY,
      identity TEXT UNIQUE,
      device_key TEXT,
      name TEXT NOT NULL,
      custom_name TEXT,
      serial TEXT,
      device_ip TEXT,
      role TEXT NOT NULL DEFAULT 'recon',
      is_firepower INTEGER NOT NULL DEFAULT 0,
      coordinates TEXT,
      video_channels TEXT,
      aircraft_position TEXT,
      aircraft_trajectory TEXT,
      last_report TEXT,
      last_seen INTEGER,
      snapshot_at TEXT,
      source TEXT,
      deleted INTEGER NOT NULL DEFAULT 0
    );
    CREATE INDEX IF NOT EXISTS idx_devices_identity ON devices(identity);
    CREATE INDEX IF NOT EXISTS idx_devices_role ON devices(is_firepower, deleted);

    -- 2. 设备组播快照缓存表
    CREATE TABLE IF NOT EXISTS device_snapshots (
      identity TEXT PRIMARY KEY,
      report TEXT NOT NULL,
      deleted INTEGER NOT NULL DEFAULT 0
    );

    -- 3. 阵位 / 作战单元表 (主外键互联: mounted_device_id 关联 devices(device_id))
    CREATE TABLE IF NOT EXISTS firepower_points (
      id TEXT PRIMARY KEY,
      name TEXT NOT NULL,
      battalion TEXT NOT NULL DEFAULT '1区',
      company TEXT NOT NULL DEFAULT '1组',
      longitude REAL NOT NULL,
      latitude REAL NOT NULL,
      altitude REAL NOT NULL,
      strike_range TEXT NOT NULL DEFAULT '40 km',
      max_targets INTEGER NOT NULL DEFAULT 1,
      ip TEXT NOT NULL DEFAULT '127.0.0.1',
      port INTEGER NOT NULL DEFAULT 5567,
      mounted_device_id TEXT,
      unmounted_ip TEXT,
      sort_order INTEGER NOT NULL DEFAULT 0,
      data TEXT NOT NULL,
      updated_at INTEGER NOT NULL DEFAULT 0,
      FOREIGN KEY (mounted_device_id) REFERENCES devices(device_id) ON DELETE SET NULL
    );
    CREATE INDEX IF NOT EXISTS idx_points_mounted ON firepower_points(mounted_device_id);
    CREATE INDEX IF NOT EXISTS idx_points_sort ON firepower_points(sort_order, id);

    -- 4. 侦察与炸点目标审计表
    CREATE TABLE IF NOT EXISTS targets (
      id TEXT PRIMARY KEY,
      target_type TEXT NOT NULL,
      target_number INTEGER NOT NULL,
      name TEXT NOT NULL,
      longitude REAL NOT NULL,
      latitude REAL NOT NULL,
      altitude REAL NOT NULL,
      trajectory TEXT NOT NULL DEFAULT '[]',
      source TEXT NOT NULL DEFAULT 'manual',
      updated_at INTEGER NOT NULL,
      deleted INTEGER NOT NULL DEFAULT 0,
      UNIQUE (target_type, target_number)
    );
    CREATE INDEX IF NOT EXISTS idx_targets_visible ON targets(deleted, updated_at DESC);

    -- 5. 态势上报实时流表
    CREATE TABLE IF NOT EXISTS live_targets (
      id TEXT PRIMARY KEY,
      target_type TEXT NOT NULL,
      target_number INTEGER NOT NULL,
      name TEXT NOT NULL,
      longitude REAL NOT NULL,
      latitude REAL NOT NULL,
      altitude REAL NOT NULL,
      trajectory TEXT NOT NULL DEFAULT '[]',
      updated_at INTEGER NOT NULL,
      ordinal INTEGER NOT NULL,
      generation TEXT NOT NULL DEFAULT ''
    );
    CREATE INDEX IF NOT EXISTS idx_live_targets_recent ON live_targets(ordinal DESC);

    -- 6. 目标协同上报与打击批次主表
    CREATE TABLE IF NOT EXISTS report_batches (
      id TEXT PRIMARY KEY,
      request_key TEXT NOT NULL,
      updated_at INTEGER NOT NULL,
      running INTEGER NOT NULL DEFAULT 0
    );
    CREATE INDEX IF NOT EXISTS idx_batches_updated ON report_batches(updated_at DESC);

    -- 7. 上报分发明细表 (多主键互联: 级联关联批次、阵位、设备)
    CREATE TABLE IF NOT EXISTS report_items (
      batch_id TEXT NOT NULL REFERENCES report_batches(id) ON DELETE CASCADE,
      item_id INTEGER NOT NULL,
      device_id TEXT NOT NULL,
      point_id TEXT,
      target_id TEXT,
      details TEXT NOT NULL,
      state TEXT NOT NULL DEFAULT 'pending',
      request_id TEXT,
      error TEXT,
      timed_out INTEGER NOT NULL DEFAULT 0,
      PRIMARY KEY (batch_id, item_id),
      FOREIGN KEY (point_id) REFERENCES firepower_points(id) ON DELETE SET NULL
    );
    CREATE INDEX IF NOT EXISTS idx_report_items_device_state ON report_items(device_id, state);
    CREATE INDEX IF NOT EXISTS idx_report_items_point_state ON report_items(point_id, state);

    -- 8. 上报重试记录表 (外键级联关联批次)
    CREATE TABLE IF NOT EXISTS report_retries (
      batch_id TEXT NOT NULL REFERENCES report_batches(id) ON DELETE CASCADE,
      attempt_id TEXT NOT NULL,
      PRIMARY KEY (batch_id, attempt_id)
    );

    -- 9. 监测席位台账表
    CREATE TABLE IF NOT EXISTS recon_seats (
      key TEXT PRIMARY KEY,
      device_id TEXT NOT NULL UNIQUE,
      name TEXT NOT NULL,
      ip TEXT NOT NULL,
      coordinates TEXT,
      deleted INTEGER NOT NULL DEFAULT 0
    );

    -- 10. 设备状态与角色兼容表
    CREATE TABLE IF NOT EXISTS simulation_devices (
      key TEXT PRIMARY KEY,
      device_id TEXT NOT NULL,
      firepower INTEGER NOT NULL DEFAULT 0,
      last_report TEXT NOT NULL,
      coordinates TEXT,
      custom_name TEXT
    );
    CREATE UNIQUE INDEX IF NOT EXISTS idx_sim_firepower_id ON simulation_devices(device_id) WHERE firepower = 1;
  `);

  dbInstances.set(dbPath, db);
  return db;
}

/**
 * 安全关闭所有活动数据库连接
 */
export function closeAllDatabases() {
  for (const [path, db] of dbInstances.entries()) {
    try {
      db.close();
    } catch (_) {}
  }
  dbInstances.clear();
}
