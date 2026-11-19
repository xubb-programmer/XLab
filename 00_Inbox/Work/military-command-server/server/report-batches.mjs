import { DatabaseSync } from "node:sqlite";
import { randomUUID } from "node:crypto";
import { mkdirSync } from "node:fs";
import { dirname } from "node:path";
import { ReportError, reportIdPattern, validateReportAssignments } from "./reporting.mjs";

const retryableStates = new Set(["not_sent", "rejected", "not_processed"]);

export function openReportBatches(path, sendEdgeAction) {
  if (path !== ":memory:") mkdirSync(dirname(path), { recursive: true });
  const db = new DatabaseSync(path);
  const activeDevices = new Set();
  db.exec(`
    PRAGMA foreign_keys = ON;
    PRAGMA journal_mode = WAL;
    PRAGMA synchronous = FULL;
    PRAGMA busy_timeout = 3000;
    CREATE TABLE IF NOT EXISTS report_batches (
      id TEXT PRIMARY KEY, request_key TEXT NOT NULL, updated_at INTEGER NOT NULL,
      running INTEGER NOT NULL DEFAULT 0
    );
    CREATE TABLE IF NOT EXISTS report_items (
      batch_id TEXT NOT NULL REFERENCES report_batches(id),
      item_id INTEGER NOT NULL, device_id TEXT NOT NULL, details TEXT NOT NULL,
      state TEXT NOT NULL, request_id TEXT, error TEXT, timed_out INTEGER NOT NULL DEFAULT 0,
      PRIMARY KEY (batch_id, item_id)
    );
    CREATE INDEX IF NOT EXISTS report_items_device_state ON report_items(device_id, state);
    CREATE TABLE IF NOT EXISTS report_retries (
      batch_id TEXT NOT NULL REFERENCES report_batches(id), attempt_id TEXT NOT NULL,
      PRIMARY KEY (batch_id, attempt_id)
    );
  `);

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

  transaction(() => db.exec(`
    UPDATE report_items SET state = 'unknown', error = '服务中断，须核对接收端处理结果' WHERE state = 'sending';
    UPDATE report_items SET state = 'not_sent', error = '服务中断，尚未发送' WHERE state = 'pending';
    UPDATE report_batches SET running = 0 WHERE running = 1;
  `));

  const selectBatch = db.prepare("SELECT * FROM report_batches WHERE id = ?");
  const selectItems = db.prepare("SELECT * FROM report_items WHERE batch_id = ? ORDER BY item_id");
  const unknownDevices = db.prepare("SELECT DISTINCT device_id FROM report_items WHERE state = 'unknown'");
  const hasUnknown = db.prepare("SELECT 1 FROM report_items WHERE device_id = ? AND state = 'unknown' LIMIT 1");
  const insertBatch = db.prepare("INSERT INTO report_batches(id, request_key, updated_at, running) VALUES (?, ?, ?, 1)");
  const insertItem = db.prepare(`
    INSERT INTO report_items(batch_id, item_id, device_id, details, state) VALUES (?, ?, ?, ?, 'pending')
  `);
  const updateBatch = db.prepare("UPDATE report_batches SET running = ?, updated_at = ? WHERE id = ?");
  const updateItem = db.prepare(`
    UPDATE report_items SET state = ?, request_id = ?, error = ?, timed_out = ? WHERE batch_id = ? AND item_id = ?
  `);
  const findRetry = db.prepare("SELECT 1 FROM report_retries WHERE batch_id = ? AND attempt_id = ?");
  const insertRetry = db.prepare("INSERT INTO report_retries(batch_id, attempt_id) VALUES (?, ?)");
  const selectRecent = db.prepare(`
    SELECT b.id AS batchId, b.updated_at AS updatedAt, b.running,
      COUNT(*) AS totalCount, SUM(i.state = 'unknown') AS unknownCount
    FROM report_batches b JOIN report_items i ON i.batch_id = b.id
    GROUP BY b.id ORDER BY (SUM(i.state = 'unknown') > 0) DESC, b.updated_at DESC LIMIT 30
  `);

  function items(batchId) {
    return selectItems.all(batchId).map(row => ({
      ...JSON.parse(row.details), itemId: row.item_id, state: row.state,
      requestId: row.request_id, error: row.error, timeout: Boolean(row.timed_out)
    }));
  }

  function unavailableDevices() {
    return new Set([...activeDevices, ...unknownDevices.all().map(row => row.device_id)]);
  }

  function snapshot(batchId, requestKey) {
    const batch = selectBatch.get(batchId);
    if (!batch) return null;
    if (requestKey && requestKey !== batch.request_key) throw new ReportError(409, "批次编号已用于另一份指派方案");
    const blocked = unavailableDevices();
    const groupMap = new Map();
    const results = items(batchId).map(({ action, timeoutMs, ...item }) => {
      const result = {
        ...item, ok: item.state === "confirmed" || item.state === "mock",
        retryable: !batch.running && retryableStates.has(item.state) && !blocked.has(item.deviceId)
      };
      if (!groupMap.has(item.firepowerId)) groupMap.set(item.firepowerId, []);
      groupMap.get(item.firepowerId).push(result);
      return result;
    });
    const groupResults = [...groupMap.values()].map(group => {
      const ok = group.every(item => item.ok);
      const unknown = group.some(item => item.state === "unknown");
      const received = group.every(item => item.state === "confirmed" && item.error === null);
      return {
        firepowerId: group[0].firepowerId, firepowerName: group[0].firepowerName,
        ok, timeout: group.some(item => item.timeout),
        message: received ? "已收到接收端回执" : ok ? "上报已确认"
          : unknown ? "上报结果未知" : group.find(item => item.error)?.error || "本工位有未确认目标",
        totalCount: group.length, successCount: group.filter(item => item.ok).length, results: group
      };
    });
    const successCount = results.filter(item => item.ok).length;
    const unknownCount = results.filter(item => item.state === "unknown").length;
    const running = Boolean(batch.running);
    const ok = !running && successCount === results.length;
    return {
      batchId, running, updatedAt: batch.updated_at, ok, totalCount: results.length, successCount,
      unknownCount, retryableCount: results.filter(item => item.retryable).length,
      mock: results.some(item => item.state === "mock"), timeout: results.some(item => item.timeout),
      message: running ? "正在上报"
        : ok ? (groupResults.every(group => group.message === "已收到接收端回执") ? "已收到接收端回执" : "上报已确认")
        : unknownCount ? "上报结果未知"
        : groupResults.find(group => !group.ok)?.message || "本批有未确认目标",
      firepowerName: groupResults.map(group => group.firepowerName).join("、"),
      groupResults, results
    };
  }

  function saveItem(batchId, item, state, { requestId = item.requestId, error = null, timeout = false } = {}) {
    updateItem.run(state, requestId ?? null, typeof error === "string" ? error : null, Number(timeout), batchId, item.itemId);
  }

  async function run(batchId) {
    try {
      const devices = new Map();
      for (const item of items(batchId).filter(item => item.state === "pending")) {
        if (!devices.has(item.deviceId)) devices.set(item.deviceId, []);
        devices.get(item.deviceId).push(item);
      }
      for (const [deviceId, group] of devices) {
        if (activeDevices.has(deviceId) || hasUnknown.get(deviceId)) {
          const error = activeDevices.has(deviceId) ? "该设备正在处理另一批上报" : "该设备有待核实的目标";
          for (const item of group) saveItem(batchId, item, "not_sent", { error });
          continue;
        }
        activeDevices.add(deviceId);
        let paused = false;
        try {
          for (const item of group) {
            if (paused) {
              saveItem(batchId, item, "not_sent", { error: "该设备上报异常，后续目标已暂停" });
              continue;
            }
            const requestId = randomUUID();
            saveItem(batchId, item, "sending", { requestId });
            let result;
            try {
              result = item.mock ? { state: "mock" } : await sendEdgeAction({
                deviceId, requestId, params: item.params, timeoutMs: item.timeoutMs
              });
            } catch {
              result = { state: "unknown", pause: true, error: "发送中断，须核对接收端" };
            }
            saveItem(batchId, item, result.state, { ...result, requestId });
            paused = Boolean(result.pause);
          }
        } finally {
          activeDevices.delete(deviceId);
        }
      }
    } finally {
      transaction(() => {
        for (const item of items(batchId)) {
          if (item.state === "sending") saveItem(batchId, item, "unknown", { error: "发送中断，须核对接收端" });
          if (item.state === "pending") saveItem(batchId, item, "not_sent", { error: "本批发送中断" });
        }
        updateBatch.run(0, Date.now(), batchId);
      });
    }
    return snapshot(batchId);
  }

  function start(batchId, background) {
    const processing = run(batchId);
    if (!background) return processing;
    void processing.catch(error => console.error("上报批次处理异常", error));
    return snapshot(batchId);
  }

  return {
    snapshot,
    recent: () => selectRecent.all().map(row => ({ ...row, running: Boolean(row.running) })),
    async submit(request, prepared, background = false) {
      const existing = transaction(() => {
        const found = snapshot(request.batchId, request.requestKey);
        if (found) return found;
        insertBatch.run(request.batchId, request.requestKey, Date.now());
        prepared.forEach((item, index) => insertItem.run(request.batchId, index + 1, item.deviceId, JSON.stringify(item)));
        return null;
      });
      return existing || start(request.batchId, background);
    },
    async retry(batchId, attemptId, points, background = false) {
      if (typeof attemptId !== "string" || !reportIdPattern.test(attemptId)) throw new ReportError(400, "重试编号格式无效");
      const started = transaction(() => {
        const batch = selectBatch.get(batchId);
        if (!batch) throw new ReportError(404, "未找到上报批次");
        if (findRetry.get(batchId, attemptId)) return false;
        if (batch.running) throw new ReportError(409, "本批正在上报");
        const blocked = unavailableDevices();
        const eligible = items(batchId).filter(item => retryableStates.has(item.state) && !blocked.has(item.deviceId));
        validateReportAssignments(eligible, points);
        insertRetry.run(batchId, attemptId);
        if (!eligible.length) return false;
        for (const item of eligible) saveItem(batchId, item, "pending", { requestId: null });
        updateBatch.run(1, Date.now(), batchId);
        return true;
      });
      return started ? start(batchId, background) : snapshot(batchId);
    },
    resolveUnknown(batchId, itemId, decision) {
      if (!Number.isInteger(itemId) || !["confirmed", "not_processed"].includes(decision)) {
        throw new ReportError(400, "核实结果格式无效");
      }
      return transaction(() => {
        const batch = selectBatch.get(batchId);
        if (!batch) throw new ReportError(404, "未找到上报批次");
        if (batch.running) throw new ReportError(409, "本批正在上报");
        const item = items(batchId).find(item => item.itemId === itemId);
        if (!item) throw new ReportError(404, "未找到上报记录");
        if (item.state === decision) return snapshot(batchId);
        if (item.state !== "unknown") throw new ReportError(409, "该目标已更新，请查看当前状态");
        saveItem(batchId, item, decision, { error: decision === "confirmed" ? "人工确认接收端已处理" : "人工确认接收端未处理" });
        updateBatch.run(0, Date.now(), batchId);
        return snapshot(batchId);
      });
    },
    close: () => db.close()
  };
}
