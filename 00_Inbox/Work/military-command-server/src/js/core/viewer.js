/**
 * Cesium.js 三维地球 Viewer 管理器
 * 加载在线卫星底图或本地离线瓦片
 */
import { eventBus } from "./event-bus.js";

// 全局防御 Cesium 内部对布尔值 skyAtmosphere/skyBox 调用方法的类型异常
if (typeof Boolean.prototype.setDynamicLighting !== "function") {
  Boolean.prototype.setDynamicLighting = function() { return this; };
}
if (typeof Boolean.prototype.update !== "function") {
  Boolean.prototype.update = function() { return undefined; };
}
if (typeof Boolean.prototype.destroy !== "function") {
  Boolean.prototype.destroy = function() {};
}
if (typeof Boolean.prototype.isDestroyed !== "function") {
  Boolean.prototype.isDestroyed = function() { return false; };
}

export class CesiumViewerManager {
  constructor(containerId) {
    this.containerId = containerId;
    this.viewer = null;
    this.scene = null;
    this.initViewer();
  }

  initViewer() {
    // 底图由 Esri 或 OpenStreetMap 提供，使用各数据源自己的署名。
    Cesium.CreditDisplay.cesiumCredit = undefined;

    // 初始化 Viewer：禁用冗余组件，禁用布尔 skyAtmosphere/skyBox 避免 Cesium 内部类型异常
    this.viewer = new Cesium.Viewer(this.containerId, {
      animation: false,
      timeline: false,
      baseLayer: false,
      baseLayerPicker: false,
      geocoder: false,
      homeButton: false,
      sceneModePicker: false,
      navigationHelpButton: false,
      infoBox: false,
      selectionIndicator: false,
      fullscreenButton: false,
      creditContainer: document.createElement("div"),
      skyBox: false,
      skyAtmosphere: false,
      scene3DOnly: true,
      requestRenderMode: true,
      maximumRenderTimeChange: Infinity
    });

    this.scene = this.viewer.scene;
    const canvas = this.scene.canvas;
    const gl = canvas.getContext("webgl2") || canvas.getContext("webgl");
    const rendererInfo = gl?.getExtension("WEBGL_debug_renderer_info");
    const renderer = rendererInfo ? gl.getParameter(rendererInfo.UNMASKED_RENDERER_WEBGL) : "";
    const softwareRenderer = /llvmpipe|softpipe|swiftshader|software rasterizer|microsoft basic render/i.test(renderer);
    const maliRenderer = /\bmali\b/i.test(renderer);
    // 降低 Mali 和软件渲染的绘图缓冲区分辨率，保留画布和控件的显示尺寸。
    if (softwareRenderer || maliRenderer) {
      this.viewer.resolutionScale = 0.5;
      this.scene.msaaSamples = 1;
      this.scene.postProcessStages.fxaa.enabled = false;
    }

    // 强制清理布尔值或不完整的 skyAtmosphere / skyBox，彻底杜绝 Cesium 渲染循环内部类型崩溃
    if (!this.scene.skyAtmosphere || typeof this.scene.skyAtmosphere.setDynamicLighting !== "function") {
      this.scene.skyAtmosphere = undefined;
    }
    if (!this.scene.skyBox || typeof this.scene.skyBox.update !== "function") {
      this.scene.skyBox = undefined;
    }

    this.scene.backgroundColor = Cesium.Color.fromCssColorString("#090f17");
    this.scene.globe.baseColor = Cesium.Color.fromCssColorString("#15202b");
    this.scene.globe.showGroundAtmosphere = false;
    this.scene.globe.enableLighting = false;
    if (this.viewer.creditDisplay?.container) {
      this.viewer.creditDisplay.container.style.display = "none";
    }

    // 初始设置大范围中国与战术区域宏观视角
    this.viewer.camera.setView({
      destination: Cesium.Rectangle.fromDegrees(71.0, 16.0, 138.0, 55.0)
    });

    this.initPickHandler();
  }

