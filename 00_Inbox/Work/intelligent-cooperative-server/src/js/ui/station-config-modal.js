/**
 * 地面作业单元点位信息与覆盖范围场景配置交互弹窗
 */
import { getPlatformDeviceId } from "../core/platform-labels.js";
import { t } from "../core/terms.js";

const activeEditors = new WeakMap();

export class StationConfigModal {
  constructor(viewerManager, saveSimulationCoordinates, changeSimulationRole = null, reconMount = null) {
    this.viewerManager = viewerManager;
    this.saveSimulationCoordinates = saveSimulationCoordinates;
    this.changeSimulationRole = changeSimulationRole;
    this.reconMount = reconMount;
    this.currentPoint = null;
    this.isSaving = false;

    this.modal = document.getElementById("firepower-config-modal");
    this.form = document.getElementById("form-firepower-config");
    this.btnClose = document.getElementById("btn-close-firepower-modal");
    this.btnCancel = document.getElementById("btn-cancel-firepower");
    this.btnSave = document.getElementById("btn-save-firepower");
    this.btnRoleToFirepower = document.getElementById("btn-modal-role-to-firepower");
    this.btnRoleToRecon = document.getElementById("btn-modal-role-to-recon");
    this.saveError = document.getElementById("cfg-fp-save-error");
    this.btnPickCenter = document.getElementById("btn-pick-map-center");
    this.btnPickPoint = document.getElementById("btn-pick-map-point");
    this.pickedHint = document.getElementById("cfg-fp-picked-hint");
    this.hud = document.getElementById("map-picking-hud");
    this.btnCancelPicking = document.getElementById("btn-cancel-picking");
    this.isPickingMode = false;

    this.inputName = document.getElementById("cfg-fp-name");
    this.inputNameLabel = document.getElementById("cfg-fp-name-label");
    this.inputLon = document.getElementById("cfg-fp-lon");
    this.inputLat = document.getElementById("cfg-fp-lat");
    this.inputAlt = document.getElementById("cfg-fp-alt");
    this.inputRange = document.getElementById("cfg-fp-range");
    this.inputMaxTargets = document.getElementById("cfg-fp-max-targets");
    this.inputIp = document.getElementById("cfg-fp-ip");
    this.inputPort = document.getElementById("cfg-fp-port");
    this.inputDeviceId = document.getElementById("cfg-fp-device");
    this.inputBattalion = document.getElementById("cfg-fp-battalion");
    this.inputCompany = document.getElementById("cfg-fp-company");
    this.inputMountedDevice = document.getElementById("cfg-fp-mounted-device");

    this.dispName = document.getElementById("cfg-fp-disp-name");
    this.dispId = document.getElementById("cfg-fp-disp-id");
    this.dispIp = document.getElementById("cfg-fp-disp-ip");
    this.dispStatus = document.getElementById("cfg-fp-disp-status");
    this.mountedMeta = document.getElementById("cfg-fp-mounted-meta");
    this.dispMounted = document.getElementById("cfg-fp-disp-mounted");
    this.mountSection = document.getElementById("cfg-recon-mount");
    this.mountStatus = document.getElementById("cfg-recon-mount-status");
    this.mountEdit = document.getElementById("cfg-recon-mount-edit");
    this.mountPicker = document.getElementById("cfg-recon-mount-picker");
    this.mountSelect = document.getElementById("cfg-recon-mount-point");
    this.mountConfirm = document.getElementById("cfg-recon-mount-confirm");
    this.mountRemove = document.getElementById("cfg-recon-mount-remove");

    this.initEvents();
    this.close();
  }

