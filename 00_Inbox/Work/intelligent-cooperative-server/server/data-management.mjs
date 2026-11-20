import { dataError } from "./operational-store.mjs";
import { mountedDeviceId, mountedDeviceIds } from "./device-mounts.mjs";

export function writeJson(res, status, body) {
  res.writeHead(status, { "Content-Type": "application/json; charset=utf-8", "Cache-Control": "no-store" });
  res.end(JSON.stringify(body));
}

export async function readDataJson(req) {
  const protocol = req.socket.encrypted ? "https" : "http";
  if (req.headers["sec-fetch-site"] === "cross-site" ||
      (req.headers.origin && req.headers.origin !== `${protocol}://${req.headers.host}`)) {
    throw dataError(403, "请求来源无效");
  }
  if (req.method === "DELETE") return null;
  if (req.headers["content-type"]?.split(";")[0] !== "application/json") throw dataError(415, "请求应为 JSON");
  let raw = "";
  req.setEncoding("utf8");
  for await (const chunk of req) {
    raw += chunk;
    if (raw.length > 2_000_000) throw dataError(413, "请求内容过大");
  }
  try {
    const payload = JSON.parse(raw);
    if (!payload || typeof payload !== "object" || Array.isArray(payload)) throw new Error();
    return payload;
  } catch {
    throw dataError(400, "请求内容格式无效");
  }
}

export function writeDataError(res, error) {
  if (!error.status) console.error("数据管理操作失败", error);
  return writeJson(res, error.status ?? 500, { ok: false, error: error.status ? error.message : "数据管理操作失败" });
}

export async function serveDataManagement(req, res, url, pathname, {
  operations, registry, targetFeed, readScenario, deleteScenarioRecord, enqueue, getDevices
}) {
  try {
    const route = /^\/api\/management\/(recon|firepower|targets)(?:\/(.+))?$/.exec(pathname);
    if (!route) throw dataError(404, "找不到该管理入口");
    const [, type, id] = route;
    if (id?.length > 500) throw dataError(400, "记录编号无效");
    const { method } = req;
    if (method === "GET" && !id) {
      const query = (url.searchParams.get("q") ?? "").trim().toLowerCase().slice(0, 100);
      const offset = Math.min(1_000_000, Math.max(0, Math.trunc(Number(url.searchParams.get("offset"))) || 0));
      const limit = 30;
      if (type === "targets") {
        const { records, total } = operations.listTargets(query, limit, offset);
        return writeJson(res, 200, { records: records.map(record => ({
          ...record, kind: record.live ? "live-target" : "manual"
        })), total });
      }
      const current = await readScenario();
      const mounts = mountedDeviceIds(current.firepowerPoints);
      const devices = await getDevices();
      const snapshot = devices.snapshot(Date.now(), mounts);
      const mounted = new Map(snapshot.mounted.map(device => [device.deviceId, device]));
      const rows = type === "recon" ? [
        ...snapshot.uavs.map(device => deviceRecord(device, "device")),
        ...current.scenarioUavs.map(uav => ({ ...uav, name: uav.name || uav.callsign || uav.id, kind: "scenario", live: true })),
        ...registry.snapshot().uavs.filter(device => !devices.isFirepower(device.deviceId))
          .map(device => deviceRecord(device, "manual"))
      ] : [
        ...current.firepowerPoints.map(point => {
          const device = mounted.get(mountedDeviceId(point.mountedDeviceId));
          return { ...point, kind: "scenario", live: true, online: Boolean(device?.online),
            ip: device?.deviceIp || point.ip, mountedDeviceName: device?.name };
        }),
        ...[...snapshot.firepower, ...snapshot.mounted].map(device => ({
          ...deviceRecord(device, "device"),
          mountedAt: current.firepowerPoints.find(point => mountedDeviceId(point.mountedDeviceId) === device.deviceId)?.id
        })),
        ...operations.listPoints().map(point => ({ ...point, origin: "point", kind: "manual", online: false }))
      ];
      const matched = rows.filter(row => [row.id, row.name, row.callsign, row.deviceId, row.ip, row.battalion, row.company, row.mountedDeviceId]
        .some(value => String(value ?? "").toLowerCase().includes(query)));
      return writeJson(res, 200, { records: matched.slice(offset, offset + limit), total: matched.length,
        scenarioRevision: current.scenarioRevision });
    }

    const create = method === "POST" && !id;
    if (!create && !(id && (method === "PUT" || method === "DELETE"))) throw dataError(405, "不支持此管理操作");
    const value = await readDataJson(req);
    const remove = method === "DELETE";
    const kind = url.searchParams.get("kind") ?? "manual";
    if (kind !== "manual") {
      if (!remove) throw dataError(405, "此记录仅支持查看和删除");
      if (kind === "live-target" && type === "targets") {
        targetFeed.removeTarget(id, url.searchParams.get("generation"));
      } else if (type !== "targets" && kind === "scenario") {
        await enqueue(() => deleteScenarioRecord(type, id, url.searchParams.get("revision")));
      } else if (type !== "targets" && kind === "device") {
        await enqueue(async () => {
          const current = await readScenario();
          (await getDevices()).removeSnapshot(id,
            mountedDeviceIds([...current.firepowerPoints, ...operations.listPoints()]));
        });
      } else {
        throw dataError(400, "记录来源无效");
      }
      return writeJson(res, 200, { ok: true });
    }
    let record;
    if (type === "targets") {
      if (remove) operations.removeTarget(id);
      else record = operations.saveTarget(value, id);
    } else if (type === "firepower") {
      if (remove) operations.removePoint(id);
      else record = operations.savePoint(value, id);
    } else {
      record = await enqueue(async () => {
        const current = await readScenario();
        const devices = await getDevices();
        const device = id ? registry.managedDevice(id) : null;
        const deviceId = value?.deviceId ?? device?.deviceId;
        const mounts = mountedDeviceIds(current.firepowerPoints);
        if (!remove && typeof deviceId === "string" && devices.isFirepower(deviceId.trim())) {
          throw dataError(409, "该设备 ID 已设为独立作业单元，请先取消独立作业单元");
        }
        if (device && (remove || (value.deviceId !== undefined && value.deviceId !== device.deviceId))) {
          const linked = mountedDeviceIds([...current.firepowerPoints, ...operations.listPoints()]);
          if (linked.has(id) || linked.has(device.deviceId)) {
            throw dataError(409, "该设备已挂载到作业单元，请先解除挂载");
          }
        }
        if (remove) return registry.removeManaged(id);
        return { id: registry.saveManaged(value, id) };
      });
    }
    return writeJson(res, create ? 201 : 200, { ok: true, record });
  } catch (error) {
    return writeDataError(res, error);
  }
}

function deviceRecord(device, kind) {
  return {
    ...device.coordinates, id: device.key, deviceId: device.deviceId, name: device.name, customName: device.customName,
    ip: device.deviceIp ?? "", online: device.online, videoSources: device.videoSources,
    origin: "recon",
    kind, live: kind === "device", manual: kind === "manual"
  };
}
