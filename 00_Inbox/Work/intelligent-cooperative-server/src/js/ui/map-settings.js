import { eventBus } from "../core/event-bus.js";
import { t } from "../core/terms.js";

/**
 * 网页设置 - 初始地图显示位置配置控制器
 * 支持经纬高手动输入、地图交互点选拾取、当前视窗中心提取、视口飞向预览与持久化保存。
 */
export class MapSettings {
  constructor(options = {}) {
    this.getViewerManager = options.getViewerManager || (() => window.__viewerManager);
    this.getScenarioData = options.getScenarioData || (() => null);
    this.onCenterUpdated = options.onCenterUpdated;

    this.inputLon = document.getElementById("setting-center-lon");
    this.inputLat = document.getElementById("setting-center-lat");
    this.inputAlt = document.getElementById("setting-center-alt");
    this.statusBar = document.getElementById("setting-map-status");
    this.pickingBanner = document.getElementById("map-picking-banner");
    this.previewBanner = document.getElementById("map-preview-banner");

    this.defaultCenter = {
      longitude: 118.520000,
      latitude: 24.520000,
      altitude: 26000
    };

    this.init();
  }

  init() {
    this.bindEvents();
    this.loadInitialValues();
  }

  loadInitialValues() {
    const cached = this.getCachedCenter();
    const scenario = this.getScenarioData();
    const center = cached || scenario?.center || this.defaultCenter;

    if (this.inputLon) this.inputLon.value = Number(center.longitude ?? this.defaultCenter.longitude).toFixed(6);
    if (this.inputLat) this.inputLat.value = Number(center.latitude ?? this.defaultCenter.latitude).toFixed(6);
    if (this.inputAlt) this.inputAlt.value = Math.round(Number(center.altitude ?? this.defaultCenter.altitude));
  }

  getCachedCenter() {
    try {
      const raw = localStorage.getItem("intelligent_cooperative_initial_center");
      if (raw) {
        const parsed = JSON.parse(raw);
        if (Number.isFinite(parsed.longitude) && Number.isFinite(parsed.latitude)) {
          return parsed;
        }
      }
    } catch {
      // 忽略本地存储异常
    }
    return null;
  }

  setCachedCenter(center) {
    try {
      localStorage.setItem("intelligent_cooperative_initial_center", JSON.stringify(center));
    } catch {
      // 忽略本地存储异常
    }
  }

  bindEvents() {
    // 地图点选拾取
    document.getElementById("btn-map-pick-coord")?.addEventListener("click", () => this.startMapPick());
    document.getElementById("btn-cancel-map-picking")?.addEventListener("click", () => this.cancelMapPick());

    // 提取当前视口
    document.getElementById("btn-map-capture-current")?.addEventListener("click", () => this.captureCurrentView());

    // 飞向预览
    document.getElementById("btn-map-preview-fly")?.addEventListener("click", () => this.previewFly());
    document.getElementById("btn-back-from-preview")?.addEventListener("click", () => this.backFromPreview());

    // 保存与重置
    document.getElementById("btn-map-save-center")?.addEventListener("click", () => this.saveCenter());
    document.getElementById("btn-map-reset-default")?.addEventListener("click", () => this.resetDefault());

    // 返回综合态势
    document.getElementById("btn-settings-back-tactical")?.addEventListener("click", () => {
      eventBus.emit("page:switch", "tactical");
    });

    // 快捷预设按钮组
    document.querySelectorAll(".btn-preset").forEach(btn => {
      btn.addEventListener("click", () => {
        const lon = Number(btn.getAttribute("data-lon"));
        const lat = Number(btn.getAttribute("data-lat"));
        const alt = Number(btn.getAttribute("data-alt"));
        if (Number.isFinite(lon) && this.inputLon) this.inputLon.value = lon.toFixed(6);
        if (Number.isFinite(lat) && this.inputLat) this.inputLat.value = lat.toFixed(6);
        if (Number.isFinite(alt) && this.inputAlt) this.inputAlt.value = Math.round(alt);
        this.flashInputs();
        this.showStatus(`已填入预设：「${btn.textContent.trim()}」`, "info");
      });
    });

    // 业务数据管理各分类列表快捷跳转
    document.querySelectorAll(".btn-data-hub-action").forEach(btn => {
      btn.addEventListener("click", () => {
        const recordType = btn.getAttribute("data-record-type");
        if (recordType) {
          eventBus.emit("page:switch", "data-management", { recordType });
        }
      });
    });

    // 全量数据管理控制台入口
    document.getElementById("btn-open-data-management")?.addEventListener("click", () => {
      eventBus.emit("page:switch", "data-management");
    });

    // 刷新台账数据
    document.getElementById("btn-refresh-data-counts")?.addEventListener("click", () => {
      this.updateDataCounts(true);
    });

    // 监听切换到设置页面事件，自动刷新数据统计
    eventBus.on("page:switch", (pageName) => {
      if (pageName === "settings") {
        this.updateDataCounts();
      }
    });

    // 初次拉取数据统计
    this.updateDataCounts();
  }