  initEvents() {
    this.btnClose?.addEventListener("click", () => this.close());
    this.btnCancel?.addEventListener("click", () => this.close());
    this.modal?.addEventListener("click", e => {
      if (e.target === this.modal) this.close();
    });

    window.addEventListener("keydown", e => {
      if (e.key === "Escape" && activeEditors.get(this.modal) === this) {
        if (this.isPickingMode) {
          this.cancelPickingPoint();
        } else if (this.modal && !this.modal.hidden && this.modal.style.display !== "none") {
          this.close();
        }
      }
    });

    this.btnPickCenter?.addEventListener("click", () => {
      if (activeEditors.get(this.modal) === this) this.pickMapCenter();
    });
    this.btnPickPoint?.addEventListener("click", () => {
      if (activeEditors.get(this.modal) === this) this.startPickingPoint();
    });
    this.btnCancelPicking?.addEventListener("click", () => {
      if (activeEditors.get(this.modal) === this) this.cancelPickingPoint();
    });

    this.mountEdit?.addEventListener("click", () => {
      if (activeEditors.get(this.modal) !== this || !this.reconMount) return;
      if (!this.mountPicker.hidden) {
        this.mountPicker.hidden = true;
        return;
      }
      this.renderReconMount(this.reconMount.getMountedPoint(this.currentPoint));
      const points = this.reconMount.getFirepowerPoints();
      this.mountSelect.replaceChildren(new Option(points.length ? t("ui.selectMountPoint", "选择挂载作业单元…") : t("ui.noMountablePoint", "暂无可挂载作业单元"), ""));
      for (const point of points) {
        const label = `${point.battalion || t("org.battalionDefault", "1区")}·${point.company || t("org.companyDefault", "1组")}·${point.name}`;
        this.mountSelect.add(new Option(label, point.id));
      }
      this.mountSelect.value = points.some(point => point.id === this.mountedTo?.id) ? this.mountedTo.id : "";
      this.mountConfirm.disabled = !this.mountSelect.value || this.mountSelect.value === this.mountedTo?.id;
      this.mountPicker.hidden = false;
    });
    this.mountSelect?.addEventListener("change", () => {
      this.mountConfirm.disabled = !this.mountSelect.value || this.mountSelect.value === this.mountedTo?.id;
    });
    this.mountConfirm?.addEventListener("click", () => this.updateReconMount(false));
    this.mountRemove?.addEventListener("click", () => this.updateReconMount(true));

    this.btnRoleToFirepower?.addEventListener("click", async () => {
      if (activeEditors.get(this.modal) !== this) return;
      if (this.changeSimulationRole && this.currentPoint) {
        this.btnRoleToFirepower.disabled = true;
        try {
          await this.changeSimulationRole(this.currentPoint, "firepower");
          this.close();
        } catch (err) {
          this.showSaveError(err.message || t("ui.setAsFirepowerFail", "设置为同时作为作业单元失败"));
        } finally {
          this.btnRoleToFirepower.disabled = false;
        }
      }
    });

    this.btnRoleToRecon?.addEventListener("click", async () => {
      if (activeEditors.get(this.modal) !== this) return;
      if (this.changeSimulationRole && this.currentPoint) {
        this.btnRoleToRecon.disabled = true;
        try {
          await this.changeSimulationRole(this.currentPoint, "uav");
          this.close();
        } catch (err) {
          this.showSaveError(err.message || t("ui.cancelStandaloneFail", "取消独立作业单元失败"));
        } finally {
          this.btnRoleToRecon.disabled = false;
        }
      }
    });

    this.form?.addEventListener("submit", e => {
      if (activeEditors.get(this.modal) !== this) return;
      e.preventDefault();
      this.handleSubmit();
    });

  }

