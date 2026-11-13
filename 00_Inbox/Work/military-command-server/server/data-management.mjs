import { dataError } from "./operational-store.mjs";

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

export async function serveDataManagement(req, res, url, pathname, operations, registry, deviceRoles) {
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
      if (type === "targets") return writeJson(res, 200, operations.listTargets(query, limit, offset));
      const rows = type === "recon"
        ? registry.snapshot().uavs.filter(device => !deviceRoles?.(device.deviceId)).map(deviceRecord)
        : operations.listPoints().map(point => ({ ...point, origin: "point", online: false }));
      const matched = rows.filter(row => [row.id, row.name, row.deviceId, row.ip, row.battalion, row.company, row.mountedDeviceId]
        .some(value => String(value ?? "").toLowerCase().includes(query)));
      return writeJson(res, 200, { records: matched.slice(offset, offset + limit), total: matched.length });
    }

    const create = method === "POST" && !id;
    if (!create && !(id && (method === "PUT" || method === "DELETE"))) throw dataError(405, "不支持此管理操作");
    const value = await readDataJson(req);
    const remove = method === "DELETE";
    let record;
    if (type === "targets") {
      if (remove) operations.removeTarget(id);
      else record = operations.saveTarget(value, id);
    } else if (type === "firepower") {
      if (remove) operations.removePoint(id);
      else record = operations.savePoint(value, id);
    } else {
      const device = id ? registry.managedDevice(id) : null;
      const deviceId = value?.deviceId ?? device?.deviceId;
      if (!remove && typeof deviceId === "string" && deviceRoles?.(deviceId.trim())) {
        throw dataError(409, "该设备 ID 已是火力点，请先转回侦察席");
      }
      if (device && (remove || (value.deviceId !== undefined && value.deviceId !== device.deviceId))) {
        const mounted = operations.listPoints().some(point =>
          point.mountedDeviceId === id || point.mountedDeviceId === device.deviceId);
        if (mounted) throw dataError(409, "该设备已挂载到火力点，请先解除挂载");
      }
      if (remove) registry.removeManaged(id);
      else record = { id: registry.saveManaged(value, id) };
    }
    return writeJson(res, create ? 201 : 200, { ok: true, record });
  } catch (error) {
    return writeDataError(res, error);
  }
}

function deviceRecord(device) {
  return {
    ...device.coordinates, id: device.key, deviceId: device.deviceId, name: device.name, customName: device.customName,
    ip: device.deviceIp ?? "", online: device.online, videoSources: device.videoSources,
    origin: "recon",
    manual: device.identity === device.key
  };
}