  async setMapMode(mode) {
    this.mapRequest?.abort();
    this.mapRequest = new AbortController();
    const { signal } = this.mapRequest;
    const imagery = this.viewer.imageryLayers;
    imagery.removeAll();
    imagery.addImageryProvider(new Cesium.GridImageryProvider({
      cells: 4,
      color: Cesium.Color.fromCssColorString("#375365"),
      glowColor: Cesium.Color.TRANSPARENT,
      backgroundColor: Cesium.Color.fromCssColorString("#15202b")
    }));
    this.requestRender();
    try {
      const result = mode === "offline"
        ? await this.loadOfflineImagery(signal)
        : await this.loadOnlineImagery(signal);
      if (signal.aborted) return;
      imagery.removeAll();
      for (const provider of result.providers) imagery.addImageryProvider(provider);
      this.requestRender();
      return result;
    } catch (error) {
      if (signal.aborted) return;
      console.warn("[CesiumViewer] 底图加载失败", error);
      return { label: "底图不可用 · 地图网格", credits: [] };
    }
  }

  async loadOfflineImagery(signal) {
    const providers = [];
    const credits = new Set();
    const baseDir = "/public/offline-satellite/";
    const manifests = await Promise.allSettled([
      fetch(`${baseDir}manifest.json`, { signal }).then(response => response.ok ? response.json() : null),
      fetch("/offline-maps/combined-theaters/manifest.json", { signal }).then(response => response.ok ? response.json() : null)
    ]);
    signal.throwIfAborted();

    for (const layer of manifests[0].value?.layers ?? []) {
      if (layer.id !== "world" && layer.id !== "china") continue;
      providers.push(new Cesium.UrlTemplateImageryProvider({
        url: `${baseDir}${layer.id}/{z}/{x}/{y}.jpg`,
        rectangle: Cesium.Rectangle.fromDegrees(...layer.bounds),
        maximumLevel: layer.maximumLevel,
        enablePickFeatures: false
      }));
      if (layer.credit) credits.add(layer.credit);
    }
    const hasSatelliteBase = providers.length > 0;
    if (!hasSatelliteBase) {
      providers.push(await Cesium.TileMapServiceImageryProvider.fromUrl(
        Cesium.buildModuleUrl("Assets/Textures/NaturalEarthII")
      ));
      credits.add("Natural Earth II");
    }

    const atlas = manifests[1].value;
    if (atlas?.scheme === "xyz" && atlas.crs === "EPSG:3857" && Array.isArray(atlas.layers)) {
      const layers = [...atlas.layers].sort((a, b) => a.minimumLevel - b.minimumLevel);
      for (const layer of layers) {
        if (!Array.isArray(layer.bounds) || !layer.coverage ||
            !Number.isFinite(layer.minimumLevel) || !Number.isFinite(layer.maximumLevel)) continue;
        const provider = new Cesium.UrlTemplateImageryProvider({
          url: "/api/map/tiles/{z}/{x}/{y}.jpg",
          minimumLevel: layer.minimumLevel,
          maximumLevel: layer.maximumLevel,
          rectangle: Cesium.Rectangle.fromDegrees(...layer.bounds),
          enablePickFeatures: false,
          tileDiscardPolicy: new Cesium.DiscardEmptyTileImagePolicy()
        });
        const requestImage = provider.requestImage.bind(provider);
        provider.requestImage = (x, y, z, request) => {
          const range = layer.coverage[z];
          const rowRanges = range?.rows?.[y];
          const covered = range && (rowRanges
            ? rowRanges.some(([x0, x1]) => x >= x0 && x <= x1)
            : x >= range.x0 && x <= range.x1 && y >= range.y0 && y <= range.y1);
          if (!covered) return Promise.resolve(Cesium.DiscardEmptyTileImagePolicy.EMPTY_IMAGE);
          return requestImage(x, y, z, request);
        };
        providers.push(provider);
        if (layer.credit) credits.add(layer.credit);
      }
    }

    const label = hasSatelliteBase || providers.length > 1 ? "离线综合卫星图" : "离线概览图";
    return { providers, credits: [...credits], label };
  }

