// 无人机视频、位置与地图目标展示入口。
import { eventBus } from "./core/event-bus.js";
import { hasGeographicPosition } from "./core/coordinates.js";
import { CesiumViewerManager } from "./core/viewer.js";
import { targetService } from "./engine/target-service.js";
import { ArtilleryLayer } from "./layers/artillery-layer.js";
import { TargetLayer } from "./layers/target-layer.js";
import { GuidanceLayer } from "./layers/guidance-layer.js";
import { computeMultiAllocationPlan } from "./engine/allocation-strategy.js";
import { CameraHud } from "./ui/camera-hud.js";
import { ArtilleryPanel } from "./ui/artillery-panel.js";
import { TargetPanel } from "./ui/target-panel.js";
import { StatsPanel } from "./ui/stats-panel.js";
import { FirepowerConfigModal } from "./ui/firepower-config-modal.js";
import { TargetReportModal } from "./ui/target-report-modal.js";
import { initDeviceManager } from "./ui/device-simulation.js";
import { ReconVideoMatrixModal } from "./ui/recon-video-matrix-modal.js";
import { FirepowerAffiliationModal } from "./ui/firepower-affiliation-modal.js";
import { initDataManagement } from "./ui/data-management.js";

const targetFeedLabels = {
  unconfigured: "未配置",
  invalid: "配置无效",
  dependency_missing: "组件未安装",
  connecting: "正在连接…",
  subscribed: "已订阅",
  subscription_failed: "订阅失败",
  connection_error: "连接失败",
  disconnected: "已断开",
  unavailable: "读取失败"
};

let viewerManager = null;
let deviceManager = null;
let targetPanel = null;
let reconVideoMatrixModal = null;
let firepowerAffiliationModal = null;
let closeCameraWindow = null;
let activeMainPage = "tactical";
let activeMainPageOptions = {};

function switchMainPage(pageName, options = {}) {
  activeMainPage = pageName;
  activeMainPageOptions = options;

  const navBtnTactical = document.getElementById("nav-btn-tactical");
  const navBtnVideoMatrix = document.getElementById("nav-btn-video-matrix");
  const navBtnBatteryConfig = document.getElementById("nav-btn-battery-config");
  const navBtnTools = document.getElementById("nav-btn-tools");
  const pageTactical = document.getElementById("page-tactical");
  const pageVideo = document.getElementById("page-video-matrix");
  const pageFirepower = document.getElementById("page-firepower-affiliation");
  const pageTools = document.getElementById("page-tools");
  const pageDataManagement = document.getElementById("page-data-management");

  navBtnTactical?.classList.toggle("active", pageName === "tactical");
  navBtnVideoMatrix?.classList.toggle("active", pageName === "video-matrix");
  navBtnBatteryConfig?.classList.toggle("active", pageName === "firepower");
  navBtnTools?.classList.toggle("active", pageName === "tools" || pageName === "data-management");

  for (const [page, visible] of [
    [pageTactical, pageName === "tactical"],
    [pageVideo, pageName === "video-matrix"],
    [pageFirepower, pageName === "firepower"],
    [pageTools, pageName === "tools"],
    [pageDataManagement, pageName === "data-management"]
  ]) {
    if (!page) continue;
    page.hidden = !visible;
    page.style.display = visible ? "flex" : "none";
  }

  if (pageName !== "tactical") closeCameraWindow?.();
  if (pageName !== "video-matrix") reconVideoMatrixModal?.close();

  if (pageName === "tactical") {
    viewerManager?.viewer?.resize();
    viewerManager?.requestRender();
  } else if (pageName === "video-matrix") {
    reconVideoMatrixModal?.open(options?.preferredSource);
  } else if (pageName === "firepower") {
    firepowerAffiliationModal?.open(options?.keepDrawer);
    if (options?.openNew) firepowerAffiliationModal?.openDrawerForNew();
  } else if (pageName === "data-management") {
    document.getElementById("management-title")?.focus();
    eventBus.emit("management:open");
  }
}