  open(point, preserveValues = false, role = "firepower") {
    if (!this.modal || this.isSaving) return;
    activeEditors.set(this.modal, this);
    if (!preserveValues) {
      this.currentRole = role;
    }
    this.currentPoint = point || {
      id: "FIRE-01",
      name: t("roles.firepowerPlaceholder", "地面任务输出点"),
      callsign: "",
      longitude: null,
      latitude: null,
      altitude: null,
      strikeRange: "40 km"
    };

    const isRecon = (this.currentRole || role) === "recon";

    if (!preserveValues) {
      this.showSaveError();
      this.mountedMeta.hidden = isRecon;
      this.dispMounted.textContent = this.currentPoint.mountedDeviceId
        ? this.currentPoint.mountedDeviceName || this.currentPoint.mountedDeviceId
        : "未挂载";
      this.mountSection.hidden = !isRecon || !this.reconMount;
      if (!this.mountSection.hidden) {
        this.renderReconMount(this.reconMount.getMountedPoint(this.currentPoint));
      }
      if (this.dispName) {
        this.dispName.textContent = this.currentPoint.name || this.currentPoint.deviceId || "—";
      }
      if (this.dispId) {
        this.dispId.textContent = getPlatformDeviceId(this.currentPoint) || "未绑定设备";
      }
      if (this.dispIp) {
        this.dispIp.textContent = this.currentPoint.deviceIp || this.currentPoint.ip || this.currentPoint.host || "127.0.0.1";
      }
      if (this.dispStatus) {
        // 未收到EdgeAgent转发状态时按离线处理，避免把静态配置或旧快照误报为在线。
        const isOnline = this.currentPoint.online === true;
        const videoInfo = this.currentPoint.videoSources ? ` (${this.currentPoint.videoSources} 路视频源)` : "";
        this.dispStatus.textContent = isOnline ? `● 在线${videoInfo}` : "○ 离线";
        this.dispStatus.className = "modal-meta-val " + (isOnline ? "status-online" : "status-offline");
      }
      const titleEl = document.getElementById("firepower-modal-title");
      const deviceId = getPlatformDeviceId(this.currentPoint) || this.currentPoint.name || "未绑定设备";
      const defaultName = isRecon ? `${t("roles.recon", "监测席")}-${deviceId}` : `${t("roles.firepower", "作业单元")}-${deviceId}`;
      if (this.inputNameLabel) this.inputNameLabel.textContent = isRecon ? t("roles.reconNameLabel", "监测席名称") : t("roles.firepowerNameLabel", "作业单元名称");
      if (this.inputName) this.inputName.placeholder = defaultName;
      if (titleEl) {
        titleEl.textContent = isRecon
          ? `${this.currentPoint.name || defaultName} · ${t("roles.recon", "监测席")}配置`
          : `${this.currentPoint.name || defaultName} · ${t("roles.firepower", "作业单元")}配置`;
      }
      for (const field of this.form.querySelectorAll("[data-operational-point-field]")) {
        field.hidden = isRecon;
        for (const input of field.querySelectorAll("input")) input.disabled = isRecon;
      }
      this.btnSave.textContent = isRecon ? "保存坐标" : "保存配置";
      if (this.inputName) this.inputName.value = this.currentPoint.name || defaultName;
      if (this.inputLon) this.inputLon.value = this.currentPoint.longitude != null ? this.currentPoint.longitude : "";
      if (this.inputLat) this.inputLat.value = this.currentPoint.latitude != null ? this.currentPoint.latitude : "";
      if (this.inputAlt) this.inputAlt.value = this.currentPoint.altitude != null ? this.currentPoint.altitude : "";
      if (this.inputRange) {
        this.inputRange.value = this.currentPoint.strikeRange != null && this.currentPoint.strikeRange !== ""
          ? String(this.currentPoint.strikeRange)
          : (this.currentPoint.maxRangeKm ? `${this.currentPoint.maxRangeKm} km` : "40 km");
      }
      if (this.inputMaxTargets) {
        this.inputMaxTargets.value = this.currentPoint.maxTargets != null ? this.currentPoint.maxTargets : 1;
      }
      if (this.inputIp) {
        this.inputIp.value = this.currentPoint.ip || this.currentPoint.host || "127.0.0.1";
      }
      if (this.inputPort) {
        this.inputPort.value = this.currentPoint.port || 5567;
      }
      if (this.inputDeviceId) {
        this.inputDeviceId.value = getPlatformDeviceId(this.currentPoint);
      }
      if (this.inputBattalion) {
        this.inputBattalion.value = this.currentPoint.battalion || t("org.battalionDefault", "1区");
      }
      if (this.inputCompany) {
        this.inputCompany.value = this.currentPoint.company || t("org.companyDefault", "1组");
      }
      if (this.inputMountedDevice) {
        this.inputMountedDevice.value = this.currentPoint.mountedDeviceId || "";
      }
      if (this.pickedHint) {
        this.pickedHint.hidden = true;
        this.pickedHint.innerHTML = "";
      }
    }

    const canSwitchRole = Boolean(this.changeSimulationRole);
    if (this.btnRoleToFirepower) this.btnRoleToFirepower.textContent = t("roles.asFirepower", "同时作为作业单元");
    if (this.btnRoleToRecon) this.btnRoleToRecon.textContent = t("roles.cancelFirepower", "取消独立作业单元");
    if (this.btnRoleToFirepower) {
      this.btnRoleToFirepower.hidden = !(canSwitchRole && isRecon);
    }
    if (this.btnRoleToRecon) {
      this.btnRoleToRecon.hidden = !(canSwitchRole && !isRecon);
    }

    this.modal.hidden = false;
    this.modal.classList.remove("is-hidden");
    this.modal.style.display = "flex";
  }

