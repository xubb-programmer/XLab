import { DatabaseSync } from "node:sqlite";
import { createServer } from "node:http";
import { createReadStream } from "node:fs";
import { stat, readFile, writeFile, rename, unlink } from "node:fs/promises";
import { createHash, randomUUID } from "node:crypto";
import { extname, relative, resolve, sep } from "node:path";
import { fileURLToPath } from "node:url";
import { loadVideoConfig, serveVideo } from "./video.mjs";
import { openDeviceSimulation } from "./device-simulation.mjs";
import { loadTargetsConfig, openTargetFeed } from "./targets.mjs";
import { ReportError, loadReportingConfig, prepareReportItems, readReportRequest, reportCapacity, reportIdPattern } from "./reporting.mjs";
import { openReportBatches } from "./report-batches.mjs";
import { edgeDeviceIdPattern } from "./edge-actions.mjs";
import { readDataJson, serveDataManagement, writeDataError } from "./data-management.mjs";
import { dataError, openOperationalStore } from "./operational-store.mjs";
import { openPreviewRegistry } from "./preview-device-registry.mjs";
import { mountedDeviceId, mountedDeviceIds, validateMounts } from "./device-mounts.mjs";
import { hasGeographicPosition } from "../src/js/core/coordinates.js";

const root = fileURLToPath(new URL("../", import.meta.url));
const scenarioPath = process.env.MILITARY_COMMAND_SERVER_SCENARIO || resolve(root, "public/scenarios/scenario-default.json");
const port = Number(process.env.PORT ?? 4177);
const host = process.env.MILITARY_COMMAND_SERVER_BIND_HOST || "127.0.0.1";
const videoConfig = await loadVideoConfig(process.env.MILITARY_COMMAND_SERVER_VIDEO_CONFIG || resolve(root, "config/video.json"));
let deviceSimulation;
const managementDb = process.env.MILITARY_COMMAND_SERVER_MANAGEMENT_DB || resolve(root, "dist/data-preview/records.sqlite");
const operations = openOperationalStore(managementDb);
const managementRegistry = openPreviewRegistry(managementDb);
const offlineMapDir = resolve(root, "dist/offline-maps/combined-theaters");
let offlineMapDb;
let offlineMapTileQuery;
const targetsConfig = await loadTargetsConfig(
  process.env.MILITARY_COMMAND_SERVER_TARGETS_CONFIG || resolve(root, "config/targets.json")
);
const targetFeed = await openTargetFeed(targetsConfig, operations);
const reportBatches = openReportBatches(
  process.env.MILITARY_COMMAND_SERVER_REPORTING_DB || resolve(root, "dist/reporting/reporting.sqlite"),
  targetFeed.sendEdgeAction
);

function getDeviceSimulation() {
  deviceSimulation ??= openDeviceSimulation(videoConfig.rtspTimeoutOption, targetsConfig, {
    read: readScenarioRecords,
    enqueue: queueFirepowerWrite
  }).catch(error => {
    deviceSimulation = undefined;
    throw error;
  });
  return deviceSimulation;
}

function writeReportJson(res, status, body) {
  res.writeHead(status, { "Content-Type": "application/json; charset=utf-8", "Cache-Control": "no-store" });
  res.end(JSON.stringify(body));
}

async function readReportJson(req) {
  let text = "";
  req.setEncoding("utf8");
  for await (const chunk of req) text += chunk;
  try {
    return JSON.parse(text);
  } catch {
    throw new ReportError(400, "上报请求格式无效");
  }
}

async function readScenario() {
  const raw = await readFile(scenarioPath, "utf8");
  const scenario = JSON.parse(raw);
  return { scenario, scenarioRevision: createHash("sha256").update(raw).digest("hex") };
}

function scenarioRecords(scenario, scenarioRevision) {
  return {
    firepowerPoints: scenario.firepowerPoints ?? [scenario.firepowerPoint].filter(Boolean),
    scenarioUavs: scenario.uavs ?? [scenario.uav].filter(Boolean),
    scenarioRevision
  };
}