function bindMainPageNavigation() {
  document.getElementById("nav-btn-tactical")?.addEventListener("click", () => switchMainPage("tactical"));
  document.getElementById("nav-btn-video-matrix")?.addEventListener("click", () => switchMainPage("video-matrix"));
  document.getElementById("nav-btn-battery-config")?.addEventListener("click", () => switchMainPage("firepower"));
  document.getElementById("nav-btn-tools")?.addEventListener("click", () => switchMainPage("tools"));
  const dataManagementButton = document.getElementById("btn-open-data-management");
  dataManagementButton?.addEventListener("click", () => switchMainPage("data-management"));
  document.getElementById("btn-data-management-back")?.addEventListener("click", () => {
    switchMainPage("tools");
    dataManagementButton?.focus();
  });
  document.getElementById("btn-uav-recon-video-matrix")?.addEventListener("click", () => switchMainPage("video-matrix"));
  document.getElementById("btn-tactical-add-firepower")?.addEventListener("click", () =>
    switchMainPage("firepower", { keepDrawer: true, openNew: true }));

  eventBus.on("page:switch", (pageName, options) => switchMainPage(pageName, options));
  eventBus.on("firepower:openAffiliationModal", options => switchMainPage("firepower", options));
  eventBus.on("recon-video:open-matrix", preferredSource =>
    switchMainPage("video-matrix", { preferredSource }));
}

async function readTargets(onUpdate) {
  let snapshot;
  try {
    const response = await fetch("/api/targets", {
      cache: "no-store", signal: AbortSignal.timeout(5000)
    });
    if (!response.ok) throw new Error("Target feed unavailable");
    snapshot = await response.json();
    if (!Array.isArray(snapshot.targets)) throw new Error("Invalid target feed");
  } catch {
    snapshot = { state: "unavailable" };
  }
  onUpdate(snapshot);
  setTimeout(() => readTargets(onUpdate), 1000);
}

function readPlatforms(records, defaultName) {
  const seen = new Set();
  return (Array.isArray(records) ? records : []).flatMap(record => {
    if (typeof record?.id !== "string" || !/^[a-zA-Z0-9][a-zA-Z0-9_-]{0,63}$/.test(record.id) || seen.has(record.id)) return [];
    seen.add(record.id);
    const name = typeof record.name === "string" ? record.name.trim() : "";
    const callsign = typeof record.callsign === "string" ? record.callsign.trim() : "";
    return [{ ...record, name: name || callsign || defaultName, callsign }];
  });
}