  renderReconMount(point) {
    this.mountedTo = point;
    this.mountStatus.textContent = point ? `${point.battalion}·${point.company}·${point.name}` : "空闲未挂载";
    this.mountEdit.textContent = point ? "更改工位" : "挂载工位";
    this.mountRemove.hidden = !point;
    this.mountPicker.hidden = true;
  }

  async updateReconMount(remove) {
    if (activeEditors.get(this.modal) !== this || this.mountPicker.hidden || this.isSaving) return;
    const pointId = remove ? this.mountedTo?.id : this.mountSelect.value;
    if (!pointId) return;
    const deviceId = remove ? "" : getPlatformDeviceId(this.currentPoint) || this.currentPoint.id;
    this.isSaving = true;
    this.mountEdit.disabled = this.mountSelect.disabled = this.mountConfirm.disabled = this.mountRemove.disabled = true;
    this.showSaveError();
    try {
      if (await this.reconMount.mountDevice(pointId, deviceId)) {
        this.renderReconMount(this.reconMount.getMountedPoint(this.currentPoint));
      } else {
        this.showSaveError(remove ? "解绑失败，请重试" : "保存单元挂载失败，请重试");
      }
    } finally {
      this.isSaving = false;
      this.mountEdit.disabled = this.mountSelect.disabled = this.mountRemove.disabled = false;
      this.mountConfirm.disabled = !this.mountSelect.value || this.mountSelect.value === this.mountedTo?.id;
    }
  }

  close() {
    if (!this.modal || this.isSaving) return;
    const editor = activeEditors.get(this.modal);
    if (editor && editor !== this) return;
    activeEditors.delete(this.modal);
    if (this.btnRoleToFirepower) this.btnRoleToFirepower.hidden = true;
    if (this.btnRoleToRecon) this.btnRoleToRecon.hidden = true;
    this.modal.hidden = true;
    this.modal.classList.add("is-hidden");
    this.modal.style.display = "none";
    this.exitPickingHud();
  }

  startPickingPoint() {
    this.isPickingMode = true;
    // 隐藏配置弹窗，露出完整地图供用户点选
    this.modal.hidden = true;
    this.modal.classList.add("is-hidden");
    this.modal.style.display = "none";

    if (this.hud) {
      this.hud.hidden = false;
      this.hud.classList.remove("is-hidden");
      this.hud.style.display = "flex";
    }

    this.viewerManager?.startMapCoordinatePicking(
      coords => {
        if (this.inputLon) this.inputLon.value = coords.longitude.toFixed(6);
        if (this.inputLat) this.inputLat.value = coords.latitude.toFixed(6);
        if (this.inputAlt) this.inputAlt.value = coords.altitude;

        this.exitPickingHud();
        this.open(this.currentPoint, true);

        if (this.pickedHint) {
          this.pickedHint.hidden = false;
          this.pickedHint.innerHTML = `<span>✔ 已从地图点选拾取坐标: <strong>${coords.longitude.toFixed(6)}°E, ${coords.latitude.toFixed(6)}°N</strong> (高度: ${coords.altitude} m)</span>`;
        }
      },
      () => {
        this.exitPickingHud();
        this.open(this.currentPoint, true);
      }
    );
  }

  cancelPickingPoint() {
    this.viewerManager?.cancelMapCoordinatePicking();
    this.exitPickingHud();
    this.open(this.currentPoint, true);
  }

  exitPickingHud() {
    this.isPickingMode = false;
    if (this.hud) {
      this.hud.hidden = true;
      this.hud.classList.add("is-hidden");
      this.hud.style.display = "none";
    }
  }

  pickMapCenter() {
    const viewer = this.viewerManager?.viewer;
    if (!viewer) return;
    try {
      const canvas = viewer.scene.canvas;
      const centerPixel = new Cesium.Cartesian2(canvas.clientWidth / 2, canvas.clientHeight / 2);
      const ray = viewer.camera.getPickRay(centerPixel);
      const cartesian = viewer.scene.globe.pick(ray, viewer.scene);
      if (cartesian) {
        const carto = Cesium.Cartographic.fromCartesian(cartesian);
        if (this.inputLon) this.inputLon.value = Cesium.Math.toDegrees(carto.longitude).toFixed(6);
        if (this.inputLat) this.inputLat.value = Cesium.Math.toDegrees(carto.latitude).toFixed(6);
        if (this.inputAlt) this.inputAlt.value = Math.max(0, Math.round(carto.height || 0));
      } else {
        const carto = viewer.camera.positionCartographic;
        if (this.inputLon) this.inputLon.value = Cesium.Math.toDegrees(carto.longitude).toFixed(6);
        if (this.inputLat) this.inputLat.value = Cesium.Math.toDegrees(carto.latitude).toFixed(6);
        if (this.inputAlt) this.inputAlt.value = 0;
      }
      if (this.pickedHint) {
        this.pickedHint.hidden = false;
        this.pickedHint.innerHTML = `<span>✔ 已获取当前视心坐标: <strong>${this.inputLon.value}°E, ${this.inputLat.value}°N</strong> (高度: ${this.inputAlt.value} m)</span>`;
      }
    } catch (err) {
      console.warn("[FirepowerConfig] 拾取地图视心坐标失败:", err);
    }
  }