async function readScenarioRecords() {
  const { scenario, scenarioRevision } = await readScenario();
  return scenarioRecords(scenario, scenarioRevision);
}

async function readFirepowerPoints() {
  return (await readScenarioRecords()).firepowerPoints;
}

function checkScenarioRevision(expected, actual) {
  if (expected !== actual) throw dataError(409, "场景配置已更新，请刷新后重试");
}

async function handleReportRequest(req, res, batchRoute) {
  try {
    const [, batchId, action] = batchRoute ?? [];
    if (batchId && !reportIdPattern.test(batchId)) throw new ReportError(400, "批次编号格式无效");
    if (req.method === "GET" && !action) {
      if (!batchId) return writeReportJson(res, 200, { batches: reportBatches.recent() });
      const snapshot = reportBatches.snapshot(batchId);
      if (!snapshot) throw new ReportError(404, "未找到上报批次");
      return writeReportJson(res, 200, snapshot);
    }
    if (req.method !== "POST" || (batchId && !action)) {
      return writeReportJson(res, 405, { ok: false, error: "不支持此上报操作" });
    }
    const payload = await readReportJson(req);
    const background = req.headers.prefer === "respond-async";
    let result;
    if (action === "resolve") {
      result = reportBatches.resolveUnknown(batchId, payload?.itemId, payload?.decision);
    } else if (action === "retry") {
      result = await reportBatches.retry(batchId, payload?.attemptId, await readFirepowerPoints(), background);
    } else {
      const request = readReportRequest(payload);
      result = reportBatches.snapshot(request.batchId, request.requestKey);
      if (!result) {
        const config = await loadReportingConfig(
          process.env.MILITARY_COMMAND_SERVER_REPORTING_CONFIG || resolve(root, "config/reporting.json")
        );
        const prepared = prepareReportItems({
          request, config, firepowerPoints: await readFirepowerPoints(),
          getTarget: id => operations.getLiveTarget(id)
        });
        result = await reportBatches.submit(request, prepared, background);
      }
    }
    return writeReportJson(res, result.running ? 202 : 200, result);
  } catch (error) {
    if (error instanceof ReportError) {
      return writeReportJson(res, error.status, { ok: false, error: error.message, ...error.details });
    }
    console.error("目标上报处理异常", error);
    return writeReportJson(res, 500, { ok: false, error: "上报服务异常，请查询批次状态" });
  }
}

function openOfflineMap() {
  if (offlineMapDb) return offlineMapDb;
  let database;
  try {
    database = new DatabaseSync(resolve(offlineMapDir, "tiles.db"), { readOnly: true });
    offlineMapTileQuery = database.prepare(`
      SELECT tile_data FROM tiles WHERE zoom_level = ? AND tile_column = ? AND tile_row = ?
      ORDER BY rowid DESC LIMIT 1
    `);
    offlineMapDb = database;
    return offlineMapDb;
  } catch {
    database?.close();
    return null;
  }
}

const mimeTypes = new Map([
  [".html", "text/html; charset=utf-8"],
  [".css", "text/css; charset=utf-8"],
  [".js", "text/javascript; charset=utf-8"],
  [".mjs", "text/javascript; charset=utf-8"],
  [".json", "application/json; charset=utf-8"],
  [".png", "image/png"],
  [".webp", "image/webp"],
  [".jpg", "image/jpeg"],
  [".jpeg", "image/jpeg"],
  [".svg", "image/svg+xml"],
  [".wasm", "application/wasm"],
  [".mp4", "video/mp4"],
  [".webm", "video/webm"],
  [".csv", "text/csv; charset=utf-8"],
  [".czml", "application/json"],
  [".geojson", "application/json"]
]);

function resolveResource(pathname) {
  const mounts = [
    ["/cesium/", "node_modules/cesium/Build/Cesium"],
    ["/public/", "public"],
    ["/", "src"]
  ];

  const matched = mounts.find(([prefix]) => pathname.startsWith(prefix));
  if (!matched) return null;

  const [prefix, directory] = matched;
  const base = resolve(root, directory);
  const relativeSubpath = pathname.slice(prefix.length) || "index.html";
  const file = resolve(base, relativeSubpath);
  const tail = relative(base, file);

  if (!tail || tail === ".." || tail.startsWith(".." + sep) || resolve(base, tail) !== file) {
    return null;
  }
  return file;
}

