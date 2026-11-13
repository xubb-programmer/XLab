export function mountedDeviceId(value) {
  if (typeof value !== "string") return "";
  const id = value.trim();
  if (!id.startsWith("[")) return id;
  try {
    const [deviceId] = JSON.parse(id);
    return typeof deviceId === "string" ? deviceId.trim() : "";
  } catch {
    return "";
  }
}

export function mountedDeviceIds(points) {
  return new Set(points.map(point => mountedDeviceId(point?.mountedDeviceId)).filter(Boolean));
}

export function validateMounts(points, standaloneIds) {
  const seen = new Set();
  for (const point of points) {
    const id = mountedDeviceId(point?.mountedDeviceId);
    if (!id) continue;
    if (seen.has(id)) throw Object.assign(new Error(`设备 ${id} 已挂载到另一个火力点`), { status: 409 });
    if (standaloneIds.has(id)) throw Object.assign(new Error(`设备 ${id} 已设为独立火力点，请先转回侦察席`), { status: 409 });
    seen.add(id);
  }
}