  async updateDataCounts(showNotification = false) {
    try {
      const scenario = this.getScenarioData?.() || {};
      let uavCount = Array.isArray(scenario.uavs) ? scenario.uavs.length : (scenario.uav ? 1 : 0);
      let pointCount = Array.isArray(scenario.firepowerPoints) ? scenario.firepowerPoints.length : (scenario.firepowerPoint ? 1 : 0);
      let targetCount = window.__targetService?.getAllTargets?.()?.length ?? 0;

      // 尝试从管理接口拉取最新实时总数
      const [reconRes, fpRes, tgtRes] = await Promise.allSettled([
        fetch("/api/management/recon?limit=1", { cache: "no-store" }).then(r => r.ok ? r.json() : null),
        fetch("/api/management/firepower?limit=1", { cache: "no-store" }).then(r => r.ok ? r.json() : null),
        fetch("/api/management/targets?limit=1", { cache: "no-store" }).then(r => r.ok ? r.json() : null)
      ]);

      if (reconRes.status === "fulfilled" && Number.isFinite(reconRes.value?.total)) {
        uavCount = reconRes.value.total;
      }
      if (fpRes.status === "fulfilled" && Number.isFinite(fpRes.value?.total)) {
        pointCount = fpRes.value.total;
      }
      if (tgtRes.status === "fulfilled" && Number.isFinite(tgtRes.value?.total)) {
        targetCount = tgtRes.value.total;
      }

      const elRecon = document.getElementById("setting-stat-recon");
      const elFp = document.getElementById("setting-stat-firepower");
      const elTgt = document.getElementById("setting-stat-targets");

      if (elRecon) elRecon.textContent = `${uavCount} 台`;
      if (elFp) elFp.textContent = `${pointCount} 处`;
      if (elTgt) elTgt.textContent = `${targetCount} 项`;

      if (showNotification) {
        const reconLabel = t("roles.recon", "监测席");
        const fpLabel = t("roles.firepower", "作业单元");
        this.showStatus(`台账数据已刷新：${reconLabel} ${uavCount}，${fpLabel} ${pointCount}，目标 ${targetCount}。`, "info");
      }
    } catch (err) {
      console.warn("[MapSettings] 刷新台账数据失败", err);
    }
  }

  startMapPick() {
    const viewerManager = this.getViewerManager();
    if (!viewerManager?.viewer) {
      this.showStatus("三维地图视口尚未初始化，无法启动拾取", "error");
      return;
    }

    // 切换到态势三维主界面
    eventBus.emit("page:switch", "tactical");

    // 显示顶部点选提示条
    if (this.pickingBanner) {
      this.pickingBanner.hidden = false;
      this.pickingBanner.style.display = "block";
    }

    viewerManager.startMapCoordinatePicking(
      coord => {
        if (this.inputLon) this.inputLon.value = Number(coord.longitude).toFixed(6);
        if (this.inputLat) this.inputLat.value = Number(coord.latitude).toFixed(6);

        // 高度处理：若用户原高度小于 1000m 或未填写，则自动采用当前相机视点高度
        const currentAlt = Number(this.inputAlt?.value);
        if (!currentAlt || currentAlt < 1000) {
          const cam = viewerManager.viewer?.camera?.positionCartographic;
          const eyeAlt = cam ? Math.round(cam.height) : 26000;
          if (this.inputAlt) this.inputAlt.value = Math.max(1000, eyeAlt);
        }

        this.finishMapPick();
        eventBus.emit("page:switch", "settings");
        this.flashInputs();
        this.showStatus(`已拾取坐标：${this.inputLon.value}°, ${this.inputLat.value}°`, "success");
      },
      () => {
        this.finishMapPick();
        eventBus.emit("page:switch", "settings");
        this.showStatus("已取消地图点选", "info");
      }
    );
  }