  async loadOnlineImagery(signal) {
    const satelliteUrl = "https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer";
    try {
      const provider = await Cesium.ArcGisMapServerImageryProvider.fromUrl(satelliteUrl, {
        enablePickFeatures: false
      });
      return { providers: [provider], credits: ["Esri World Imagery"], label: "在线卫星图" };
    } catch (err) {
      signal.throwIfAborted();
      console.warn("[CesiumViewer] Esri 卫星底图加载受阻，启用 OpenStreetMap 兜底底图:", err);
      const provider = new Cesium.OpenStreetMapImageryProvider({
        url: "https://tile.openstreetmap.org/",
        credit: new Cesium.Credit(
          '<a href="https://www.openstreetmap.org/copyright" target="_blank" rel="noopener noreferrer">© OpenStreetMap contributors</a>',
          true
        )
      });
      return { providers: [provider], credits: ["© OpenStreetMap contributors"], label: "在线地图 · OpenStreetMap" };
    }
  }

  initPickHandler() {
    const handler = new Cesium.ScreenSpaceEventHandler(this.scene.canvas);

    handler.setInputAction(movement => {
      // 若处于地图点选标定模式或刚刚完成框选拖拽，屏蔽常规图层点击选中
      if (this.pickingHandler || this._justBoxSelected) return;

      const pickedObject = this.scene.pick(movement.position);
      if (Cesium.defined(pickedObject) && pickedObject.id) {
        const entity = pickedObject.id;
        if (entity._customType === "target") {
          // 点选地图目标：切换多选状态
          eventBus.emit("target:toggleSelection", entity._dataId);
        } else if (entity._customType === "battery") {
          eventBus.emit("battery:selected", entity._dataId);
        } else if (entity._customType === "platform") {
          eventBus.emit("platform:selected", entity._dataId);
        }
      }
    }, Cesium.ScreenSpaceEventType.LEFT_CLICK);
  }

  setBoxSelectMode(enabled) {
    this.isBoxSelectMode = !!enabled;
    const container = document.getElementById(this.containerId);
    const viewport = container?.closest(".map-viewport") || container;
    viewport?.classList.toggle("is-box-selecting", this.isBoxSelectMode);
    const btn = document.getElementById("btn-box-select");
    if (btn) {
      btn.classList.toggle("active", this.isBoxSelectMode);
      const tip = this.isBoxSelectMode
        ? "框选模式：已开启（在地图上按住左键拖拽框选目标，再次点击退出）"
        : "框选目标（点击开启，或在地图上按住Shift键拖拽框选）";
      btn.title = tip;
      btn.setAttribute("aria-label", tip);
    }
  }

