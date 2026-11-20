import { readFile } from "node:fs/promises";
import { createHash, randomUUID } from "node:crypto";
import { edgeDeviceIdPattern } from "./edge-actions.mjs";
import { hasGeographicPosition } from "../src/js/core/coordinates.js";

const defaultEndpoint = { timeoutMs: 6000 };
export const reportIdPattern = /^[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}$/;

export class ReportError extends Error {
  constructor(status, message, details = {}) {
    super(message);
    this.status = status;
    this.details = details;
  }
}

export async function loadReportingConfig(source) {
  try {
    const config = typeof source === "object" && source !== null
      ? source
      : JSON.parse((await readFile(source, "utf8")).replace(/^\uFEFF/, ""));
    return { defaultEndpoint: { ...defaultEndpoint, ...config.defaultEndpoint }, endpoints: config.endpoints || {} };
  } catch {
    return { defaultEndpoint, endpoints: {} };
  }
}

export function readReportRequest(payload) {
  const batchId = payload?.batchId === undefined ? randomUUID() : payload.batchId;
  if (typeof batchId !== "string" || !reportIdPattern.test(batchId)) {
    throw new ReportError(400, "批次编号格式无效");
  }
  const legacyAssignments = Array.isArray(payload?.targetIds)
    ? payload.targetIds.map(targetId => ({ targetId, firepowerId: payload.firepowerId })) : [];
  const assignments = payload?.assignments == null || (Array.isArray(payload.assignments) && !payload.assignments.length)
    ? legacyAssignments : payload.assignments;
  if (!Array.isArray(assignments) || !assignments.length) {
    throw new ReportError(400, "未指定目标分配方案或未选中目标");
  }

  const seen = new Set();
  const targets = new Map((Array.isArray(payload.targets) ? payload.targets : [])
    .filter(target => typeof target?.id === "string").map(target => [target.id, target]));
  const plan = assignments.map(item => {
    const { targetId, firepowerId } = item ?? {};
    if (typeof targetId !== "string" || !targetId.trim() || typeof firepowerId !== "string" || !firepowerId.trim()) {
      throw new ReportError(400, "目标或工位编号无效");
    }
    const key = JSON.stringify([targetId, firepowerId]);
    if (seen.has(key)) throw new ReportError(400, "同一目标在同一工位重复指派");
    seen.add(key);
    const target = targets.get(targetId);
    const entry = [targetId, firepowerId, target?.longitude, target?.latitude, target?.altitude];
    if (target?.generation !== undefined) entry.push(target.generation);
    return entry;
  });
  const requestKey = createHash("sha256").update(JSON.stringify(plan)).digest("hex");
  return { batchId, requestKey, assignments, targets };
}

export function reportCapacity(point) {
  return Math.max(1, Math.min(50, Math.trunc(Number(point.maxTargets)) || 1));
}

export function validateReportAssignments(assignments, firepowerPoints) {
  const points = new Map(firepowerPoints.map(point => [point.id, point]));
  const counts = new Map();
  for (const item of assignments) {
    const point = points.get(item.firepowerId);
    if (!point) throw new ReportError(400, "所选工位不存在，请更新工位配置后重试");
    const deviceId = point.deviceId || "firepower-action-device";
    if (typeof deviceId !== "string" || !edgeDeviceIdPattern.test(deviceId)) {
      throw new ReportError(400, `工位 ${point.name || point.id} 的 EdgeAgent 设备 ID 无效`);
    }
    if (item.deviceId && item.deviceId !== deviceId) {
      throw new ReportError(409, `工位 ${point.name || point.id} 的设备绑定已修改，请核对后重新发起上报`);
    }
    counts.set(point.id, (counts.get(point.id) || 0) + 1);
  }
  const capacityErrors = [];
  for (const [firepowerId, assigned] of counts) {
    const point = points.get(firepowerId);
    const maxTargets = reportCapacity(point);
    if (assigned > maxTargets) {
      capacityErrors.push({ firepowerId, firepowerName: point.name || firepowerId, assigned, maxTargets });
    }
  }
  if (capacityErrors.length) {
    const errors = capacityErrors.map(item => `${item.firepowerName} 指派 ${item.assigned} 个，上限 ${item.maxTargets} 个`);
    throw new ReportError(422, errors.join("。"), { capacityErrors });
  }
  return points;
}

export function prepareReportItems({ request, firepowerPoints, getTarget, config }) {
  const points = validateReportAssignments(request.assignments, firepowerPoints);
  return request.assignments.map(({ targetId, firepowerId }) => {
    const target = getTarget(targetId);
    const submitted = request.targets.get(targetId);
    if (!target || (submitted && (submitted.generation ?? "") !== target.generation)) {
      throw new ReportError(409, `目标 ${targetId} 已删除或重新入库，请刷新后重试`);
    }
    if (!hasGeographicPosition(target)) throw new ReportError(400, `目标 ${targetId} 缺少有效经纬高`);
    const point = points.get(firepowerId);
    const endpoint = config.endpoints[firepowerId] || config.defaultEndpoint;
    const configuredTimeout = Number(endpoint.timeoutMs);
    const timeoutMs = Number.isInteger(configuredTimeout) && configuredTimeout > 0 && configuredTimeout <= 2_147_483_647
      ? configuredTimeout : defaultEndpoint.timeoutMs;
    return {
      targetId, targetNumber: target.targetNumber, name: target.name,
      firepowerId, firepowerName: point.name || firepowerId,
      deviceId: point.deviceId || "firepower-action-device",
      endpoint: {
        address: point.ip || "127.0.0.1",
        port: point.port || 5567
      },
      timeoutMs, mock: Boolean(endpoint.mock),
      params: {
        lon: Number(target.longitude.toFixed(6)),
        lat: Number(target.latitude.toFixed(6)),
        elevate: Math.round(target.altitude)
      }
    };
  });
}