  showSaveError(message = "") {
    this.saveError.textContent = message;
    this.saveError.hidden = !message;
  }

  async handleSubmit() {
    if (this.isSaving) return;
    this.showSaveError();
    const isRecon = this.currentRole === "recon";
    const rawLon = parseFloat(this.inputLon.value);
    const rawLat = parseFloat(this.inputLat.value);
    const rawAlt = parseFloat(this.inputAlt.value);
    const range = this.inputRange.value.trim() || "40 km";
    const name = this.inputName.value.trim() || t("roles.firepowerPlaceholder", "地面任务输出点");
    const maxTargets = Math.max(1, Math.min(50, parseInt(this.inputMaxTargets?.value, 10) || 1));
    const ip = this.inputIp?.value?.trim() || "127.0.0.1";
    const port = Math.max(1, Math.min(65535, parseInt(this.inputPort?.value, 10) || 5567));
    const deviceId = this.saveSimulationCoordinates
      ? (this.inputDeviceId?.value?.trim() || getPlatformDeviceId(this.currentPoint))
      : undefined;

    if (!isRecon && (isNaN(rawLon) || rawLon < -180 || rawLon > 180)) {
      alert("请输入有效的经度数值 (-180° ~ 180°)");
      this.inputLon.focus();
      return;
    }
    if (!isRecon && (isNaN(rawLat) || rawLat < -90 || rawLat > 90)) {
      alert("请输入有效的纬度数值 (-90° ~ 90°)");
      this.inputLat.focus();
      return;
    }

    const battalion = this.inputBattalion?.value?.trim() || t("org.battalionDefault", "1区");
    const company = this.inputCompany?.value?.trim() || t("org.companyDefault", "1组");
    const mountedDeviceId = this.inputMountedDevice?.value.trim() ?? this.currentPoint?.mountedDeviceId ?? "";

    const updated = {
      ...(this.currentPoint || {}),
      id: this.currentPoint?.id || "FIRE-01",
      name,
      battalion,
      company,
      mountedDeviceId,
      callsign: "",
      longitude: isNaN(rawLon) ? (this.currentPoint?.longitude ?? null) : Number(rawLon.toFixed(6)),
      latitude: isNaN(rawLat) ? (this.currentPoint?.latitude ?? null) : Number(rawLat.toFixed(6)),
      altitude: isNaN(rawAlt) ? (this.currentPoint?.altitude ?? 0) : Number(rawAlt.toFixed(1)),
      strikeRange: range,
      maxTargets,
      ip,
      port,
      ...(deviceId ? { deviceId } : {})
    };

    this.isSaving = true;
    this.form.inert = true;
    this.btnSave.disabled = true;
    this.btnClose.disabled = true;
    try {
      const { longitude, latitude, altitude, strikeRange, maxTargets, ip, port } = updated;
      const coordinates = isRecon
        ? (Number.isNaN(rawLon) && Number.isNaN(rawLat) && Number.isNaN(rawAlt) ? null : { longitude, latitude, altitude })
        : { longitude, latitude, altitude, strikeRange, maxTargets, ip, port, battalion, company, mountedDeviceId };
      await this.saveSimulationCoordinates(this.currentPoint, coordinates, updated.name);
    } catch (error) {
      this.showSaveError(error.message || "未收到保存确认，请刷新后重试。");
      return;
    } finally {
      this.isSaving = false;
      this.form.inert = false;
      this.btnSave.disabled = false;
      this.btnClose.disabled = false;
    }

    this.close();
  }
}

export { StationConfigModal as FirepowerConfigModal };