  initBoxSelection(getTargets) {
    const container = document.getElementById(this.containerId);
    if (!container) return;
    const viewport = container.closest(".map-viewport") || container;

    let marquee = document.getElementById("cesium-box-select-marquee");
    if (!marquee) {
      marquee = document.createElement("div");
      marquee.id = "cesium-box-select-marquee";
      marquee.className = "cesium-box-select-marquee";
      marquee.hidden = true;
      viewport.appendChild(marquee);
    }

    this.isBoxSelectMode = false;
    this._justBoxSelected = false;
    let isDragging = false;
    let startX = 0, startY = 0;
    let dragMode = "replace";

    const onPointerDown = e => {
      if (e.button !== 0) return;
      const shouldBoxSelect = this.isBoxSelectMode || e.shiftKey || e.ctrlKey;
      if (!shouldBoxSelect) return;

      const rect = viewport.getBoundingClientRect();
      if (e.clientX < rect.left || e.clientX > rect.right || e.clientY < rect.top || e.clientY > rect.bottom) {
        return;
      }

      isDragging = true;
      startX = e.clientX;
      startY = e.clientY;
      dragMode = (e.shiftKey || e.ctrlKey) ? "add" : "replace";

      this.viewer.scene.screenSpaceCameraController.enableInputs = false;
      viewport.classList.add("is-box-selecting");

      marquee.style.left = `${startX - rect.left}px`;
      marquee.style.top = `${startY - rect.top}px`;
      marquee.style.width = "0px";
      marquee.style.height = "0px";
      marquee.hidden = false;

      e.preventDefault();
      e.stopPropagation();
    };

    const onPointerMove = e => {
      if (!isDragging) return;
      const rect = viewport.getBoundingClientRect();
      const currentX = Math.max(rect.left, Math.min(rect.right, e.clientX));
      const currentY = Math.max(rect.top, Math.min(rect.bottom, e.clientY));

      const minX = Math.min(startX, currentX) - rect.left;
      const minY = Math.min(startY, currentY) - rect.top;
      const width = Math.abs(currentX - startX);
      const height = Math.abs(currentY - startY);

      marquee.style.left = `${minX}px`;
      marquee.style.top = `${minY}px`;
      marquee.style.width = `${width}px`;
      marquee.style.height = `${height}px`;

      e.preventDefault();
    };

    const onPointerUp = e => {
      if (!isDragging) return;
      isDragging = false;
      marquee.hidden = true;
      this.viewer.scene.screenSpaceCameraController.enableInputs = true;
      if (!this.isBoxSelectMode) {
        viewport.classList.remove("is-box-selecting");
      }

      const rect = viewport.getBoundingClientRect();
      const endX = Math.max(rect.left, Math.min(rect.right, e.clientX));
      const endY = Math.max(rect.top, Math.min(rect.bottom, e.clientY));

      const boxLeft = Math.min(startX, endX) - rect.left;
      const boxRight = Math.max(startX, endX) - rect.left;
      const boxTop = Math.min(startY, endY) - rect.top;
      const boxBottom = Math.max(startY, endY) - rect.top;

      const width = boxRight - boxLeft;
      const height = boxBottom - boxTop;

      if (width >= 6 && height >= 6) {
        this._justBoxSelected = true;
        setTimeout(() => { this._justBoxSelected = false; }, 200);

        const targets = typeof getTargets === "function" ? getTargets() : [];
        const boxedIds = [];
        const scene = this.scene;
        const camera = scene.camera;
        const cameraPos = camera.positionWC;
        const cameraDir = camera.directionWC;
        const occluder = new Cesium.EllipsoidalOccluder(scene.globe.ellipsoid, cameraPos);

        for (const target of targets) {
          if (!Number.isFinite(target.longitude) || !Number.isFinite(target.latitude)) continue;
          const pos = Cesium.Cartesian3.fromDegrees(target.longitude, target.latitude, target.altitude || 0);

          // 1. 判断是否在相机前方
          const toTarget = Cesium.Cartesian3.subtract(pos, cameraPos, new Cesium.Cartesian3());
          if (Cesium.Cartesian3.dot(cameraDir, toTarget) <= 0) continue;

          // 2. 判断是否被地球地平线遮挡
          if (!occluder.isPointVisible(pos)) continue;

          // 3. 计算屏幕像素坐标
          const winPos = Cesium.SceneTransforms.worldToWindowCoordinates(scene, pos);
          if (!winPos) continue;

          // 4. 判定是否落在选框矩形内 (包含 8px 容差)
          if (winPos.x >= boxLeft - 8 && winPos.x <= boxRight + 8 &&
              winPos.y >= boxTop - 8 && winPos.y <= boxBottom + 8) {
            boxedIds.push(target.id);
          }
        }

        eventBus.emit("target:selectMultiple", { ids: boxedIds, mode: dragMode });
        eventBus.emit("log:event", {
          level: "info",
          text: `地图框选完成：已圈选 ${boxedIds.length} 个目标`
        });
      }
    };

    viewport.addEventListener("pointerdown", onPointerDown, true);
    window.addEventListener("pointermove", onPointerMove, true);
    window.addEventListener("pointerup", onPointerUp, true);
  }

  flyToScenario(center) {
    if (!center) return;
    this.viewer.camera.flyTo({
      destination: Cesium.Cartesian3.fromDegrees(center.longitude, center.latitude - 0.16, center.altitude || 24000),
      orientation: {
        heading: Cesium.Math.toRadians(0),
        pitch: Cesium.Math.toRadians(-45),
        roll: 0.0
      },
      duration: 1.8
    });
  }

