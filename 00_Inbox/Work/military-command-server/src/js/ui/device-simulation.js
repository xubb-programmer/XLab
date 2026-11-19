import { StationConfigModal } from "./station-config-modal.js";
import { eventBus } from "../core/event-bus.js";
import { t } from "../core/terms.js";

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
  const { artilleryLayer, statsPanel, targetService, scenarioPoints = [], configuredUavs = [] } = options;
  const status = document.getElementById("device-simulation-status");
  if (status) {
    status.hidden = false;
    status.textContent = "正在连接EdgeAgent设备状态通道…";
  }

  let previous = null;
  let revision = 0;
  let refreshRequest = 0;
  let saving = false;
  let currentUavs = configuredUavs;
  let currentFixedPoints = [];
  let currentMountedDevices = [];
  let aircraftSignature;
  const modelPoint = p => ({
    ...p,
    battalion: p.battalion || t("org.battalionDefault", "1区"),
    company: p.company || t("org.companyDefault", "1组"),
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
  const scenarioUavs = configuredUavs.map(uav => ({ ...uav, isScenario: true }));
  let scenarioRevision;
  let savedScenarioUavs = scenarioUavs.map(uav => ({ ...uav }));
  let lastDeviceResult = { uavs: [], firepower: [], mounted: [] };
  let savedScenarioPoints = scenarioFirepower.map(point => ({ ...point }));

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
      const batName = `${b}${t("org.battalion", "区")}`;
      const companies = [];
      for (let c = 1; c <= compCount; c++) {
        companies.push(`${c}${t("org.company", "组")}`);
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

  function addBattalion(battalionName, companies = [`1${t("org.company", "组")}`]) {
    const name = String(battalionName || "").trim();
    if (!name) return false;
    if (orgStructure.some(b => b.name === name)) return false;
    const comps = Array.isArray(companies) && companies.length ? companies : [`1${t("org.company", "组")}`];
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

  async function saveBatchFirepower({ includeUavs = false } = {}) {
    saving = true;
    revision++;
    try {
      const firepowerPoints = scenarioFirepower.map(p => ({
        id: p.id, name: p.name, battalion: p.battalion, company: p.company, deviceId: p.deviceId,
        mountedDeviceId: p.mountedDeviceId || "", unmountedIp: p.unmountedIp,
        longitude: p.longitude, latitude: p.latitude, altitude: p.altitude,
        strikeRange: p.strikeRange, maxTargets: p.maxTargets, ip: p.ip, port: p.port
      }));
      const response = await fetch("/api/scenario/firepower/batch", {
        method: "POST", headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ firepowerPoints, scenarioRevision, ...(includeUavs && { scenarioUavs }) })
      });
      const result = await response.json();
      if (!response.ok || !result.ok) throw new Error(result.error || t("ui.saveFirepowerFail", "保存作业单元配置失败"));
      render({ ...lastDeviceResult, ...result });
      return true;
    } catch (error) {
      render({ ...lastDeviceResult, firepowerPoints: savedScenarioPoints, scenarioUavs: savedScenarioUavs });
      if (status) status.textContent = error.message;
      return false;
    } finally {
      saving = false;
    }
  }

  function findMountedDevice(deviceId) {
    if (!deviceId) return undefined;
    const devices = [...currentMountedDevices, ...currentUavs];
    return devices.find(u => u.id === deviceId)
      ?? devices.find(u => extractDeviceId(u) === deviceId && u.online)
      ?? devices.find(u => extractDeviceId(u) === deviceId);
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
    if (!pt || saving) return false;
    saving = true;
    revision++;
    try {
      const response = await fetch("/api/scenario/firepower/mount", {
        method: "POST", headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ pointId: firepowerId, deviceId, scenarioRevision })
      });
      const result = await response.json();
      if (!response.ok) throw new Error(result.error || "保存挂载失败");
      render(result);
      return true;
    } catch (error) {
      if (status) status.textContent = error.message;
      return false;
    } finally {
      saving = false;
    }
  }

  async function updatePointAffiliation(firepowerId, updates = {}) {
    if (saving) return false;
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
      return saveBatchFirepower();
    }
  }

  async function addFirepowerPoint(data = {}) {
    if (saving) return null;
    const nextIdx = scenarioFirepower.length + 1;
    const randomId = Array.from(crypto.getRandomValues(new Uint8Array(12)), byte => byte.toString(16).padStart(2, "0")).join("");
    const newId = `FIRE-${randomId}`;
    const newPt = {
      id: newId,
      name: data.name || t("roles.firepowerDefaultName", `${nextIdx}号作业单元`, { n: nextIdx }),
      battalion: data.battalion || t("org.battalionDefault", "1区"),
      company: data.company || t("org.companyDefault", "1组"),
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
    return await saveBatchFirepower() ? newPt : null;
  }

  async function autoMountAvailableDevices() {
    if (saving) return 0;
    const availableUavs = currentUavs.filter(u => u.online && !u.duplicateId && !u.mountedTo && !u.isScenario);
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
      if (!await saveBatchFirepower()) return 0;
    }
    return mountedCount;
  }

  async function resetToDefaultAffiliation() {
    orgStructure = [
      { name: "1区", companies: ["1组", "2组"] },
      { name: "2区", companies: ["1组", "2组"] }
    ];
    saveOrgStructure();
    scenarioFirepower.length = 0;
    scenarioFirepower.push(
      {
        id: "FIRE-01",
        name: t("roles.firepowerDefaultName", "1号作业单元", { n: 1 }),
        battalion: "1区",
        company: "1组",
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
        name: t("roles.firepowerDefaultName", "2号作业单元", { n: 2 }),
        battalion: "1区",
        company: "1组",
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
        name: t("roles.firepowerDefaultName", "3号作业单元", { n: 3 }),
        battalion: "1区",
        company: "2组",
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

    // 1. 先用已配置的片区分组结构初始化
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

    // 2. 遍历现有作业单元，归入对应片区分组
    for (const pt of all) {
      const batName = pt.battalion || t("org.battalionDefault", "1区");
      const compName = pt.company || t("org.companyDefault", "1组");

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
      root: t("org.center", "业务调度中心"),
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
    reportSituation: key => request("situation-report", { key }),
    openAffiliation: () => eventBus.emit("firepower:openAffiliationModal"),
    async changeRole(device, role) {
      if (saving) throw new Error("设备配置正在保存，请稍后重试");
      if (device.isScenario) {
        if (role === "uav") {
          if (device.mountedDeviceId) {
            if (!await mountDevice(device.id, "")) throw new Error("解除挂载失败");
            return;
          }
          const idx = scenarioFirepower.findIndex(p => p.id === device.id);
          if (idx >= 0) {
            const pt = scenarioFirepower.splice(idx, 1)[0];
            scenarioUavs.push(pt);
            if (!await saveBatchFirepower({ includeUavs: true })) throw new Error(t("ui.toReconFail", "转换为监测席失败"));
          }
        } else if (role === "firepower") {
          const idx = scenarioUavs.findIndex(p => p.id === device.id);
          if (idx >= 0) {
            const pt = scenarioUavs.splice(idx, 1)[0];
            scenarioFirepower.push(modelPoint(pt));
            if (!await saveBatchFirepower({ includeUavs: true })) throw new Error(t("ui.toFirepowerFail", "转换为作业单元失败"));
          }
        }
        return;
      }
      await save("role", { key: device.id, role });
    }
  };
  const editor = new StationConfigModal(
    viewer,
    async (point, coordinates, name) => {
      if (saving) throw new Error("设备配置正在保存，请稍后重试");
      const key = point.id;
      if (point.isScenario) {
        const recon = scenarioUavs.find(p => p.id === key);
        const scenarioPt = recon || scenarioFirepower.find(p => p.id === key);
        if (!scenarioPt) throw new Error("场景记录已删除，请关闭后刷新");
        saving = true;
        revision++;
        try {
          const response = await fetch("/api/scenario/firepower", {
            method: "POST", headers: { "Content-Type": "application/json" },
            body: JSON.stringify({ ...scenarioPt, ...(coordinates ?? { longitude: null, latitude: null, altitude: null }),
              name, scenarioRevision, kind: recon ? "recon" : "firepower" })
          });
          const result = await response.json();
          if (!response.ok) throw new Error(result.error || "保存场景坐标失败");
          render(result);
        } finally {
          saving = false;
        }
        return;
      }
      return save("coordinates", { key, coordinates, name });
    },
    (device, role) => callbacks.changeRole(device, role),
    {
      getFirepowerPoints: () => scenarioFirepower,
      getMountedPoint: device => currentUavs.find(uav => uav.id === device.id)?.mountedTo ?? null,
      mountDevice
    }
  );

  function render({ uavs = [], firepower = [], mounted = [], aircraft, firepowerPoints, scenarioUavs: nextUavs, scenarioRevision: nextRevision }) {
    lastDeviceResult = { uavs, firepower, mounted, aircraft: aircraft ?? lastDeviceResult.aircraft };
    if (aircraft) {
      const signature = JSON.stringify(aircraft);
      if (signature !== aircraftSignature) {
        aircraftSignature = signature;
        eventBus.emit("aircraft:updated", aircraft);
      }
    }
    if (nextRevision) scenarioRevision = nextRevision;
    if (nextUavs) {
      savedScenarioUavs = nextUavs.map(uav => ({
        ...uav, name: uav.name || uav.callsign || uav.id, isScenario: true
      }));
      scenarioUavs.splice(0, scenarioUavs.length, ...savedScenarioUavs.map(uav => ({ ...uav })));
    }
    if (firepowerPoints) {
      savedScenarioPoints = firepowerPoints.map(modelPoint);
      scenarioFirepower.splice(0, scenarioFirepower.length, ...savedScenarioPoints.map(point => ({ ...point })));
    }
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
    // 后端现在分别返回监测视图和单元挂载视图；同一设备可以同时存在于两者。
    // 监测列表只使用 uavs，避免把 mounted 视图再次拼接造成重复卡片。
    const devices = model(uavs);
    const mountedDevices = model(mounted);
    const mountIds = new Set(scenarioFirepower.map(point => point.mountedDeviceId).filter(Boolean));
    const isMounted = device => mountIds.has(device.deviceId) || mountIds.has(device.id);
    currentMountedDevices = mountedDevices;
    currentUavs = [...scenarioUavs, ...devices];
    currentFixedPoints = model(firepower).filter(device => !isMounted(device));
    const allFirepowerPoints = [...scenarioFirepower, ...currentFixedPoints];

    // 动态依据EdgeAgent转发的在线监测设备计算作业单元在线状态
    for (const pt of allFirepowerPoints) {
      if (!pt.battalion) pt.battalion = t("org.battalionDefault", "1区");
      if (!pt.company) pt.company = t("org.companyDefault", "1组");
      if (pt.mountedDeviceId) {
        const mountedDevice = findMountedDevice(pt.mountedDeviceId);
        pt.online = Boolean(mountedDevice && mountedDevice.online !== false);
        pt.mountedDeviceName = mountedDevice?.name || "";
        if (mountedDevice?.deviceIp && pt.isScenario) {
          if (pt.ip !== mountedDevice.deviceIp) pt.unmountedIp ??= pt.ip;
          pt.ip = mountedDevice.deviceIp;
        }
      } else if (pt.isScenario) {
        pt.online = false;
      }
    }

    // 标记监测设备挂载的作业单元
    for (const u of currentUavs) {
      const uDevId = extractDeviceId(u);
      const mountedPoint = allFirepowerPoints.find(pt => pt.mountedDeviceId && (pt.mountedDeviceId === uDevId || pt.mountedDeviceId === u.id));
      u.mountedTo = mountedPoint ? {
        id: mountedPoint.id,
        name: mountedPoint.name,
        battalion: mountedPoint.battalion || t("org.battalionDefault", "1区"),
        company: mountedPoint.company || t("org.companyDefault", "1组")
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

  async function refresh(schedule = true) {
    const started = revision;
    const requestId = ++refreshRequest;
    try {
      if (saving) return;
      const result = await request("devices");
      if (requestId !== refreshRequest || started !== revision || saving) return;
      render(result);
      const receiveStatus = {
        unconfigured: "未配置设备状态通道",
        invalid: "设备状态通道配置无效",
        dependency_missing: "未安装 MQTT 组件",
        connecting: "正在连接EdgeAgent…",
        subscribed: "已订阅",
        subscription_failed: "EdgeAgent设备状态主题订阅失败",
        connection_error: "EdgeAgent数据连接失败",
        disconnected: "EdgeAgent数据连接已断开"
      };
      if (status) status.textContent = result.receiveError || receiveStatus[result.state] || "等待EdgeAgent设备状态";
    } catch (error) {
      if (requestId === refreshRequest && started === revision && status) status.textContent = error.message;
    } finally {
      if (schedule) setTimeout(refresh, 1000);
    }
  }

  eventBus.on("management:changed", () => void refresh(false));
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
    getScenarioPoints: () => [...scenarioFirepower],
    getScenarioUavs: () => [...scenarioUavs],
    openConfig: (point, role = "firepower") => editor.open(point, false, role),
    changeRole: (point, role) => callbacks.changeRole(point, role),
    mountDevice,
    updatePointAffiliation,
    addFirepowerPoint,
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