function parseRange(rangeHeader, size) {
  const match = /^bytes=(\d*)-(\d*)$/.exec(rangeHeader);
  if (!match || (!match[1] && !match[2]) || size === 0) return null;
  const start = match[1] ? Number(match[1]) : Math.max(size - Number(match[2]), 0);
  const end = match[1] && match[2] ? Math.min(Number(match[2]), size - 1) : size - 1;
  return start < size && start <= end ? [start, end] : null;
}

let pendingFirepowerWrite = Promise.resolve();

function queueFirepowerWrite(write) {
  const result = pendingFirepowerWrite.then(write);
  pendingFirepowerWrite = result.catch(() => {});
  return result;
}

async function saveScenario(scenario) {
  const devices = await getDeviceSimulation();
  const raw = JSON.stringify({ ...scenario, revision: randomUUID() }, null, 2) + "\n";
  const current = scenarioRecords(scenario, createHash("sha256").update(raw).digest("hex"));
  const mounts = mountedDeviceIds(current.firepowerPoints);
  validateMounts(current.firepowerPoints, devices.standaloneIds());
  const temporaryPath = `${scenarioPath}.${randomUUID()}.tmp`;
  try {
    await writeFile(temporaryPath, raw, "utf-8");
    await rename(temporaryPath, scenarioPath);
  } catch (error) {
    await unlink(temporaryPath).catch(() => {});
    throw error;
  }
  return { ...current, ...devices.snapshot(Date.now(), mounts) };
}

async function deleteScenarioRecord(type, id, expectedRevision) {
  const { scenario, scenarioRevision } = await readScenario();
  checkScenarioRevision(expectedRevision, scenarioRevision);
  const current = scenarioRecords(scenario, scenarioRevision);
  const key = type === "firepower" ? "firepowerPoints" : "uavs";
  const rows = type === "firepower" ? current.firepowerPoints : current.scenarioUavs;
  if (!rows.some(record => record.id === id)) throw dataError(404, "场景记录不存在");
  scenario[key] = rows.filter(record => record.id !== id);
  delete scenario[type === "firepower" ? "firepowerPoint" : "uav"];
  return saveScenario(scenario);
}

async function saveFirepowerPoint(point, expectedRevision, kind) {
  const { scenario, scenarioRevision } = await readScenario();
  checkScenarioRevision(expectedRevision, scenarioRevision);
  if (kind === "recon") {
    const { scenarioUavs } = scenarioRecords(scenario, scenarioRevision);
    const idx = scenarioUavs.findIndex(existing => existing.id === point.id);
    if (idx < 0) throw dataError(404, "场景侦察席不存在");
    const { name, longitude, latitude, altitude } = point;
    scenarioUavs[idx] = { ...scenarioUavs[idx], name, longitude, latitude, altitude };
    scenario.uavs = scenarioUavs;
    return saveScenario(scenario);
  }
  if (!Array.isArray(scenario.firepowerPoints)) {
    scenario.firepowerPoints = scenario.firepowerPoint ? [scenario.firepowerPoint] : [];
  }
  const idx = scenario.firepowerPoints.findIndex(existing => existing.id === point.id);
  if (idx < 0) throw dataError(404, "场景火力点不存在");
  const saved = { ...scenario.firepowerPoints[idx], ...point };
  scenario.firepowerPoints[idx] = saved;

  return { ...await saveScenario(scenario), firepowerPoint: saved };
}