async function bootstrap() {


  // 1. 加载初始视角与平台配置
  const response = await fetch("/public/scenarios/scenario-default.json");
  if (!response.ok) {
    throw new Error("无法加载场景基准数据: scenario-default.json");
  }
  const scenarioData = await response.json();

  // 2. 初始化 Cesium.js 三维地球管理器
  viewerManager = new CesiumViewerManager("cesium-container");
  if (targetPanel) targetPanel.viewerManager = viewerManager;
  const mapButton = document.getElementById("btn-map-mode");
  const mapStatus = document.getElementById("map-mode-status");

  // 纯图标定义：离线底图（地球带斜杠）与在线底图（完整地球）
  const MAP_MODE_ICONS = {
    offline: `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><circle cx="12" cy="12" r="9.5"></circle><line x1="2.5" y1="12" x2="21.5" y2="12"></line><path d="M12 2.5a14 14 0 0 1 3.8 9.5 14 14 0 0 1-3.8 9.5 14 14 0 0 1-3.8-9.5 14 14 0 0 1 3.8-9.5z"></path><line x1="3.5" y1="3.5" x2="20.5" y2="20.5" stroke="currentColor" stroke-width="2.2"></line></svg>`,
    online: `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><circle cx="12" cy="12" r="9.5"></circle><line x1="2.5" y1="12" x2="21.5" y2="12"></line><path d="M12 2.5a14 14 0 0 1 3.8 9.5 14 14 0 0 1-3.8 9.5 14 14 0 0 1-3.8-9.5 14 14 0 0 1 3.8-9.5z"></path></svg>`
  };

  let mapMode;
  async function switchMapMode(mode) {
    mapMode = mode;
    const offline = mode === "offline";
    mapButton.classList.add("is-loading");
    mapButton.classList.toggle("is-offline", offline);
    mapButton.classList.toggle("is-online", !offline);
    mapButton.classList.toggle("active", !offline);
    mapButton.innerHTML = offline ? MAP_MODE_ICONS.offline : MAP_MODE_ICONS.online;

    const loadingTip = offline ? "当前：离线底图加载中…（点击切换在线）" : "当前：在线底图加载中…（点击切换离线）";
    mapButton.title = loadingTip;
    mapButton.setAttribute("aria-label", loadingTip);

    const result = await viewerManager.setMapMode(mode);
    mapButton.classList.remove("is-loading");
    if (result) {
      const activeTip = offline
        ? `当前：${result.label}（点击切换为在线底图）`
        : `当前：${result.label}（点击切换为离线底图）`;
      const fullTip = result.credits?.length ? `${activeTip}\n数据源：${result.credits.join(" / ")}` : activeTip;
      mapButton.title = fullTip;
      mapButton.setAttribute("aria-label", activeTip);
      if (mapStatus) {
        mapStatus.textContent = result.label;
        mapStatus.title = result.credits.join("\n");
      }
    }
  }
  mapButton.addEventListener("click", () => switchMapMode(mapMode === "offline" ? "online" : "offline"));
  void switchMapMode(scenarioData.mapMode === "online" ? "online" : "offline");
  window.__viewerManager = viewerManager;
  window.__targetService = targetService;
  window.__eventBus = eventBus;

  // 初始化地图区域框选交互
  viewerManager.initBoxSelection(() => targetService.getAllTargets());
  const btnBoxSelect = document.getElementById("btn-box-select");
  btnBoxSelect?.addEventListener("click", () => {
    viewerManager.setBoxSelectMode(!viewerManager.isBoxSelectMode);
  });

  for (const button of document.querySelectorAll(".sidebar-toggle")) {
    const sidebar = document.getElementById(button.getAttribute("aria-controls"));
    button.addEventListener("click", () => {
      const collapsed = !sidebar.hidden;
      sidebar.hidden = collapsed;
      button.setAttribute("aria-expanded", String(!collapsed));
      const label = `${collapsed ? "展开" : "收起"}${sidebar.getAttribute("aria-label")}侧栏`;
      button.setAttribute("aria-label", label);
      button.title = label;
      viewerManager.viewer.resize();
      viewerManager.requestRender();
    });
  }

  const uavs = readPlatforms(scenarioData.uavs ?? [scenarioData.uav], "无人机");
  const points = readPlatforms(scenarioData.firepowerPoints ?? [scenarioData.firepowerPoint], "地面火力点");

  // 4. 初始化三维场景图层渲染器
  const artilleryLayer = new ArtilleryLayer(viewerManager);
  const targetLayer = new TargetLayer(viewerManager);
  const guidanceLayer = new GuidanceLayer(viewerManager);
  window.__guidanceLayer = guidanceLayer;

  // 5. 初始化无人机视频窗口
  const cameraHud = new CameraHud();
  const artilleryPanel = new ArtilleryPanel(cameraHud, viewerManager);
  closeCameraWindow = () => {
    if (!cameraHud.activeUav) return;
    cameraHud.toggle(cameraHud.activeUav);
    artilleryPanel.updateVideoButtons();
  };

  // 6. 初始化 UI 交互控制台
  const statsPanel = new StatsPanel();
  // 6.0 初始化在线设备发现与角色台账 (EdgeAgent转发状态 + 在线无人机与地面火力阵位分类)
  deviceManager = initDeviceManager(viewerManager, artilleryPanel, {
    artilleryLayer,
    statsPanel,
    targetService,
    scenarioPoints: points
  });
  if (activeMainPage === "data-management") eventBus.emit("management:open");

  // 6.1 初始化地面火力点手动配置弹窗
  const firepowerModal = new FirepowerConfigModal(
    viewerManager,
    updatedPoint => {
      const idx = points.findIndex(p => p.id === updatedPoint.id);
      if (idx >= 0) points[idx] = { ...points[idx], ...updatedPoint };
      else points.push(updatedPoint);

      const currentUavs = deviceManager ? deviceManager.getUavs() : uavs;
      const currentFixed = deviceManager ? deviceManager.getFixedPoints() : points;
      artilleryPanel.update(currentUavs, currentFixed);
      artilleryLayer.updatePlatforms(currentUavs, currentFixed);
      statsPanel.update({ uavs: currentUavs, points: currentFixed, targets: targetService.getAllTargets(), source });
      viewerManager.requestRender();

      if (hasGeographicPosition(updatedPoint)) {
        eventBus.emit("battery:selected", updatedPoint.id);
      }
    },
    null,
    (device, role) => deviceManager?.changeRole(device, role)
  );

  document.getElementById("btn-open-firepower-config")?.addEventListener("click", () => {
    const currentPoints = deviceManager ? deviceManager.getFixedPoints() : points;
    if (deviceManager) {
      deviceManager.openConfig(currentPoints[0] || points[0], "firepower");
    } else {
      firepowerModal.open(points[0]);
    }
  });

  // 6.2 初始化目标上报至火力点交互弹窗
  const targetReportModal = new TargetReportModal(targetService, () => points);

  // 6.3 初始化地图多选悬浮工具条 (入口 2: 地图选取目标)
  const mapTargetHud = document.getElementById("map-target-selection-hud");
  const mapHudCount = document.getElementById("map-hud-selected-count");
  const btnMapHudReport = document.getElementById("btn-map-hud-report");
  const btnMapHudClear = document.getElementById("btn-map-hud-clear");

  eventBus.on("targets:selectionChanged", selectedIds => {
    const count = selectedIds.length;
    if (mapTargetHud) {
      if (count > 0) {
        mapTargetHud.hidden = false;
        mapTargetHud.style.display = "flex";
        mapTargetHud.classList.remove("is-hidden");
        if (mapHudCount) mapHudCount.textContent = String(count);
      } else {
        mapTargetHud.hidden = true;
        mapTargetHud.style.display = "none";
        mapTargetHud.classList.add("is-hidden");
      }
    }
  });

  btnMapHudReport?.addEventListener("click", () => {
    const selected = targetService.getSelectedTargets();
    if (selected.length > 0) {
      targetReportModal.open(selected);
    }
  });

  btnMapHudClear?.addEventListener("click", () => {
    targetService.clearSelection();
  });

  // 6.4 初始化侦察载荷全视频链路总览矩阵 (全链路视频控制中心)
  reconVideoMatrixModal = new ReconVideoMatrixModal(viewerManager, () => {
    return deviceManager ? deviceManager.getUavs() : uavs;
  }, uavs);
  if (activeMainPage === "video-matrix") reconVideoMatrixModal.open(activeMainPageOptions?.preferredSource);

  // 6.5 初始化战术火力整备与编制隶属管理弹窗 (树状图与动态挂载控制中心)
  firepowerAffiliationModal = new FirepowerAffiliationModal(viewerManager, () => deviceManager);
  if (activeMainPage === "firepower") {
    firepowerAffiliationModal.open(activeMainPageOptions?.keepDrawer);
    if (activeMainPageOptions?.openNew) firepowerAffiliationModal.openDrawerForNew();
  }

  const focusUavButton = document.getElementById("btn-view-uav");
  let source = "正在读取目标配置…";

  // 7. 统一帧更新与战术态势渲染管线
  function refreshGuidance() {
    if (!guidanceLayer.visible) {
      guidanceLayer.clear();
      return;
    }
    if (targetReportModal && targetReportModal.isOpen()) return;

    // 严格遵循“人机在环·自主裁决”原则：仅在人工勾选目标时展示态势引导线，未选目标时保持三维地球纯净，不主动绘制
    const selected = targetService.getSelectedTargets();
    if (selected.length > 0) {
      const plan = computeMultiAllocationPlan(selected, points);
      const assignments = (plan.targetPlans || []).map(p => {
        // 核心机制：优先遵从操作员人工裁决；未裁决时遵从系统智能推荐
        const adjudicatedFpId = targetService.getAdjudication(p.target.id);
        const point = (adjudicatedFpId && points.find(pt => pt.id === adjudicatedFpId))
          || p.recommendedPoint
          || points[0];

        return {
          target: p.target,
          firepowerPoint: point,
          isAdjudicated: !!adjudicatedFpId
        };
      });
      guidanceLayer.updateAssignments(assignments);
    } else {
      guidanceLayer.clear();
    }
  }

  function renderState() {
    const targets = targetService.getAllTargets();
    const currentUavs = deviceManager ? deviceManager.getUavs() : uavs;
    const currentFixed = deviceManager ? deviceManager.getFixedPoints() : [];

    // 刷新 Cesium 3D 图层
    artilleryLayer.updatePlatforms(currentUavs, currentFixed);
    targetLayer.updateTargets(targets);
    refreshGuidance();

    // 刷新各 UI 监控面板
    statsPanel.update({ uavs: currentUavs, points: currentFixed, targets, source });
    targetPanel.update(targets);
    if (focusUavButton) focusUavButton.hidden = currentUavs.filter(hasGeographicPosition).length !== 1;

    viewerManager.requestRender();
  }

  // 8. 监听事件流驱动渲染
  eventBus.on("targets:updated", () => renderState());
  eventBus.on("devices:updated", () => {
    statsPanel.update({
      uavs: deviceManager.getUavs(), points: deviceManager.getFixedPoints(),
      targets: targetService.getAllTargets(), source
    });
  });
  eventBus.on("targets:selectionChanged", () => {
    refreshGuidance();
    viewerManager.requestRender();
  });
  eventBus.on("targets:adjudicationChanged", () => {
    refreshGuidance();
    viewerManager.requestRender();
  });
  eventBus.on("guidance:modalClosed", () => {
    refreshGuidance();
    viewerManager.requestRender();
  });

  // 9. 视点切换与态势引导连线快捷按钮
  const btnToggleGuidance = document.getElementById("btn-toggle-guidance");
  btnToggleGuidance?.addEventListener("click", () => {
    guidanceLayer.visible = !guidanceLayer.visible;
    guidanceLayer.refreshVisibility();
    btnToggleGuidance.classList.toggle("active", guidanceLayer.visible);
    const tip = guidanceLayer.visible ? "态势引导线：开启（点击关闭）" : "态势引导线：关闭（点击开启）";
    btnToggleGuidance.title = tip;
    btnToggleGuidance.setAttribute("aria-label", tip);

    if (guidanceLayer.visible) {
      const selected = targetService.getSelectedTargets();
      if (selected.length > 0) {
        refreshGuidance();
        eventBus.emit("log:event", {
          level: "info",
          text: `开启态势引导线：已呈现 ${selected.length} 个目标的打击指派路径`
        });
      } else {
        guidanceLayer.clear();
        eventBus.emit("log:event", {
          level: "info",
          text: "开启态势引导线：请在目标列表或地图上勾选目标以显示引导路径"
        });
      }
    } else {
      guidanceLayer.clear();
      eventBus.emit("log:event", {
        level: "info",
        text: "关闭态势引导线"
      });
    }
    viewerManager.requestRender();
  });
  document.getElementById("btn-view-tactical")?.addEventListener("click", () => {
    viewerManager.flyToScenario(scenarioData.center);
    eventBus.emit("log:event", { level: "info", text: "视角切换至: 战术俯仰全景视角 (-45°)" });
  });

  document.getElementById("btn-view-top")?.addEventListener("click", () => {
    viewerManager.setViewTopDown(scenarioData.center);
    eventBus.emit("log:event", { level: "info", text: "视角切换至: 正射垂直俯视视角 (-90°)" });
  });

  focusUavButton?.addEventListener("click", () => {
    const currentUavs = deviceManager ? deviceManager.getUavs() : uavs;
    const uav = currentUavs.find(hasGeographicPosition);
    if (!uav) return;
    viewerManager.flyToScenario({
      longitude: uav.longitude,
      latitude: uav.latitude,
      altitude: Math.max(1000, (uav.altitude || 0) + 2000)
    });
    eventBus.emit("log:event", { level: "info", text: `视角切换至: 侦察无人机空域 (${uav.name})` });
  });

  // 10. 目标分类过滤按钮
  const btnAll = document.getElementById("filter-btn-all");
  const btnRecon = document.getElementById("filter-btn-recon");
  const btnBurst = document.getElementById("filter-btn-burst");

  const clearActiveFilter = () => {
    [btnAll, btnRecon, btnBurst].forEach(b => b?.classList.remove("active"));
  };

  btnAll?.addEventListener("click", () => {
    clearActiveFilter();
    btnAll.classList.add("active");
    targetPanel.setFilter("ALL");
  });

  btnRecon?.addEventListener("click", () => {
    clearActiveFilter();
    btnRecon.classList.add("active");
    targetPanel.setFilter("RECON");
  });

  btnBurst?.addEventListener("click", () => {
    clearActiveFilter();
    btnBurst.classList.add("active");
    targetPanel.setFilter("BURST");
  });

  // 11. 初始视角定位至演练战区
  viewerManager.flyToScenario(scenarioData.center);

  // 12. 默认执行首次渲染
  renderState();

  let revision;
  readTargets(snapshot => {
    const nextSource = targetFeedLabels[snapshot.state] ?? targetFeedLabels.unavailable;
    const sourceChanged = source !== nextSource;
    source = nextSource;
    const nextRevision = snapshot.instance + ":" + snapshot.revision;
    if (snapshot.targets && nextRevision !== revision) {
      revision = nextRevision;
      targetService.replaceTargets(snapshot.targets, snapshot.instance);
    } else if (sourceChanged) {
      const currentUavs = deviceManager ? deviceManager.getUavs() : uavs;
      const currentFixed = deviceManager ? deviceManager.getFixedPoints() : [];
      statsPanel.update({ uavs: currentUavs, points: currentFixed, targets: targetService.getAllTargets(), source });
    }
  });
}

window.addEventListener("DOMContentLoaded", () => {
  initDataManagement(() => ({
    recon: deviceManager?.getUavs() ?? [],
    firepower: deviceManager?.getFixedPoints() ?? []
  }));
  bindMainPageNavigation();
  targetPanel = new TargetPanel(targetService, viewerManager);
  targetPanel.update(targetService.getAllTargets());

  bootstrap().catch(err => {
    console.error("[URAGS Boot Error]", err);
  });
});