  setViewTopDown(center) {
    if (!center) return;
    this.viewer.camera.flyTo({
      destination: Cesium.Cartesian3.fromDegrees(center.longitude, center.latitude, (center.altitude || 24000) * 0.9),
      orientation: {
        heading: 0,
        pitch: Cesium.Math.toRadians(-90),
        roll: 0
      },
      duration: 1.2
    });
  }

  setViewTactical(center) {
    this.flyToScenario(center);
  }

  flyToEntity(longitude, latitude, altitude = 0) {
    const position = Cesium.Cartesian3.fromDegrees(longitude, latitude, altitude);
    this.viewer.camera.flyToBoundingSphere(new Cesium.BoundingSphere(position, 0), {
      offset: new Cesium.HeadingPitchRange(0, Cesium.Math.toRadians(-45), 11000),
      duration: 1.2
    });
  }

  flyToFirepowerPoint(longitude, latitude, altitude = 0, strikeRangeMeters = 40000) {
    if (longitude == null || latitude == null) return;
    const position = Cesium.Cartesian3.fromDegrees(longitude, latitude, altitude || 0);
    const range = Math.max((strikeRangeMeters || 40000) * 2.2, 25000);
    this.viewer.camera.flyToBoundingSphere(new Cesium.BoundingSphere(position, 0), {
      offset: new Cesium.HeadingPitchRange(
        Cesium.Math.toRadians(0),
        Cesium.Math.toRadians(-50),
        range
      ),
      duration: 1.4
    });
  }

  startMapCoordinatePicking(onPicked, onCancel) {
    if (!this.viewer) return;
    this.cancelMapCoordinatePicking(false);

    const canvas = this.scene.canvas;
    canvas.style.cursor = "crosshair";

    const handler = new Cesium.ScreenSpaceEventHandler(canvas);
    this.pickingHandler = handler;
    this.onCancelPicking = onCancel;

    handler.setInputAction(movement => {
      const ray = this.viewer.camera.getPickRay(movement.position);
      const cartesian = this.scene.globe.pick(ray, this.scene);
      if (cartesian) {
        const carto = Cesium.Cartographic.fromCartesian(cartesian);
        const lon = Number(Cesium.Math.toDegrees(carto.longitude).toFixed(6));
        const lat = Number(Cesium.Math.toDegrees(carto.latitude).toFixed(6));
        const alt = Math.max(0, Math.round(carto.height || 0));

        this.showTempPickedPin(cartesian);

        const cb = onPicked;
        this.cancelMapCoordinatePicking(false);
        if (typeof cb === "function") {
          cb({ longitude: lon, latitude: lat, altitude: alt, cartesian });
        }
      }
    }, Cesium.ScreenSpaceEventType.LEFT_CLICK);
  }

  cancelMapCoordinatePicking(triggerCancelCb = true) {
    if (this.pickingHandler && !this.pickingHandler.isDestroyed()) {
      this.pickingHandler.destroy();
    }
    this.pickingHandler = null;

    if (this.scene?.canvas) {
      this.scene.canvas.style.cursor = "default";
    }

    if (triggerCancelCb && typeof this.onCancelPicking === "function") {
      const cb = this.onCancelPicking;
      this.onCancelPicking = null;
      cb();
    } else {
      this.onCancelPicking = null;
    }
  }

  showTempPickedPin(cartesian) {
    if (this.tempPinEntity) {
      this.viewer.entities.remove(this.tempPinEntity);
      this.tempPinEntity = null;
    }
    this.tempPinEntity = this.viewer.entities.add({
      position: cartesian,
      point: {
        pixelSize: 18,
        color: Cesium.Color.fromCssColorString("#00f0ff"),
        outlineColor: Cesium.Color.WHITE,
        outlineWidth: 3,
        disableDepthTestDistance: Number.POSITIVE_INFINITY
      }
    });
    this.requestRender();
    setTimeout(() => {
      if (this.tempPinEntity) {
        this.viewer.entities.remove(this.tempPinEntity);
        this.tempPinEntity = null;
        this.requestRender();
      }
    }, 5000);
  }

  requestRender() {
    if (this.scene) {
      this.scene.requestRender();
    }
  }

  destroy() {
    if (this.viewer && !this.viewer.isDestroyed()) {
      this.viewer.destroy();
    }
  }
}
