function text(value) {
  return typeof value === "string" ? value.trim() : "";
}

function cleanReportedName(value) {
  return text(value).replace(/^智图影链[\s_-]*/u, "").trim();
}

export function getPlatformDeviceId(platform) {
  if (!platform) return "";
  if (platform.mountedDeviceId) return text(platform.mountedDeviceId);
  if (platform.deviceId) return text(platform.deviceId);
  if (platform.isScenario) return "";
  if (typeof platform.id === "string" && platform.id.startsWith("[")) {
    try {
      const [deviceId] = JSON.parse(platform.id);
      return text(deviceId);
    } catch {
      return "";
    }
  }
  return "";
}

export function getPlatformName(platform, role) {
  const configuredName = platform?.customName ? text(platform.name) : cleanReportedName(platform?.name);
  if (configuredName) {
    if (role === "firepower") {
      if (configuredName.endsWith("号")) return `${configuredName}火力点`;
      if (configuredName.endsWith("阵位")) return configuredName.replace(/阵位$/, "火力点");
    }
    return configuredName;
  }
  const roleName = role === "firepower" ? "火力点" : "侦察席";
  return `${roleName}-${getPlatformDeviceId(platform) || "未绑定设备"}`;
}

export function getPlatformDisplayName(platform, role) {
  const roleName = role === "firepower" ? "火力点" : "侦察席";
  return `${roleName} · ${getPlatformName(platform, role)}`;
}