  finishMapPick() {
    if (this.pickingBanner) {
      this.pickingBanner.hidden = true;
      this.pickingBanner.style.display = "none";
    }
  }

  cancelMapPick() {
    const viewerManager = this.getViewerManager();
    viewerManager?.cancelMapCoordinatePicking(true);
    this.finishMapPick();
    eventBus.emit("page:switch", "settings");
  }

  captureCurrentView() {
    const viewerManager = this.getViewerManager();
    const viewer = viewerManager?.viewer;
    if (!viewer) {
      this.showStatus("三维地图视口尚未初始化，无法读取相机坐标", "error");
      return;
    }

    try {
      const scene = viewer.scene;
      const canvas = scene?.canvas;
      let cartesian = null;

      // 1. 若三维态势画布处于可见渲染状态（clientWidth > 0），优先使用视口中心像素投射射线拾取地表
      if (canvas && canvas.clientWidth > 0 && canvas.clientHeight > 0 && viewer.camera?.getPickRay) {
        try {
          const centerPixel = new Cesium.Cartesian2(canvas.clientWidth / 2, canvas.clientHeight / 2);
          const ray = viewer.camera.getPickRay(centerPixel);
          if (ray && !Number.isNaN(ray.origin?.x) && !Number.isNaN(ray.direction?.x)) {
            cartesian = scene.globe?.pick(ray, scene);
          }
        } catch {
          // 忽略画布投射异常
        }
      }

      // 2. 若处于设置页面（地图画布后台隐藏 clientWidth === 0）或投射未命中地形，
      //    直接使用 Cesium 相机自身空间视线向量 (positionWC / directionWC) 与地球椭球面求交
      if (!cartesian && viewer.camera?.positionWC && viewer.camera?.directionWC) {
        try {
          const centerRay = new Cesium.Ray(viewer.camera.positionWC, viewer.camera.directionWC);
          if (scene?.globe) {
            cartesian = scene.globe.pick(centerRay, scene);
          }
          if (!cartesian && scene?.globe?.ellipsoid) {
            const interval = Cesium.IntersectionTests.rayEllipsoid(centerRay, scene.globe.ellipsoid);
            if (interval && interval.start > 0) {
              cartesian = Cesium.Ray.getPoint(centerRay, interval.start);
            }
          }
        } catch {
          // 忽略空间射线求交异常
        }
      }

      // 3. 提取拾取到的经纬度；若未求交（例如仰视天空），则回退为当前相机正下方地表投影点
      let lon = null;
      let lat = null;
      if (cartesian) {
        try {
          const carto = Cesium.Cartographic.fromCartesian(cartesian);
          if (carto && Number.isFinite(carto.longitude) && Number.isFinite(carto.latitude)) {
            lon = Cesium.Math.toDegrees(carto.longitude).toFixed(6);
            lat = Cesium.Math.toDegrees(carto.latitude).toFixed(6);
          }
        } catch {}
      }

      const camCarto = viewer.camera?.positionCartographic;
      if ((lon === null || lat === null) && camCarto) {
        lon = Cesium.Math.toDegrees(camCarto.longitude).toFixed(6);
        lat = Cesium.Math.toDegrees(camCarto.latitude).toFixed(6);
      }

      if (lon === null || lat === null) {
        this.showStatus("无法解析当前地图相机坐标，请稍候重试", "error");
        return;
      }

      if (this.inputLon) this.inputLon.value = lon;
      if (this.inputLat) this.inputLat.value = lat;

      const alt = camCarto ? Math.max(100, Math.round(camCarto.height)) : 26000;
      if (this.inputAlt) this.inputAlt.value = alt;

      this.flashInputs();
      this.showStatus(`已获取视心：${this.inputLon.value}°, ${this.inputLat.value}° · ${this.inputAlt.value}m`, "success");
    } catch (err) {
      console.warn("[MapSettings] 读取视窗坐标失败", err);
      this.showStatus("读取视窗坐标失败，请在地图就绪后重试", "error");
    }
  }

  previewFly() {
    const coords = this.readAndValidateInputs();
    if (!coords) return;

    const viewerManager = this.getViewerManager();
    const viewer = viewerManager?.viewer;
    if (!viewer) {
      this.showStatus("三维地图视口尚未就绪", "error");
      return;
    }

    eventBus.emit("page:switch", "tactical");

    if (this.previewBanner) {
      this.previewBanner.hidden = false;
      this.previewBanner.style.display = "block";
    }

    viewer.camera.flyTo({
      destination: Cesium.Cartesian3.fromDegrees(coords.longitude, coords.latitude - 0.16, coords.altitude),
      duration: 1.5
    });
  }

