import { FirepowerConfigModal } from "./firepower-config-modal.js";
import { eventBus } from "../core/event-bus.js";

const api = "/api/device-simulation";

function extractDeviceId(dev) {
  if (!dev) return "";
  if (dev.deviceId) return String(dev.deviceId).trim();
  if (dev.mountedDeviceId) return String(dev.mountedDeviceId).trim();
  if (typeof dev.id === "string" && dev.id.startsWith("[")) {
    try {
      const [parsed] = JSON.parse(dev.id);
      return String(parsed).trim();
    } catch {
      return "";
    }
  }
  return "";
}

export function initDeviceManager(viewer, panel, options = {}) {
  const { artilleryLayer, statsPanel, targetService, scenarioPoints = [] } = options;
  const status = document.getElementById("device-simulation-status");
  if (status) {
    status.hidden = false;
    status.textContent = "正在连接EdgeAgent设备状态通道…";
  }

  let previous = null;
  let revision = 0;
  let saving = false;
  let currentUavs = [];
  let currentFixedPoints = [];
  const modelPoint = p => ({
    ...p,
    battalion: p.battalion || "1营",
    company: p.company || "1连",
    mountedDeviceId: p.mountedDeviceId || "",
    simulation: true,
    device: true,
    isScenario: true,
    strikeRange: p.strikeRange || (p.maxRangeKm ? `${p.maxRangeKm} km` : "40 km"),
    maxTargets: p.maxTargets ?? 1,
    ip: p.ip || "127.0.0.1",
    port: p.port || 5567
  });
  const scenarioFirepower = scenarioPoints.map(modelPoint);
  const scenarioUavs = [];
  let lastDeviceResult = { uavs: [], firepower: [] };

  const ORG_STORAGE_KEY = "tactical_org_structure";

  function loadOrgStructure() {
    try {
      const raw = localStorage.getItem(ORG_STORAGE_KEY);
      if (raw) {
        const parsed = JSON.parse(raw);
        if (Array.isArray(parsed) && parsed.length > 0) {
          return parsed;
        }
      }
    } catch (_) {}
    return [
      { name: "1营", companies: ["1连", "2连"] },
      { name: "2营", companies: ["1连", "2连"] }
    ];
  }

  function saveOrgStructure() {
    try {
      localStorage.setItem(ORG_STORAGE_KEY, JSON.stringify(orgStructure));
    } catch (_) {}
  }

  let orgStructure = loadOrgStructure();

  function setOrgScale(battalionCount, companyCountPerBattalion) {
    const batCount = Math.max(1, Math.min(10, parseInt(battalionCount, 10) || 2));
    const compCount = Math.max(1, Math.min(10, parseInt(companyCountPerBattalion, 10) || 2));

    const newStructure = [];
    for (let b = 1; b <= batCount; b++) {
      const batName = `${b}营`;
      const companies = [];
      for (let c = 1; c <= compCount; c++) {
        companies.push(`${c}连`);
      }
      newStructure.push({ name: batName, companies });
    }

    orgStructure = newStructure;
    saveOrgStructure();

    const validBats = new Set(newStructure.map(b => b.name));
    for (const pt of scenarioFirepower) {
      if (!validBats.has(pt.battalion)) {
        pt.battalion = newStructure[0].name;
        pt.company = newStructure[0].companies[0];
      } else {
        const bObj = newStructure.find(b => b.name === pt.battalion);
        if (bObj && !bObj.companies.includes(pt.company)) {
          pt.company = bObj.companies[0];
        }
      }
    }

    void saveBatchFirepower();
    previous = null;
    render(lastDeviceResult);
    eventBus.emit("firepower:affiliationChanged", getAffiliationTree());
    return getAffiliationTree();
  }

  function addBattalion(battalionName, companies = ["1连"]) {
    const name = String(battalionName || "").trim();
    if (!name) return false;
    if (orgStructure.some(b => b.name === name)) return false;
    const comps = Array.isArray(companies) && companies.length ? companies : ["1连"];
    orgStructure.push({ name, companies: comps });
    saveOrgStructure();
    eventBus.emit("firepower:affiliationChanged", getAffiliationTree());
    return true;
  }

  function removeBattalion(battalionName) {
    const idx = orgStructure.findIndex(b => b.name === battalionName);
    if (idx < 0) return false;
    orgStructure.splice(idx, 1);
    if (orgStructure.length === 0) {
      orgStructure.push({ name: "1营", companies: ["1连"] });
    }
    saveOrgStructure();
    const fallbackBat = orgStructure[0].name;
    const fallbackComp = orgStructure[0].companies[0];
    for (const pt of scenarioFirepower) {
      if (pt.battalion === battalionName) {
        pt.battalion = fallbackBat;
        pt.company = fallbackComp;
      }
    }
    void saveBatchFirepower();
    previous = null;
    render(lastDeviceResult);
    eventBus.emit("firepower:affiliationChanged", getAffiliationTree());
    return true;
  }

  function addCompany(battalionName, companyName) {
    const bName = String(battalionName || "").trim();
    const cName = String(companyName || "").trim();
    if (!bName || !cName) return false;
    let b = orgStructure.find(item => item.name === bName);
    if (!b) {
      b = { name: bName, companies: [] };
      orgStructure.push(b);
    }
    if (!b.companies.includes(cName)) {
      b.companies.push(cName);
      saveOrgStructure();
      eventBus.emit("firepower:affiliationChanged", getAffiliationTree());
      return true;
    }
    return false;
  }

  function removeCompany(battalionName, companyName) {
    const b = orgStructure.find(item => item.name === battalionName);
    if (!b) return false;
    const cIdx = b.companies.indexOf(companyName);
    if (cIdx >= 0) {
      b.companies.splice(cIdx, 1);
      if (b.companies.length === 0) {
        b.companies.push("1连");
      }
      saveOrgStructure();
      for (const pt of scenarioFirepower) {
        if (pt.battalion === battalionName && pt.company === companyName) {
          pt.company = b.companies[0];
        }
      }
      void saveBatchFirepower();
      previous = null;
      render(lastDeviceResult);
      eventBus.emit("firepower:affiliationChanged", getAffiliationTree());
      return true;
    }
    return false;
  }

  async function saveBatchFirepower() {
    try {
      const firepowerPoints = scenarioFirepower.map(p => ({
        id: p.id, name: p.name, battalion: p.battalion, company: p.company,
        mountedDeviceId: p.mountedDeviceId || "", unmountedIp: p.unmountedIp,
        longitude: p.longitude, latitude: p.latitude, altitude: p.altitude,
        strikeRange: p.strikeRange, maxTargets: p.maxTargets, ip: p.ip, port: p.port
      }));
      const response = await fetch("/api/scenario/firepower/batch", {
        method: "POST", headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ firepowerPoints })
      });
      const result = await response.json();
      return response.ok && result.ok;
    } catch (error) {
      console.warn("[Scenario] 批量保存阵位隶属失败:", error);
      return false;
    }
  }

  function findMountedDevice(deviceId) {
    if (!deviceId) return undefined;
    return currentUavs.find(u => u.id === deviceId)
      ?? currentUavs.find(u => extractDeviceId(u) === deviceId && u.online)
      ?? currentUavs.find(u => extractDeviceId(u) === deviceId);
  }

  function getMountedDeviceIp(deviceId) {
    return findMountedDevice(deviceId)?.deviceIp || "";
  }

  function setPointMount(point, deviceId) {
    const nextId = String(deviceId || "").trim();
    if (point.mountedDeviceId !== nextId) {
      if (point.mountedDeviceId) {
        const previousDeviceIp = getMountedDeviceIp(point.mountedDeviceId);
        point.ip = point.unmountedIp ?? (point.ip === previousDeviceIp ? "127.0.0.1" : point.ip);
        delete point.unmountedIp;
      }
      point.mountedDeviceId = nextId;
      if (nextId) point.unmountedIp = point.ip;
    }
    const deviceIp = getMountedDeviceIp(nextId);
    if (deviceIp) point.ip = deviceIp;
  }

  function assignPointMount(point, deviceId) {
    const cleanId = String(deviceId || "").trim();
    const device = findMountedDevice(cleanId);
    for (const other of scenarioFirepower) {
      if (other === point || !other.mountedDeviceId) continue;
      if (other.mountedDeviceId === cleanId || (device && findMountedDevice(other.mountedDeviceId) === device)) {
        setPointMount(other, "");
      }
    }
    setPointMount(point, cleanId);
  }

  async function mountDevice(firepowerId, deviceId) {
    const pt = scenarioFirepower.find(p => p.id === firepowerId);
    if (!pt) return;
    assignPointMount(pt, deviceId);
    await saveBatchFirepower();
    previous = null;
    render(lastDeviceResult);
    eventBus.emit("firepower:affiliationChanged", getAffiliationTree());
  }

  async function updatePointAffiliation(firepowerId, updates = {}) {
    const pt = scenarioFirepower.find(p => p.id === firepowerId);
    if (pt) {
      if (updates.battalion !== undefined) pt.battalion = updates.battalion.trim();
      if (updates.company !== undefined) pt.company = updates.company.trim();
      if (updates.name !== undefined) pt.name = updates.name.trim();
      if (updates.strikeRange !== undefined) pt.strikeRange = updates.strikeRange;
      if (updates.maxTargets !== undefined) pt.maxTargets = updates.maxTargets;
      if (updates.ip !== undefined && !pt.mountedDeviceId) pt.ip = updates.ip;
      if (updates.mountedDeviceId !== undefined) assignPointMount(pt, updates.mountedDeviceId);
      if (updates.ip !== undefined && pt.mountedDeviceId && !getMountedDeviceIp(pt.mountedDeviceId)) pt.ip = updates.ip;
      if (updates.port !== undefined) pt.port = updates.port;
      if (updates.longitude !== undefined) pt.longitude = updates.longitude;
      if (updates.latitude !== undefined) pt.latitude = updates.latitude;
      if (updates.altitude !== undefined) pt.altitude = updates.altitude;
      await saveBatchFirepower();
      previous = null;
      render(lastDeviceResult);
      eventBus.emit("firepower:affiliationChanged", getAffiliationTree());
    }
  }

  async function addFirepowerPoint(data = {}) {
    const nextIdx = scenarioFirepower.length + 1;
    const randomId = Array.from(crypto.getRandomValues(new Uint8Array(12)), byte => byte.toString(16).padStart(2, "0")).join("");
    const newId = `FIRE-${randomId}`;
    const newPt = {
      id: newId,
      name: data.name || `${nextIdx}号火力点`,
      battalion: data.battalion || "1营",
      company: data.company || "1连",
      mountedDeviceId: "",
      longitude: Number(data.longitude ?? 118.500000),
      latitude: Number(data.latitude ?? 24.500000),
      altitude: Number(data.altitude ?? 0),
      strikeRange: data.strikeRange || "20 km",
      maxTargets: data.maxTargets ?? 1,
      ip: data.ip || "127.0.0.1",
      port: data.port || 5567,
      simulation: true,
      device: true,
      isScenario: true
    };
    assignPointMount(newPt, data.mountedDeviceId);
    scenarioFirepower.push(newPt);
    await saveBatchFirepower();
    previous = null;
    render(lastDeviceResult);
    eventBus.emit("firepower:affiliationChanged", getAffiliationTree());
    return newPt;
  }

  async function removeFirepowerPoint(firepowerId) {
    let removed = false;
    const idx = scenarioFirepower.findIndex(p => p.id === firepowerId);
    if (idx >= 0) {
      scenarioFirepower.splice(idx, 1);
      removed = true;
    }
    const fixedIdx = currentFixedPoints.findIndex(p => p.id === firepowerId);
    if (fixedIdx >= 0) {
      currentFixedPoints.splice(fixedIdx, 1);
      removed = true;
    }
    if (removed) {
      await saveBatchFirepower();
      previous = null;
      render(lastDeviceResult);
      eventBus.emit("firepower:affiliationChanged", getAffiliationTree());
      return true;
    }
    return false;
  }

  async function autoMountAvailableDevices() {
    const availableUavs = currentUavs.filter(u => u.online && !u.mountedTo);
    let uavIdx = 0;
    let mountedCount = 0;
    for (const pt of scenarioFirepower) {
      if (!pt.mountedDeviceId && uavIdx < availableUavs.length) {
        const u = availableUavs[uavIdx++];
        assignPointMount(pt, extractDeviceId(u) || u.id);
        mountedCount++;
      }
    }
    if (mountedCount > 0) {
      await saveBatchFirepower();
      previous = null;
      render(lastDeviceResult);
      eventBus.emit("firepower:affiliationChanged", getAffiliationTree());
    }
    return mountedCount;
  }

  async function resetToDefaultAffiliation() {
    orgStructure = [
      { name: "1营", companies: ["1连", "2连"] },
      { name: "2营", companies: ["1连", "2连"] }
    ];
    saveOrgStructure();
    scenarioFirepower.length = 0;
    scenarioFirepower.push(
      {
        id: "FIRE-01",
        name: "1号火力点",
        battalion: "1营",
        company: "1连",
        mountedDeviceId: "",
        longitude: 118.570452,
        latitude: 24.573654,
        altitude: 0,
        strikeRange: "12 km",
        maxTargets: 1,
        ip: "127.0.0.1",
        port: 5567,
        simulation: true,
        device: true,
        isScenario: true
      },
      {
        id: "FIRE-02",
        name: "2号火力点",
        battalion: "1营",
        company: "1连",
        mountedDeviceId: "",
        longitude: 118.432154,
        latitude: 24.617528,
        altitude: 0,
        strikeRange: "40 km",
        maxTargets: 1,
        ip: "192.168.1.102",
        port: 5567,
        simulation: true,
        device: true,
        isScenario: true
      },
      {
        id: "FIRE-03",
        name: "3号火力点",
        battalion: "1营",
        company: "2连",
        mountedDeviceId: "",
        longitude: 118.448499,
        latitude: 24.507795,
        altitude: 0,
        strikeRange: "18 km",
        maxTargets: 1,
        ip: "192.168.1.103",
        port: 5567,
        simulation: true,
        device: true,
        isScenario: true
      }
    );
    assignPointMount(scenarioFirepower[0], "ZTYL_02");
    await saveBatchFirepower();
    previous = null;
    render(lastDeviceResult);
    eventBus.emit("firepower:affiliationChanged", getAffiliationTree());
  }

  function getAffiliationTree() {
    const all = [...scenarioFirepower, ...currentFixedPoints];
    const battalionsMap = new Map();

    // 1. 先用已配置的营连结构初始化
    for (const b of orgStructure) {
      const batObj = {
        name: b.name,
        companies: new Map(),
        totalPoints: 0,
        onlinePoints: 0,
        mountedPoints: 0
      };
      for (const c of b.companies) {
        batObj.companies.set(c, {
          name: c,
          battalion: b.name,
          points: [],
          totalPoints: 0,
          onlinePoints: 0
        });
      }
      battalionsMap.set(b.name, batObj);
    }

    // 2. 遍历现有火力点，归入对应营连
    for (const pt of all) {
      const batName = pt.battalion || "1营";
      const compName = pt.company || "1连";

      if (!battalionsMap.has(batName)) {
        battalionsMap.set(batName, {
          name: batName,
          companies: new Map(),
          totalPoints: 0,
          onlinePoints: 0,
          mountedPoints: 0
        });
        orgStructure.push({ name: batName, companies: [compName] });
        saveOrgStructure();
      }
      const bat = battalionsMap.get(batName);
      bat.totalPoints++;
      if (pt.online) bat.onlinePoints++;
      if (pt.mountedDeviceId) bat.mountedPoints++;

      if (!bat.companies.has(compName)) {
        bat.companies.set(compName, {
          name: compName,
          battalion: batName,
          points: [],
          totalPoints: 0,
          onlinePoints: 0
        });
        const found = orgStructure.find(b => b.name === batName);
        if (found && !found.companies.includes(compName)) {
          found.companies.push(compName);
          saveOrgStructure();
        }
      }
      const comp = bat.companies.get(compName);
      comp.totalPoints++;
      if (pt.online) comp.onlinePoints++;
      comp.points.push(pt);
    }

    const battalions = Array.from(battalionsMap.values()).map(bat => ({
      name: bat.name,
      totalPoints: bat.totalPoints,
      onlinePoints: bat.onlinePoints,
      mountedPoints: bat.mountedPoints,
      companies: Array.from(bat.companies.values())
    }));

    return {
      root: "战术指挥所",
      totalPoints: all.length,
      onlinePoints: all.filter(p => p.online).length,
      mountedPoints: all.filter(p => Boolean(p.mountedDeviceId)).length,
      battalions,
      allPoints: all,
      orgStructure: JSON.parse(JSON.stringify(orgStructure))
    };
  }

  function getOnlineReconDevices() {
    return currentUavs.map(u => ({
      id: u.id,
      deviceId: extractDeviceId(u) || u.id,
      name: u.customName || u.name,
      online: Boolean(u.online),
      deviceIp: u.deviceIp || u.ip || "127.0.0.1",
      mountedTo: u.mountedTo || null
    }));
  }

  const callbacks = {
    edit: (device, role) => editor.open(device, false, role),
    mount: (firepowerId, deviceId) => mountDevice(firepowerId, deviceId),
    getFirepowerPoints: () => [...scenarioFirepower, ...currentFixedPoints],
    openAffiliation: () => eventBus.emit("firepower:openAffiliationModal"),
    async changeRole(device, role) {
      if (saving) return;
      if (device.isScenario) {
        if (role === "uav") {
          const idx = scenarioFirepower.findIndex(p => p.id === device.id);
          if (idx >= 0) {
            const pt = scenarioFirepower.splice(idx, 1)[0];
            scenarioUavs.push({
              ...pt,
              videoSources: pt.videoSources || 2,
              videoChannels: pt.videoChannels && pt.videoChannels.length ? pt.videoChannels : [
                { id: `${pt.id}-c1`, label: `${pt.name} 视频1`, available: true },
                { id: `${pt.id}-c2`, label: `${pt.name} 视频2`, available: true }
              ]
            });
            previous = null;
            render(lastDeviceResult);
            if (status) status.textContent = `${device.name || device.id} 已转为侦察席`;
          }
        } else if (role === "firepower") {
          const idx = scenarioUavs.findIndex(p => p.id === device.id);
          if (idx >= 0) {
            const pt = scenarioUavs.splice(idx, 1)[0];
            scenarioFirepower.push(pt);
            previous = null;
            render(lastDeviceResult);
            if (status) status.textContent = `${device.name || device.id} 已转为火力点`;
          }
        }
        return;
      }
      try {
        await save("role", { key: device.id, role });
      } catch (error) {
        if (status) status.textContent = error.message;
      }
    }
  };
  const editor = new FirepowerConfigModal(
    viewer,
    null,
    async (key, coordinates, name) => {
      const scenarioPt = [...scenarioFirepower, ...scenarioUavs].find(p => p.id === key);
      if (scenarioPt) {
        const deviceIp = scenarioPt.isScenario ? getMountedDeviceIp(scenarioPt.mountedDeviceId) : "";
        Object.assign(scenarioPt, coordinates, { name });
        if (deviceIp) scenarioPt.ip = deviceIp;
        try {
          await fetch("/api/scenario/firepower", {
            method: "POST", headers: { "Content-Type": "application/json" },
            body: JSON.stringify(scenarioPt)
          });
        } catch (error) {
          console.warn("[Scenario] 保存场景阵位坐标失败:", error);
        }
        previous = null;
        render(lastDeviceResult);
        return;
      }
      return save("coordinates", { key, coordinates, name });
    },
    (device, role) => callbacks.changeRole(device, role)
  );

  function render({ uavs = [], firepower = [] }) {
    lastDeviceResult = { uavs, firepower };
    const model = devices => devices.map(({ key, name, customName, deviceId, online, coordinates, videoSources, videoChannels, canAssign, duplicateId, staleSnapshot, deviceIp }) => ({
      id: key, name, customName, deviceId, online, ...coordinates, videoSources,
      videoChannels: Array.isArray(videoChannels) ? videoChannels : [],
      canAssign, duplicateId, staleSnapshot, deviceIp,
      strikeRange: coordinates?.strikeRange || "40 km",
      maxTargets: coordinates?.maxTargets ?? 1,
      ip: coordinates?.ip || deviceIp || "127.0.0.1",
      port: coordinates?.port || 5567,
      simulation: true, device: true
    }));
    currentUavs = [...scenarioUavs, ...model(uavs)];
    currentFixedPoints = model(firepower);
    const allFirepowerPoints = [...scenarioFirepower, ...currentFixedPoints];

    // 动态依据EdgeAgent转发的在线侦察设备计算火力点在线状态
    for (const pt of allFirepowerPoints) {
      if (!pt.battalion) pt.battalion = "1营";
      if (!pt.company) pt.company = "1连";
      if (pt.mountedDeviceId) {
        const mountedDevice = findMountedDevice(pt.mountedDeviceId);
        pt.online = Boolean(mountedDevice && mountedDevice.online !== false);
        if (mountedDevice?.deviceIp && pt.isScenario) {
          if (pt.ip !== mountedDevice.deviceIp) pt.unmountedIp ??= pt.ip;
          pt.ip = mountedDevice.deviceIp;
        }
      } else if (pt.isScenario) {
        pt.online = false;
      }
    }

    // 标记侦察设备挂载的火力点
    for (const u of currentUavs) {
      const uDevId = extractDeviceId(u);
      const mountedPoint = allFirepowerPoints.find(pt => pt.mountedDeviceId && (pt.mountedDeviceId === uDevId || pt.mountedDeviceId === u.id));
      u.mountedTo = mountedPoint ? {
        id: mountedPoint.id,
        name: mountedPoint.name,
        battalion: mountedPoint.battalion || "1营",
        company: mountedPoint.company || "1连"
      } : null;
    }

    const platforms = [currentUavs, allFirepowerPoints];
    const next = JSON.stringify(platforms);
    if (next === previous) return;
    previous = next;

    panel.update(currentUavs, allFirepowerPoints, callbacks);
    if (artilleryLayer) {
      artilleryLayer.updatePlatforms(currentUavs, allFirepowerPoints);
    }
    if (statsPanel && targetService) {
      statsPanel.update({
        uavs: currentUavs,
        points: allFirepowerPoints,
        targets: targetService.getAllTargets(),
        source: "device"
      });
    }
    eventBus.emit("devices:updated", { uavs: currentUavs, firepower: allFirepowerPoints });
    eventBus.emit("firepower:affiliationChanged", getAffiliationTree());
    viewer.requestRender();
  }

  async function request(path, payload) {
    const response = await fetch(`${api}/${path}`, {
      cache: "no-store", signal: AbortSignal.timeout(5000),
      ...(payload ? { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(payload) } : {})
    });
    const result = await response.json();
    if (!response.ok) throw new Error(result.error || "无法读取设备状态");
    return result;
  }

  async function save(path, payload) {
    saving = true;
    revision++;
    try {
      render(await request(path, payload));
      if (status) status.textContent = "设备配置已保存";
    } finally {
      saving = false;
    }
  }

  async function refresh() {
    const started = revision;
    try {
      if (saving) return;
      const result = await request("devices");
      if (started !== revision || saving) return;
      render(result);
      const receiveStatus = {
        connecting: "正在连接EdgeAgent…",
        subscribed: `设备状态通道已订阅 · ${result.source}`,
        subscription_failed: "EdgeAgent设备状态主题订阅失败",
        connection_error: "EdgeAgent数据连接失败",
        disconnected: "EdgeAgent数据连接已断开"
      };
      if (status) status.textContent = result.receiveError || receiveStatus[result.state] || "等待EdgeAgent设备状态";
    } catch (error) {
      if (started === revision && status) status.textContent = error.message;
    } finally {
      setTimeout(refresh, 1000);
    }
  }

  panel.update(scenarioUavs, scenarioFirepower, callbacks);
  if (statsPanel && targetService) {
    statsPanel.update({
      uavs: scenarioUavs,
      points: scenarioFirepower,
      targets: targetService.getAllTargets(),
      source: "device"
    });
  }
  void refresh();

  return {
    getUavs: () => currentUavs,
    getFixedPoints: () => [...scenarioFirepower, ...currentFixedPoints],
    openConfig: (point, role = "firepower") => editor.open(point, false, role),
    changeRole: (point, role) => callbacks.changeRole(point, role),
    mountDevice,
    updatePointAffiliation,
    addFirepowerPoint,
    removeFirepowerPoint,
    autoMountAvailableDevices,
    resetToDefaultAffiliation,
    getAffiliationTree,
    getOnlineReconDevices,
    getMountedDeviceIp,
    saveBatchFirepower,
    setOrgScale,
    addBattalion,
    removeBattalion,
    addCompany,
    removeCompany,
    getOrgStructure: () => JSON.parse(JSON.stringify(orgStructure))
  };
}

export const startDeviceSimulation = initDeviceManager;
