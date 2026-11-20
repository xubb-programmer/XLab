import { t } from "./terms.js";

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
      const firepowerLabel = t("roles.firepower", "作业单元");
      if (configuredName.endsWith("号")) return `${configuredName}${firepowerLabel}`;
      if (configuredName.endsWith("工位") || configuredName.endsWith("点位")) {
        return configuredName.replace(/(?:工位|点位)$/, firepowerLabel);
      }
    }
    return configuredName;
  }
  const roleName = role === "firepower" ? t("roles.firepower", "作业单元") : t("roles.recon", "监测席");
  return `${roleName}-${getPlatformDeviceId(platform) || "未绑定设备"}`;
}

export function getPlatformDisplayName(platform, role) {
  if (role === "firepower") {
    return getPlatformName(platform, role);
  }
  const roleName = t("roles.recon", "侦察席");
  const pName = getPlatformName(platform, role);
  if (pName.startsWith(roleName)) return pName;
  return `${roleName} · ${pName}`;
}