const server = createServer(async (req, res) => {
  try {
    const parsedUrl = new URL(req.url, "http://localhost");
    const pathname = decodeURIComponent(parsedUrl.pathname);

    if (pathname.startsWith("/api/management/")) {
      return await serveDataManagement(req, res, parsedUrl, pathname, {
        operations, registry: managementRegistry, targetFeed, readScenario: readScenarioRecords,
        deleteScenarioRecord, enqueue: queueFirepowerWrite, getDevices: getDeviceSimulation
      });
    }

    if (pathname.startsWith("/api/device-simulation/")) {
      try {
        return await (await getDeviceSimulation()).serve(req, res, parsedUrl);
      } catch (error) {
        console.error("设备管理监听不可用", error.message);
        return writeReportJson(res, 503, { error: "EdgeAgent设备状态通道不可用，请检查代理和MQTT配置" });
      }
    }

    if (req.method === "POST" && pathname === "/api/scenario/firepower/mount") {
      try {
        const { pointId, deviceId, scenarioRevision: expectedRevision } = await readDataJson(req);
        if (typeof pointId !== "string" || !pointId || typeof deviceId !== "string" || deviceId.length > 500) {
          return writeReportJson(res, 400, { error: "请选择火力点和挂载设备" });
        }
        const result = await queueFirepowerWrite(async () => {
          const { scenario, scenarioRevision } = await readScenario();
          checkScenarioRevision(expectedRevision, scenarioRevision);
          const points = scenario.firepowerPoints ?? [scenario.firepowerPoint].filter(Boolean);
          const point = points.find(item => item.id === pointId);
          if (!point) return { status: 404, error: "火力点不存在" };
          const id = mountedDeviceId(deviceId);
          if (deviceId.trim() && !id) return { status: 400, error: "挂载设备 ID 无效" };
          const devices = await getDeviceSimulation();
          const state = devices.snapshot(Date.now(), mountedDeviceIds(points));
          const device = [...(state?.uavs ?? []), ...(state?.mounted ?? [])].find(item => item.deviceId === id);
          if (id && mountedDeviceId(point.mountedDeviceId) !== id && (!device?.online || device.duplicateId)) {
            return { status: 409, error: "设备离线、ID 重复或角色已变更，请刷新后重试" };
          }
          for (const other of points) {
            if (other !== point && (!id || mountedDeviceId(other.mountedDeviceId) !== id)) continue;
            const nextId = other === point ? id : "";
            if (mountedDeviceId(other.mountedDeviceId) === nextId) continue;
            if (other.mountedDeviceId) {
              other.ip = other.unmountedIp ?? "127.0.0.1";
              delete other.unmountedIp;
            }
            other.mountedDeviceId = nextId;
            if (nextId) {
              other.unmountedIp = other.ip || "127.0.0.1";
              if (device?.deviceIp) other.ip = device.deviceIp;
            }
          }
          scenario.firepowerPoints = points;
          return saveScenario(scenario);
        });
        return writeReportJson(res, result.status ?? 200, result);
      } catch (error) {
        return writeDataError(res, error);
      }
    }

    // 允许前端批量保存战术编制隶属关系与火力点挂载
    if (req.method === "POST" && pathname === "/api/scenario/firepower/batch") {
      let body = "";
      req.on("data", chunk => { body += chunk; });
      req.on("end", async () => {
        try {
          const payload = JSON.parse(body);
          if (Array.isArray(payload?.firepowerPoints)) {
            if (payload.scenarioUavs !== undefined && !Array.isArray(payload.scenarioUavs)) {
              throw dataError(400, "场景侦察席格式无效");
            }
            const result = await queueFirepowerWrite(async () => {
              const { scenario, scenarioRevision } = await readScenario();
              checkScenarioRevision(payload.scenarioRevision, scenarioRevision);
              scenario.firepowerPoints = payload.firepowerPoints;
              if (payload.scenarioUavs) scenario.uavs = payload.scenarioUavs;
              return saveScenario(scenario);
            });
            res.writeHead(200, { "Content-Type": "application/json; charset=utf-8", "Cache-Control": "no-store" });
            res.end(JSON.stringify({ ok: true, ...result }));
            return;
          }
          res.writeHead(400, { "Content-Type": "application/json; charset=utf-8" });
          res.end(JSON.stringify({ ok: false, error: "缺少火力点列表。" }));
        } catch (e) {
          res.writeHead(e.status || 500, { "Content-Type": "application/json; charset=utf-8" });
          res.end(JSON.stringify({ ok: false, error: e.message }));
        }
      });
      return;
    }

    // 允许前端手动配置并保存地面火力点阵位信息
    if (req.method === "POST" && pathname === "/api/scenario/firepower") {
      let body = "";
      req.on("data", chunk => { body += chunk; });
      req.on("end", async () => {
        let payload;
        try {
          payload = JSON.parse(body);
        } catch {
          res.writeHead(400, { "Content-Type": "application/json; charset=utf-8" });
          res.end(JSON.stringify({ ok: false, error: "阵位配置格式无效。" }));
          return;
        }
        const id = payload?.id ?? "FIRE-01";
        const emptyReconPosition = payload?.kind === "recon" &&
          [payload.longitude, payload.latitude, payload.altitude].every(value => value == null);
        if (typeof id !== "string" || !/^[a-zA-Z0-9][a-zA-Z0-9_-]{0,63}$/.test(id) ||
            (!emptyReconPosition && !hasGeographicPosition(payload))) {
          res.writeHead(400, { "Content-Type": "application/json; charset=utf-8" });
          res.end(JSON.stringify({ ok: false, error: "请检查火力点编号和经纬高。" }));
          return;
        }
        const { name, longitude, latitude, altitude, strikeRange, ip, port, maxTargets, deviceId, battalion, company, mountedDeviceId } = payload;
        if (deviceId !== undefined && (typeof deviceId !== "string" || !edgeDeviceIdPattern.test(deviceId))) {
          res.writeHead(400, { "Content-Type": "application/json; charset=utf-8" });
          res.end(JSON.stringify({ ok: false, error: "请填写有效的 EdgeAgent 设备 ID。" }));
          return;
        }
        const updatedPoint = {
          id,
          name: typeof name === "string" && name.trim() ? name.trim() : "地面火力输出点",
          longitude,
          latitude,
          altitude,
          strikeRange: (typeof strikeRange === "string" && strikeRange.trim()) ||
            (Number.isFinite(strikeRange) && strikeRange) || "40 km",
          maxTargets: reportCapacity({ maxTargets }),
          ip: typeof ip === "string" && ip.trim() ? ip.trim() : (typeof payload?.host === "string" && payload.host.trim() ? payload.host.trim() : "127.0.0.1"),
          port: Number(port) || 5567
        };
        if (deviceId !== undefined) updatedPoint.deviceId = deviceId;
        if (battalion !== undefined) updatedPoint.battalion = String(battalion).trim();
        if (company !== undefined) updatedPoint.company = String(company).trim();
        if (mountedDeviceId !== undefined) updatedPoint.mountedDeviceId = String(mountedDeviceId).trim();
        try {
          const result = await queueFirepowerWrite(() => saveFirepowerPoint(updatedPoint, payload.scenarioRevision, payload.kind));
          res.writeHead(200, { "Content-Type": "application/json; charset=utf-8", "Cache-Control": "no-store" });
          res.end(JSON.stringify({ ok: true, ...result }));
        } catch (err) {
          if (!err.status) console.error("无法保存阵位配置:", err);
          res.writeHead(err.status || 500, { "Content-Type": "application/json; charset=utf-8" });
          res.end(JSON.stringify({ ok: false, error: err.status ? err.message : "无法写入场景配置，请重试。" }));
        }
      });
      return;
    }

    const batchRoute = /^\/api\/targets\/report\/([^/]+)(?:\/(retry|resolve))?$/.exec(pathname);
    if (pathname === "/api/targets/report" || batchRoute) return await handleReportRequest(req, res, batchRoute);

    if (req.method !== "GET" && req.method !== "HEAD") {
      res.writeHead(405, { Allow: "GET, HEAD" }).end("Method Not Allowed");
      return;
    }
    if (pathname === "/api/video/status" || pathname === "/api/video/stream") {
      const uavId = parsedUrl.searchParams.get("uav") ?? "UAV-RECON-01";
      const { scenarioUavs } = await readScenarioRecords();
      const config = scenarioUavs.some(uav => uav.id === uavId) ? videoConfig : { state: "unconfigured", streams: [] };
      serveVideo(req, res, config, pathname, parsedUrl.searchParams.get("id"), uavId);
      return;
    }
    if (pathname === "/public/scenarios/scenario-default.json") {
      const { scenario } = await readScenario();
      res.writeHead(200, { "Content-Type": "application/json; charset=utf-8", "Cache-Control": "no-store" });
      res.end(req.method === "HEAD" ? undefined : JSON.stringify(scenario));
      return;
    }
    if (pathname === "/api/targets") {
      res.writeHead(200, { "Content-Type": "application/json; charset=utf-8", "Cache-Control": "no-store" });
      res.end(req.method === "HEAD" ? undefined : JSON.stringify(targetFeed.snapshot()));
      return;
    }
    if (pathname === "/offline-maps/combined-theaters/manifest.json") {
      const database = openOfflineMap();
      const content = await readFile(resolve(offlineMapDir, "manifest.json"), "utf8").catch(() => null);
      if (!database || !content) {
        res.writeHead(404).end();
        return;
      }
      const manifest = JSON.parse(content);
      res.writeHead(200, { "Content-Type": "application/json; charset=utf-8", "Cache-Control": "no-store" });
      res.end(req.method === "HEAD" ? undefined : JSON.stringify(manifest));
      return;
    }

    // MapTileGenerator 为当前库保存北向下递增的 XYZ 行号。
    const tileMatch = pathname.match(/^\/api\/map\/tiles\/(\d+)\/(\d+)\/(\d+)\.(?:jpg|png|jpeg)$/);
    if (tileMatch) {
      const [, z, x, y] = tileMatch.map(Number);
      if (z <= 22 && x < 2 ** z && y < 2 ** z && openOfflineMap()) {
        const row = offlineMapTileQuery.get(String(z), x, y);
        if (row?.tile_data) {
          res.writeHead(200, {
            "Content-Type": "image/jpeg",
            "Content-Length": row.tile_data.length,
            "Cache-Control": "public, max-age=86400"
          });
          res.end(req.method === "HEAD" ? undefined : row.tile_data);
          return;
        }
      }
      res.writeHead(404, { "Content-Type": "text/plain" }).end("Tile not found");
      return;
    }
    const file = resolveResource(pathname);
    const fileStat = file ? await stat(file).catch(() => null) : null;

    if (!fileStat || !fileStat.isFile()) {
      res.writeHead(404, { "Content-Type": "text/plain; charset=utf-8" }).end("404 Not Found");
      return;
    }

    const ext = extname(file).toLowerCase();
    const headers = {
      "Content-Type": mimeTypes.get(ext) ?? "application/octet-stream",
      "Content-Length": fileStat.size,
      "Cache-Control": "no-store, no-cache, must-revalidate, max-age=0",
      "Pragma": "no-cache",
      "Expires": "0",
      "Accept-Ranges": "bytes"
    };

    let readOptions;
    if (req.headers.range) {
      const range = parseRange(req.headers.range, fileStat.size);
      if (!range) {
        res.writeHead(416, { "Content-Range": `bytes */${fileStat.size}` }).end();
        return;
      }
      const [start, end] = range;
      readOptions = { start, end };
      headers["Content-Range"] = `bytes ${start}-${end}/${fileStat.size}`;
      headers["Content-Length"] = end - start + 1;
    }

    res.writeHead(readOptions ? 206 : 200, headers);
    if (req.method === "HEAD") {
      res.end();
    } else {
      createReadStream(file, readOptions)
        .on("error", () => res.destroy())
        .pipe(res);
    }
  } catch (err) {
    res.writeHead(500, { "Content-Type": "text/plain; charset=utf-8" }).end("Internal Server Error");
  }
});

server.listen(port, host, () => {
  const { address, port } = server.address();
  const urlHost = address.includes(":") ? `[${address}]` : address;
  console.log(`[Military Command Server] Server listening at http://${urlHost}:${port}/`);
  void getDeviceSimulation().catch(error => console.error("设备状态订阅启动失败", error));
});
server.on("close", () => {
  targetFeed.close();
  reportBatches.close();
  managementRegistry.close();
  operations.close();
  void deviceSimulation?.then(simulation => simulation.close()).catch(() => {});
  offlineMapDb?.close();
});