  backFromPreview() {
    if (this.previewBanner) {
      this.previewBanner.hidden = true;
      this.previewBanner.style.display = "none";
    }
    eventBus.emit("page:switch", "settings");
  }

  async saveCenter() {
    const coords = this.readAndValidateInputs();
    if (!coords) return;

    this.showStatus("正在向服务端持久化保存初始位置配置…", "info");

    try {
      const response = await fetch("/api/scenario/center", {
        method: "POST",
        headers: { "Content-Type": "application/json; charset=utf-8" },
        body: JSON.stringify(coords)
      });

      const result = await response.json().catch(() => null);
      if (!response.ok || !result?.ok) {
        throw new Error(result?.error || "服务端保存失败，请检查服务连接");
      }

      // 更新运行时场景中心与本地持久化缓存
      this.setCachedCenter(coords);
      const scenario = this.getScenarioData();
      if (scenario) {
        scenario.center = coords;
      }

      if (typeof this.onCenterUpdated === "function") {
        this.onCenterUpdated(coords);
      }

      this.flashInputs();
      this.showStatus(`位置配置已保存：${coords.longitude.toFixed(6)}°, ${coords.latitude.toFixed(6)}° · ${coords.altitude}m`, "success");
    } catch (err) {
      console.error("[MapSettings] 保存初始视口异常", err);
      // 服务端若离线或未通，仍然保存在前端本地存储供后续使用
      this.setCachedCenter(coords);
      const scenario = this.getScenarioData();
      if (scenario) scenario.center = coords;

      this.showStatus(`服务端保存受阻 (${err.message})，已先保存在本机生效。`, "error");
    }
  }

  resetDefault() {
    const scenario = this.getScenarioData();
    const target = scenario?.center || this.defaultCenter;

    if (this.inputLon) this.inputLon.value = Number(target.longitude ?? this.defaultCenter.longitude).toFixed(6);
    if (this.inputLat) this.inputLat.value = Number(target.latitude ?? this.defaultCenter.latitude).toFixed(6);
    if (this.inputAlt) this.inputAlt.value = Math.round(Number(target.altitude ?? this.defaultCenter.altitude));

    this.flashInputs();
    this.showStatus("已恢复默认位置，点击「保存配置」应用。", "info");
  }

  readAndValidateInputs() {
    const lon = Number(this.inputLon?.value);
    const lat = Number(this.inputLat?.value);
    const alt = Number(this.inputAlt?.value);

    if (!Number.isFinite(lon) || lon < -180 || lon > 180) {
      this.showStatus("经度数值无效，请输入 -180.0 到 180.0 之间的数值", "error");
      this.inputLon?.focus();
      return null;
    }

    if (!Number.isFinite(lat) || lat < -90 || lat > 90) {
      this.showStatus("纬度数值无效，请输入 -90.0 到 90.0 之间的数值", "error");
      this.inputLat?.focus();
      return null;
    }

    if (!Number.isFinite(alt) || alt < 100 || alt > 10000000) {
      this.showStatus("视点高度数值无效，请输入 100 到 10,000,000 米之间的数值", "error");
      this.inputAlt?.focus();
      return null;
    }

    return {
      longitude: Number(lon.toFixed(6)),
      latitude: Number(lat.toFixed(6)),
      altitude: Math.round(alt)
    };
  }

  flashInputs() {
    [this.inputLon, this.inputLat, this.inputAlt].forEach(el => {
      if (!el) return;
      el.classList.add("input-highlight");
      setTimeout(() => el.classList.remove("input-highlight"), 1500);
    });
  }

  showStatus(message, type = "info") {
    if (!this.statusBar) return;
    this.statusBar.hidden = false;
    this.statusBar.className = `settings-status-bar is-${type}`;
    this.statusBar.textContent = message;

    if (this.statusTimer) clearTimeout(this.statusTimer);
    if (type !== "error") {
      this.statusTimer = setTimeout(() => {
        if (this.statusBar) this.statusBar.hidden = true;
      }, 5000);
    }
  }
}

export function initMapSettings(options) {
  return new MapSettings(options);
}
