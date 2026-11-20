/**
 * 协同作业整备与组织架构管理弹窗 (完整树状拓扑图 + 片区分组架构设置 + 在线设备挂载)
 */
import { escapeHtml } from "../core/html.js";
import { eventBus } from "../core/event-bus.js";
import { t } from "../core/terms.js";

function parseRangeMeters(rangeVal) {
  if (typeof rangeVal === "number") return rangeVal > 500 ? rangeVal : rangeVal * 1000;
  if (typeof rangeVal === "string") {
    const match = rangeVal.match(/(\d+(?:\.\d+)?)\s*(km|千米|m|米)?/i);
    if (match) {
      const num = parseFloat(match[1]);
      const unit = (match[2] || "").toLowerCase();
      return unit.includes("m") && !unit.includes("k") && !unit.includes("千") ? num : num * 1000;
    }
  }
  return 40000;
}

export class AffiliationModal {
  constructor(viewerManager, getDeviceManager) {
    this.viewerManager = viewerManager;
    this.getDeviceManager = getDeviceManager;
    this.modal = document.getElementById("page-firepower-affiliation") || document.getElementById("firepower-affiliation-modal");
    this.treeContainer = document.getElementById("affiliation-tree-canvas");
    this.reconPoolContainer = document.getElementById("affiliation-recon-pool-list");
    this.statTotal = document.getElementById("tree-stat-total");
    this.statOnline = document.getElementById("tree-stat-online");
    this.statOffline = document.getElementById("tree-stat-offline");
    this.statMulticast = document.getElementById("tree-stat-multicast");

    this.btnClose = document.getElementById("btn-close-affiliation-modal");
    this.btnFootClose = document.getElementById("btn-affiliation-close-foot");
    this.btnAddPoint = document.getElementById("btn-tree-add-point");
    this.btnOrgScale = document.getElementById("btn-tree-org-scale");
    this.btnAutoMount = document.getElementById("btn-tree-auto-mount");
    this.btnResetDefault = document.getElementById("btn-tree-reset-default");

    // 编辑抽屉元素
    this.drawer = document.getElementById("affiliation-edit-drawer");
    this.drawerTitle = document.getElementById("tree-drawer-title");
    this.drawerForm = document.getElementById("form-tree-drawer-edit");
    this.btnCancelDrawer = document.getElementById("btn-cancel-tree-drawer");
    this.drawerPointId = document.getElementById("tree-edit-point-id");
    this.inputBattalion = document.getElementById("tree-edit-battalion");
    this.inputCompany = document.getElementById("tree-edit-company");
    this.inputName = document.getElementById("tree-edit-name");
    this.selectMountDevice = document.getElementById("tree-edit-mounted-device");
    this.inputLon = document.getElementById("tree-edit-lon");
    this.inputLat = document.getElementById("tree-edit-lat");
    this.inputAlt = document.getElementById("tree-edit-alt");
    this.inputRange = document.getElementById("tree-edit-range");
    this.inputMaxTargets = document.getElementById("tree-edit-max-targets");
    this.inputIp = document.getElementById("tree-edit-ip");
    this.inputPort = document.getElementById("tree-edit-port");
    this.btnPickCenter = document.getElementById("btn-tree-pick-center");
    this.btnPickPoint = document.getElementById("btn-tree-pick-point");
    this.btnCancelDrawerBtn = document.getElementById("btn-cancel-tree-drawer-btn");
    this.pickedHint = document.getElementById("tree-edit-picked-hint");
    this.hud = document.getElementById("map-picking-hud");
    this.btnCancelPicking = document.getElementById("btn-cancel-picking");

    // 组织架构规模设置抽屉
    this.orgModal = document.getElementById("affiliation-org-modal");
    this.btnCloseOrgModal = document.getElementById("btn-close-org-modal");
    this.btnFinishOrgModal = document.getElementById("btn-finish-org-modal");
    this.btnApplyOrgScale = document.getElementById("btn-apply-org-scale");
    this.btnResetOrgDefault = document.getElementById("btn-reset-org-default");
    this.inputScaleBattalions = document.getElementById("input-scale-battalions");
    this.inputScaleCompanies = document.getElementById("input-scale-companies");
    this.orgManageList = document.getElementById("org-manage-tree-list");
    this.inputNewBatName = document.getElementById("input-new-battalion-name");
    this.btnAddBat = document.getElementById("btn-add-new-battalion");

    this.editingPointId = null;
    this.isPickingMode = false;
    this.savedDrawerState = null;

    this.collapsedKeys = new Set();
    this.checkedPointIds = new Set();
    this.customRootName = localStorage.getItem("org_custom_root_name") || "";
    this.hasInitializedChecks = false;

    this.initEvents();
  }

  initEvents() {
    const returnToTactical = () => {
      this.close();
      eventBus.emit("page:switch", "tactical");
    };
    this.btnClose?.addEventListener("click", returnToTactical);
    this.btnFootClose?.addEventListener("click", returnToTactical);
    this.modal?.addEventListener("click", e => {
      if (e.target === this.modal) returnToTactical();
    });

    window.addEventListener("keydown", e => {
      if (e.key === "Escape") {
        if (this.isPickingMode) {
          this.cancelPickingPoint();
          return;
        }
        if (this.isOpen()) {
          if (this.orgModal && !this.orgModal.hidden && this.orgModal.style.display !== "none") {
            this.closeOrgSettings();
          } else if (this.drawer && !this.drawer.hidden && this.drawer.style.display !== "none") {
            this.closeDrawer();
          } else {
            returnToTactical();
          }
        }
      }
    });

    this.btnAddPoint?.addEventListener("click", () => {
      this.openDrawerForNew();
    });

    this.btnOrgScale?.addEventListener("click", () => {
      this.openOrgSettings();
    });

    this.btnAutoMount?.addEventListener("click", async () => {
      const dm = this.getDeviceManager();
      if (dm?.autoMountAvailableDevices) {
        await dm.autoMountAvailableDevices();
        this.render();
      }
    });

    this.btnResetDefault?.addEventListener("click", async () => {
      if (confirm(t("org.scaleResetConfirm", "确定重置为默认标准组织架构（2个片区，各2个组）吗？"))) {
        const dm = this.getDeviceManager();
        if (dm?.resetToDefaultAffiliation) {
          await dm.resetToDefaultAffiliation();
          this.render();
        }
      }
    });

    this.btnCancelDrawer?.addEventListener("click", () => {
      this.closeDrawer();
    });

    this.btnCancelDrawerBtn?.addEventListener("click", () => {
      this.closeDrawer();
    });

    this.btnPickCenter?.addEventListener("click", () => {
      this.pickMapCenter();
    });

    this.btnPickPoint?.addEventListener("click", () => {
      this.startPickingPoint();
    });

    this.btnCancelPicking?.addEventListener("click", () => {
      if (this.isPickingMode) {
        this.cancelPickingPoint();
      }
    });

    this.drawerForm?.addEventListener("submit", async e => {
      e.preventDefault();
      await this.handleSaveDrawer();
    });

    // 架构规模设置相关事件
    this.btnCloseOrgModal?.addEventListener("click", () => this.closeOrgSettings());
    this.btnFinishOrgModal?.addEventListener("click", () => this.closeOrgSettings());
    this.btnApplyOrgScale?.addEventListener("click", () => this.handleApplyOrgScale());
    this.btnResetOrgDefault?.addEventListener("click", () => this.handleResetOrgDefault());
    this.btnAddBat?.addEventListener("click", () => this.handleAddBattalion());

    // 编辑抽屉片区分组下拉联动
    this.inputBattalion?.addEventListener("change", () => {
      const dm = this.getDeviceManager();
      const orgRaw = dm?.getOrgStructure ? dm.getOrgStructure() : [];
      const battalions = Array.isArray(orgRaw) ? orgRaw : (orgRaw?.battalions || []);
      const currentBat = this.inputBattalion.value;
      this.updateCompanySelectOptions(currentBat, battalions);
    });
    this.selectMountDevice?.addEventListener("change", () => this.syncMountedIp());

    eventBus.on("firepower:openAffiliationModal", () => {
      this.open();
    });

    eventBus.on("firepower:affiliationChanged", () => {
      if (this.isOpen()) {
        this.render();
      }
    });
  }

  isOpen() {
    return Boolean(this.modal && !this.modal.hidden && this.modal.style.display !== "none");
  }

  open(keepDrawer = false) {
    if (!this.modal) return;
    this.modal.hidden = false;
    this.modal.classList.remove("is-hidden");
    this.modal.style.display = "flex";
    if (!keepDrawer) {
      this.closeDrawer();
      this.closeOrgSettings();
    }
    this.render();
  }

  close() {
    if (!this.modal) return;
    this.exitPickingHud();
    this.modal.hidden = true;
    this.modal.classList.add("is-hidden");
    this.modal.style.display = "none";
    this.closeDrawer();
    this.closeOrgSettings();
  }

  // ===== 组织架构规模设置抽屉 =====
  openOrgSettings() {
    if (!this.orgModal) return;
    this.closeDrawer();
    const dm = this.getDeviceManager();
    const orgRaw = dm?.getOrgStructure ? dm.getOrgStructure() : [];
    const battalions = Array.isArray(orgRaw) ? orgRaw : (orgRaw?.battalions || []);

    if (this.inputScaleBattalions) {
      this.inputScaleBattalions.value = Math.max(1, battalions.length || 2);
    }
    if (this.inputScaleCompanies) {
      const avgComps = battalions.length > 0
        ? Math.round(battalions.reduce((sum, b) => sum + (b.companies ? b.companies.length : 0), 0) / battalions.length)
        : 2;
      this.inputScaleCompanies.value = Math.max(1, avgComps || 2);
    }

    this.renderOrgManageList();
    this.orgModal.hidden = false;
    this.orgModal.classList.remove("is-hidden");
    this.orgModal.style.display = "flex";
  }

  closeOrgSettings() {
    if (!this.orgModal) return;
    this.orgModal.hidden = true;
    this.orgModal.classList.add("is-hidden");
    this.orgModal.style.display = "none";
  }

  handleApplyOrgScale() {
    const dm = this.getDeviceManager();
    if (!dm?.setOrgScale) return;
    const bats = parseInt(this.inputScaleBattalions?.value, 10) || 2;
    const comps = parseInt(this.inputScaleCompanies?.value, 10) || 2;
    dm.setOrgScale(bats, comps);
    this.renderOrgManageList();
    this.render();
  }

  handleResetOrgDefault() {
    const dm = this.getDeviceManager();
    if (!dm?.setOrgScale) return;
    dm.setOrgScale(2, 2);
    if (this.inputScaleBattalions) this.inputScaleBattalions.value = 2;
    if (this.inputScaleCompanies) this.inputScaleCompanies.value = 2;
    this.renderOrgManageList();
    this.render();
  }

  handleAddBattalion() {
    const dm = this.getDeviceManager();
    if (!dm?.addBattalion) return;
    const name = this.inputNewBatName?.value?.trim();
    if (!name) return;
    dm.addBattalion(name);
    if (this.inputNewBatName) this.inputNewBatName.value = "";
    this.renderOrgManageList();
    this.render();
  }

  renderOrgManageList() {
    if (!this.orgManageList) return;
    const dm = this.getDeviceManager();
    const orgRaw = dm?.getOrgStructure ? dm.getOrgStructure() : [];
    const battalions = Array.isArray(orgRaw) ? orgRaw : (orgRaw?.battalions || []);
    const tree = dm?.getAffiliationTree ? dm.getAffiliationTree() : null;

    if (!battalions.length) {
      this.orgManageList.innerHTML = `<div class="tactical-empty-box compact"><span class="empty-text">当前暂未配置片区架构</span></div>`;
      return;
    }

    this.orgManageList.innerHTML = battalions.map(bat => {
      const batTree = tree?.battalions?.find(b => b.name === bat.name);
      const pointsInBat = batTree?.totalPoints || 0;
      const comps = bat.companies || [];

      return `
        <div class="org-manage-bat-item" data-battalion="${escapeHtml(bat.name)}">
          <div class="bat-manage-header">
            <div class="bat-manage-title">
              <span class="status-dot-point online"></span>
              <strong>${escapeHtml(bat.name)}</strong>
              <span class="bat-points-count">(${pointsInBat}${t("roles.firepower", "个作业单元")})</span>
            </div>
            <div class="bat-manage-actions">
              <button type="button" class="btn btn-xs btn-secondary btn-org-add-comp" data-battalion="${escapeHtml(bat.name)}">+ ${t("org.company", "组")}</button>
              ${pointsInBat === 0 ? `
                <button type="button" class="btn btn-xs btn-secondary-danger btn-org-del-bat" data-battalion="${escapeHtml(bat.name)}" title="删除空片区">✕</button>
              ` : ''}
            </div>
          </div>
          <div class="bat-comps-chip-list">
            ${comps.map(comp => {
              const compTree = batTree?.companies?.find(c => c.name === comp);
              const pointsInComp = compTree?.points?.length || 0;
              return `
                <span class="comp-chip">
                  <span class="comp-chip-name">${escapeHtml(comp)}</span>
                  <span class="comp-chip-count">${pointsInComp}点</span>
                  ${pointsInComp === 0 ? `
                    <button type="button" class="comp-chip-del btn-org-del-comp" data-battalion="${escapeHtml(bat.name)}" data-company="${escapeHtml(comp)}" title="删除空组">✕</button>
                  ` : ''}
                </span>
              `;
            }).join('')}
            <button type="button" class="comp-chip-add btn-org-add-comp-inline" data-battalion="${escapeHtml(bat.name)}" title="新增组">+</button>
          </div>
        </div>
      `;
    }).join('');

    // 绑定片区分组管理事件
    this.orgManageList.querySelectorAll(".btn-org-add-comp, .comp-chip-add").forEach(btn => {
      btn.addEventListener("click", () => {
        const batName = btn.getAttribute("data-battalion");
        const compName = prompt(`请输入要在 [${batName}] 下新增的组名称 (如: 3组):`);
        if (compName?.trim() && dm?.addCompany) {
          dm.addCompany(batName, compName.trim());
          this.renderOrgManageList();
          this.render();
        }
      });
    });

    this.orgManageList.querySelectorAll(".btn-org-del-comp").forEach(btn => {
      btn.addEventListener("click", () => {
        const batName = btn.getAttribute("data-battalion");
        const compName = btn.getAttribute("data-company");
        if (confirm(`确定删除 [${batName}] 下的 [${compName}] 吗？`)) {
          dm?.removeCompany?.(batName, compName);
          this.renderOrgManageList();
          this.render();
        }
      });
    });

    this.orgManageList.querySelectorAll(".btn-org-del-bat").forEach(btn => {
      btn.addEventListener("click", () => {
        const batName = btn.getAttribute("data-battalion");
        if (confirm(`确定删除空片区 [${batName}] 吗？`)) {
          dm?.removeBattalion?.(batName);
          this.renderOrgManageList();
          this.render();
        }
      });
    });
  }

  // ===== 编辑抽屉片区分组下拉填充与联动 =====
  populateOrgSelects(selectedBat = t("org.battalionDefault", "1区"), selectedComp = t("org.companyDefault", "1组")) {
    const dm = this.getDeviceManager();
    const orgRaw = dm?.getOrgStructure ? dm.getOrgStructure() : null;
    const battalions = Array.isArray(orgRaw) ? orgRaw : (orgRaw?.battalions || [
      { name: "1" + t("org.battalion", "区"), companies: ["1" + t("org.company", "组"), "2" + t("org.company", "组")] },
      { name: "2" + t("org.battalion", "区"), companies: ["1" + t("org.company", "组"), "2" + t("org.company", "组")] }
    ]);

    if (this.inputBattalion) {
      this.inputBattalion.innerHTML = battalions.map(b => `
        <option value="${escapeHtml(b.name)}" ${b.name === selectedBat ? 'selected' : ''}>${escapeHtml(b.name)}</option>
      `).join('');

      // 若传入的 selectedBat 不在列表中，追加
      if (selectedBat && !battalions.some(b => b.name === selectedBat)) {
        const opt = document.createElement("option");
        opt.value = selectedBat;
        opt.textContent = selectedBat;
        opt.selected = true;
        this.inputBattalion.appendChild(opt);
      }
    }

    const currentBat = this.inputBattalion?.value || selectedBat;
    this.updateCompanySelectOptions(currentBat, battalions, selectedComp);
  }

  updateCompanySelectOptions(battalionName, battalions, selectedComp = null) {
    if (!this.inputCompany) return;
    const batList = Array.isArray(battalions) ? battalions : (battalions?.battalions || []);
    const batObj = batList.find(b => b.name === battalionName);
    const comps = batObj?.companies || ["1" + t("org.company", "组"), "2" + t("org.company", "组")];

    this.inputCompany.innerHTML = comps.map(c => `
      <option value="${escapeHtml(c)}" ${c === selectedComp ? 'selected' : ''}>${escapeHtml(c)}</option>
    `).join('');

    if (selectedComp && !comps.includes(selectedComp)) {
      const opt = document.createElement("option");
      opt.value = selectedComp;
      opt.textContent = selectedComp;
      opt.selected = true;
      this.inputCompany.appendChild(opt);
    }
  }

  // ===== 坐标拾取相关 =====
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
        this.pickedHint.style.display = "flex";
        this.pickedHint.innerHTML = `<span>✔ 已获取当前视心坐标: <strong>${this.inputLon.value}°E, ${this.inputLat.value}°N</strong> (高度: ${this.inputAlt.value} m)</span>`;
      }
    } catch (err) {
      console.warn("[Affiliation] 拾取视心失败:", err);
    }
  }

  startPickingPoint() {
    this.isPickingMode = true;
    this.savedDrawerState = {
      editingPointId: this.editingPointId,
      drawerTitle: this.drawerTitle ? this.drawerTitle.textContent : "",
      battalion: this.inputBattalion?.value || "",
      company: this.inputCompany?.value || "",
      name: this.inputName?.value || "",
      mountedDeviceId: this.selectMountDevice?.value || "",
      lon: this.inputLon?.value || "",
      lat: this.inputLat?.value || "",
      alt: this.inputAlt?.value || "",
      range: this.inputRange?.value || "",
      maxTargets: this.inputMaxTargets?.value || "",
      ip: (this.inputIp?.readOnly ? this.unmountedDrawerIp : this.inputIp?.value) || "",
      port: this.inputPort?.value || ""
    };

    if (this.drawer) {
      this.drawer.hidden = true;
      this.drawer.style.display = "none";
    }

    eventBus.emit("page:switch", "tactical");

    if (this.hud) {
      this.hud.hidden = false;
      this.hud.classList.remove("is-hidden");
      this.hud.style.display = "flex";
    }

    this.viewerManager?.startMapCoordinatePicking(
      coords => {
        this.exitPickingHud();
        eventBus.emit("page:switch", "firepower", { keepDrawer: true });
        this.restoreDrawerState(this.savedDrawerState, coords);
      },
      () => {
        this.exitPickingHud();
        eventBus.emit("page:switch", "firepower", { keepDrawer: true });
        this.restoreDrawerState(this.savedDrawerState, null);
      }
    );
  }

  cancelPickingPoint() {
    this.viewerManager?.cancelMapCoordinatePicking(false);
    this.exitPickingHud();
    eventBus.emit("page:switch", "firepower", { keepDrawer: true });
    this.restoreDrawerState(this.savedDrawerState, null);
  }

  exitPickingHud() {
    this.isPickingMode = false;
    if (this.hud) {
      this.hud.hidden = true;
      this.hud.classList.add("is-hidden");
      this.hud.style.display = "none";
    }
    this.viewerManager?.cancelMapCoordinatePicking(false);
  }

  restoreDrawerState(savedState, coords) {
    if (!savedState) return;
    this.editingPointId = savedState.editingPointId;
    if (this.drawerTitle) this.drawerTitle.textContent = savedState.drawerTitle;
    if (this.drawerPointId) this.drawerPointId.value = savedState.editingPointId || "";

    this.populateOrgSelects(savedState.battalion, savedState.company);
    if (this.inputName) this.inputName.value = savedState.name;
    if (this.inputRange) this.inputRange.value = savedState.range;
    if (this.inputMaxTargets) this.inputMaxTargets.value = savedState.maxTargets;
    if (this.inputIp) {
      this.inputIp.readOnly = false;
      this.inputIp.value = savedState.ip;
    }
    if (this.inputPort) this.inputPort.value = savedState.port;

    const dm = this.getDeviceManager();
    const onlineRecons = dm?.getOnlineReconDevices ? dm.getOnlineReconDevices() : [];
    this.populateMountedSelect(savedState.mountedDeviceId, onlineRecons);
    this.syncMountedIp();

    if (coords) {
      if (this.inputLon) this.inputLon.value = coords.longitude.toFixed(6);
      if (this.inputLat) this.inputLat.value = coords.latitude.toFixed(6);
      if (this.inputAlt) this.inputAlt.value = coords.altitude;
      if (this.pickedHint) {
        this.pickedHint.hidden = false;
        this.pickedHint.style.display = "flex";
        this.pickedHint.innerHTML = `<span>✔ 已从地图点选拾取坐标: <strong>${coords.longitude.toFixed(6)}°E, ${coords.latitude.toFixed(6)}°N</strong> (高度: ${coords.altitude} m)</span>`;
      }
    } else {
      if (this.inputLon) this.inputLon.value = savedState.lon;
      if (this.inputLat) this.inputLat.value = savedState.lat;
      if (this.inputAlt) this.inputAlt.value = savedState.alt;
    }

    if (this.drawer) {
      this.drawer.hidden = false;
      this.drawer.style.display = "flex";
    }
  }

  // ===== 渲染整备拓扑图与侧边栏 =====
  render() {
    const dm = this.getDeviceManager();
    if (!dm) return;

    const tree = dm.getAffiliationTree ? dm.getAffiliationTree() : null;
    const onlineRecons = dm.getOnlineReconDevices ? dm.getOnlineReconDevices() : [];
    if (!tree) return;

    if (this.statTotal) this.statTotal.textContent = String(tree.totalPoints);
    if (this.statOnline) this.statOnline.textContent = String(tree.onlinePoints);
    if (this.statOffline) this.statOffline.textContent = String(tree.totalPoints - tree.onlinePoints);
    if (this.statMulticast) {
      const activeMulticast = onlineRecons.filter(u => u.online).length;
      this.statMulticast.textContent = `${activeMulticast} 台在线`;
    }

    this.renderTreeCanvas(tree, onlineRecons);
    this.renderReconPool(onlineRecons, tree.allPoints);
  }

  updateCheckboxCascadeStates(tree) {
    if (!this.treeContainer) return;
    const allPoints = tree.allPoints || [];
    const checkedCount = allPoints.filter(p => this.checkedPointIds.has(p.id)).length;
    const totalCount = allPoints.length;

    // 更新各组
    for (const bat of tree.battalions) {
      for (const comp of bat.companies) {
        const compPoints = comp.points || [];
        const compChecked = compPoints.filter(p => this.checkedPointIds.has(p.id)).length;
        const compCb = this.treeContainer.querySelector(`.dir-check-comp[data-bat="${CSS.escape(bat.name)}"][data-comp="${CSS.escape(comp.name)}"]`);
        if (compCb) {
          compCb.checked = compPoints.length > 0 && compChecked === compPoints.length;
          compCb.indeterminate = compChecked > 0 && compChecked < compPoints.length;
        }
      }

      // 更新片区
      const batPoints = [];
      bat.companies.forEach(c => batPoints.push(...c.points));
      const batChecked = batPoints.filter(p => this.checkedPointIds.has(p.id)).length;
      const batCb = this.treeContainer.querySelector(`.dir-check-bat[data-bat="${CSS.escape(bat.name)}"]`);
      if (batCb) {
        batCb.checked = batPoints.length > 0 && batChecked === batPoints.length;
        batCb.indeterminate = batChecked > 0 && batChecked < batPoints.length;
      }
    }

    // 更新根节点
    const rootCb = this.treeContainer.querySelector(`.dir-check-root`);
    if (rootCb) {
      rootCb.checked = totalCount > 0 && checkedCount === totalCount;
      rootCb.indeterminate = checkedCount > 0 && checkedCount < totalCount;
    }

    // 更新底部文字
    const summaryEl = document.getElementById("dir-checked-summary");
    if (summaryEl) summaryEl.textContent = String(checkedCount);
  }

  renderTreeCanvas(tree, onlineRecons) {
    if (!this.treeContainer) return;
    const dm = this.getDeviceManager();

    const allPoints = tree.allPoints || [];
    if (!this.hasInitializedChecks && allPoints.length > 0) {
      this.hasInitializedChecks = true;
      for (const pt of allPoints) {
        this.checkedPointIds.add(pt.id);
      }
    }

    const rootName = this.customRootName || tree.root || "业务调度中心";
    const isRootCollapsed = this.collapsedKeys.has("__root__");
    const totalPointsCount = allPoints.length;
    const checkedPointsCount = allPoints.filter(p => this.checkedPointIds.has(p.id)).length;

    this.treeContainer.innerHTML = `
      <div class="dir-tree-window">
        <!-- 窗口标题栏: 通信名录列表 (参考图 1) -->
        <div class="dir-window-titlebar">
          <div class="dir-title-left">
            <svg class="dir-window-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
              <path d="M4 4h16c1.1 0 2 .9 2 2v12c0 1.1-.9 2-2 2H4c-1.1 0-2-.9-2-2V6c0-1.1.9-2 2-2z"/>
              <polyline points="22,6 12,13 2,6"/>
            </svg>
            <strong class="dir-title-text" id="dir-window-main-title">通信名录列表</strong>
            <span class="dir-title-badge">${tree.totalPoints} 单元 (${tree.onlinePoints} 在线就绪)</span>
          </div>
          <div class="dir-title-actions">
            <button type="button" class="btn-dir-win-close" id="btn-dir-win-close" title="关闭并返回综合态势">✕</button>
          </div>
        </div>

        <!-- 工具栏 -->
        <div class="dir-window-toolbar">
          <div class="toolbar-left-group">
            <button type="button" class="btn btn-xs btn-tactical-tool" id="btn-dir-expand-all" title="展开全部名录">
              <span>⊞ 全部展开</span>
            </button>
            <button type="button" class="btn btn-xs btn-tactical-tool" id="btn-dir-collapse-all" title="折叠全部名录">
              <span>⊟ 全部折叠</span>
            </button>
            <span class="toolbar-divider">|</span>
            <button type="button" class="btn btn-xs btn-tactical-tool" id="btn-dir-select-all" title="勾选所有作业单元">
              <span>☑ 全选</span>
            </button>
            <button type="button" class="btn btn-xs btn-tactical-tool" id="btn-dir-deselect-all" title="取消所有勾选">
              <span>☐ 全不选</span>
            </button>
          </div>
          <div class="toolbar-right-group">
            <button type="button" class="btn btn-xs btn-tactical-tool highlight-tool" id="btn-dir-add-point" title="新增作业单元">
              <span>+ 新增单元</span>
            </button>
            <button type="button" class="btn btn-xs btn-tactical-tool" id="btn-dir-add-bat" title="新增片区">
              <span>+ 添加片区</span>
            </button>
            <button type="button" class="btn btn-xs btn-secondary" id="btn-dir-org-scale" title="片区分组规模配置">
              <span>⚙ 架构设置</span>
            </button>
          </div>
        </div>

        <!-- 树形名录滚动容器 -->
        <div class="dir-tree-container">
          <div class="dir-tree-root-branch ${isRootCollapsed ? 'is-collapsed' : ''}">
            <!-- 根节点 (中枢/集群) -->
            <div class="dir-node-row dir-root-row" data-level="root">
              <button type="button" class="dir-expander" data-toggle="__root__" title="展开/收起根节点">
                ${isRootCollapsed ? '+' : '-'}
              </button>
              <input type="checkbox" class="dir-checkbox dir-check-root" data-role="root">
              <div class="dir-node-label">
                <span class="dir-node-icon">🌐</span>
                <strong class="dir-root-name" title="点击重命名">${escapeHtml(rootName)}</strong>
                <span class="dir-node-badge root-badge">中枢</span>
              </div>
              <div class="dir-node-meta">
                <span>下辖 ${tree.battalions.length} 个片区 / 共 ${tree.totalPoints} 个作业单元 (${tree.onlinePoints} 在线就绪)</span>
              </div>
              <div class="dir-node-actions">
                <button type="button" class="btn btn-xs btn-tactical-tool btn-dir-rename-root" title="自定义集群中枢名称">✎ 重命名</button>
                <button type="button" class="btn btn-xs btn-secondary btn-dir-add-bat-inline" title="快速新增片区">+ 片区</button>
              </div>
            </div>

            <!-- 片区列表 -->
            <ul class="dir-tree-list dir-battalions-list" ${isRootCollapsed ? 'style="display:none;"' : ''}>
              ${tree.battalions.map(bat => {
                const batKey = `bat:${bat.name}`;
                const isBatCollapsed = this.collapsedKeys.has(batKey);

                return `
                  <li class="dir-tree-node dir-bat-node ${isBatCollapsed ? 'is-collapsed' : ''}" data-bat="${escapeHtml(bat.name)}">
                    <div class="dir-node-row dir-bat-row" data-level="bat">
                      <button type="button" class="dir-expander" data-toggle="${batKey}">
                        ${isBatCollapsed ? '+' : '-'}
                      </button>
                      <input type="checkbox" class="dir-checkbox dir-check-bat" data-role="bat" data-bat="${escapeHtml(bat.name)}">
                      <div class="dir-node-label">
                        <span class="dir-node-icon">📁</span>
                        <strong>${escapeHtml(bat.name)}</strong>
                      </div>
                      <div class="dir-node-meta">
                        <span>${bat.companies.length} 组 / ${bat.totalPoints} 单元 (${bat.onlinePoints} 在线)</span>
                      </div>
                      <div class="dir-node-actions">
                        <button type="button" class="btn btn-xs btn-secondary btn-dir-add-comp" data-bat="${escapeHtml(bat.name)}" title="在此片区下新增组">+ 组</button>
                        ${bat.totalPoints === 0 ? `
                          <button type="button" class="btn btn-xs btn-tactical-tool btn-dir-del-bat" data-bat="${escapeHtml(bat.name)}" title="删除空片区">✕ 删除</button>
                        ` : ''}
                      </div>
                    </div>

                    <!-- 组列表 -->
                    <ul class="dir-tree-list dir-companies-list" ${isBatCollapsed ? 'style="display:none;"' : ''}>
                      ${bat.companies.map(comp => {
                        const compKey = `comp:${bat.name}:${comp.name}`;
                        const isCompCollapsed = this.collapsedKeys.has(compKey);
                        const compTotal = comp.points.length;

                        return `
                          <li class="dir-tree-node dir-comp-node ${isCompCollapsed ? 'is-collapsed' : ''}" data-comp="${escapeHtml(comp.name)}">
                            <div class="dir-node-row dir-comp-row" data-level="comp">
                              <button type="button" class="dir-expander ${compTotal === 0 ? 'empty-placeholder' : ''}" data-toggle="${compKey}">
                                ${isCompCollapsed ? '+' : '-'}
                              </button>
                              <input type="checkbox" class="dir-checkbox dir-check-comp" data-role="comp" data-bat="${escapeHtml(bat.name)}" data-comp="${escapeHtml(comp.name)}">
                              <div class="dir-node-label">
                                <span class="dir-node-icon">📂</span>
                                <strong>${escapeHtml(comp.name)}</strong>
                              </div>
                              <div class="dir-node-meta">
                                <span>${compTotal} 单元</span>
                              </div>
                              <div class="dir-node-actions">
                                <button type="button" class="btn btn-xs btn-tactical-tool highlight-tool btn-dir-add-point-to-comp" data-bat="${escapeHtml(bat.name)}" data-comp="${escapeHtml(comp.name)}" title="在此组新增作业单元">+ 单元</button>
                                ${compTotal === 0 ? `
                                  <button type="button" class="btn btn-xs btn-tactical-tool btn-dir-del-comp" data-bat="${escapeHtml(bat.name)}" data-comp="${escapeHtml(comp.name)}" title="删除空组">✕</button>
                                ` : ''}
                              </div>
                            </div>

                            <!-- 作业单元列表 -->
                            <ul class="dir-tree-list dir-points-list" ${isCompCollapsed ? 'style="display:none;"' : ''}>
                              ${compTotal === 0 ? `
                                <li class="dir-tree-node dir-empty-comp-node">
                                  <span class="dir-empty-text">该组暂无作业单元</span>
                                  <button type="button" class="btn btn-xs btn-tactical-tool highlight-tool btn-dir-add-point-to-comp" data-bat="${escapeHtml(bat.name)}" data-comp="${escapeHtml(comp.name)}">+ 新增单元</button>
                                </li>
                              ` : comp.points.map(pt => {
                                const isOnline = Boolean(pt.online);
                                const mountedDevId = pt.mountedDeviceId || "";
                                const isChecked = this.checkedPointIds.has(pt.id);

                                return `
                                  <li class="dir-tree-node dir-point-node ${isOnline ? 'is-online' : 'is-offline'}" data-point-id="${escapeHtml(pt.id)}">
                                    <div class="dir-node-row dir-point-row ${isChecked ? 'is-row-checked' : ''}" data-level="point">
                                      <span class="dir-tree-elbow"></span>
                                      <input type="checkbox" class="dir-checkbox dir-check-point" data-role="point" data-id="${escapeHtml(pt.id)}" data-bat="${escapeHtml(bat.name)}" data-comp="${escapeHtml(comp.name)}" ${isChecked ? 'checked' : ''}>
                                      <div class="dir-node-label">
                                        <span class="status-dot-point ${isOnline ? 'online' : 'standby'}"></span>
                                        <strong class="point-name-text">${escapeHtml(pt.name)}</strong>
                                      </div>

                                      <!-- 挂载状态徽标 -->
                                      <div class="dir-mount-indicator">
                                        ${mountedDevId ? `
                                          <span class="dir-mount-tag active" title="已挂载组播在线设备: ${escapeHtml(mountedDevId)}">
                                            ● 挂载: <strong class="mono">${escapeHtml(mountedDevId)}</strong>
                                          </span>
                                        ` : `
                                          <span class="dir-mount-tag inactive" title="未挂载设备，作业单元离线">○ 未挂载设备</span>
                                        `}
                                      </div>

                                      <!-- 快速挂载设备下拉 -->
                                      <div class="dir-quick-mount">
                                        <select class="quick-mount-select text-input text-input-xs" data-id="${escapeHtml(pt.id)}" title="快速变更挂载设备">
                                          <option value="">${mountedDevId ? '更改挂载…' : '挂载设备…'}</option>
                                          <option value="__UNMOUNT__">✕ 解除挂载</option>
                                          ${onlineRecons.map(u => `
                                            <option value="${escapeHtml(u.deviceId)}" ${u.deviceId === mountedDevId ? 'selected' : ''}>
                                              ${u.online ? '●' : '○'} ${escapeHtml(u.deviceId)} (${escapeHtml(u.name)})
                                            </option>
                                          `).join('')}
                                        </select>
                                      </div>

                                      <!-- 规格指标 -->
                                      <div class="dir-point-specs">
                                        <span class="dir-spec-chip" title="覆盖范围">${escapeHtml(pt.strikeRange || '20 km')}</span>
                                        <span class="dir-spec-chip mono" title="通信端点">${escapeHtml(pt.ip || '127.0.0.1')}:${pt.port || 5567}</span>
                                      </div>

                                      <!-- 操作按钮组 -->
                                      <div class="dir-node-actions point-actions">
                                        <button type="button" class="btn btn-xs btn-tactical-tool btn-dir-edit-point" data-id="${escapeHtml(pt.id)}" title="编辑作业单元参数">编辑</button>
                                        <button type="button" class="btn btn-xs btn-tactical-tool btn-dir-fly-point" data-id="${escapeHtml(pt.id)}" title="在地图上定位此单元">定位</button>
                                        <button type="button" class="btn btn-xs btn-secondary-danger btn-dir-del-point" data-id="${escapeHtml(pt.id)}" title="删除此作业单元">✕</button>
                                      </div>
                                    </div>
                                  </li>
                                `;
                              }).join('')}
                            </ul>
                          </li>
                        `;
                      }).join('')}
                    </ul>
                  </li>
                `;
              }).join('')}
            </ul>
          </div>
        </div>

        <!-- 底部操作栏 (参考图 1 界面按钮) -->
        <div class="dir-window-footer">
          <div class="dir-footer-left">
            <span class="dir-footer-stat">已勾选名录: <strong id="dir-checked-summary" class="highlight-cyan">${checkedPointsCount}</strong> / ${totalPointsCount} 个作业单元</span>
            <span class="dir-footer-tip">(支持层级全选与多选协同管理)</span>
          </div>
          <div class="dir-footer-right">
            <button type="button" class="btn btn-sm btn-primary-red btn-dir-send" id="btn-dir-action-send" title="向勾选的通信节点发送协同指令或下派任务">
              <span>发 送</span>
            </button>
            <button type="button" class="btn btn-sm btn-secondary btn-dir-cancel" id="btn-dir-action-cancel" title="关闭名录并返回综合态势">
              <span>取 消</span>
            </button>
          </div>
        </div>
      </div>
    `;

    // 初始化级联复选框状态
    this.updateCheckboxCascadeStates(tree);

    // 绑定窗口关闭与底部取消事件
    const returnToTactical = () => {
      this.close();
      eventBus.emit("page:switch", "tactical");
    };
    document.getElementById("btn-dir-win-close")?.addEventListener("click", returnToTactical);
    document.getElementById("btn-dir-action-cancel")?.addEventListener("click", returnToTactical);

    // 工具栏操作
    document.getElementById("btn-dir-expand-all")?.addEventListener("click", () => {
      this.collapsedKeys.clear();
      this.treeContainer.querySelectorAll(".dir-tree-node, .dir-tree-root-branch").forEach(el => {
        el.classList.remove("is-collapsed");
      });
      this.treeContainer.querySelectorAll(".dir-tree-list").forEach(el => {
        el.style.display = "";
      });
      this.treeContainer.querySelectorAll(".dir-expander:not(.empty-placeholder)").forEach(btn => {
        btn.textContent = "-";
      });
    });

    document.getElementById("btn-dir-collapse-all")?.addEventListener("click", () => {
      this.collapsedKeys.add("__root__");
      for (const bat of tree.battalions) {
        this.collapsedKeys.add(`bat:${bat.name}`);
        for (const comp of bat.companies) {
          this.collapsedKeys.add(`comp:${bat.name}:${comp.name}`);
        }
      }
      this.treeContainer.querySelectorAll(".dir-bat-node, .dir-comp-node, .dir-tree-root-branch").forEach(el => {
        el.classList.add("is-collapsed");
      });
      this.treeContainer.querySelectorAll(".dir-tree-list").forEach(el => {
        el.style.display = "none";
      });
      this.treeContainer.querySelectorAll(".dir-expander:not(.empty-placeholder)").forEach(btn => {
        btn.textContent = "+";
      });
    });

    document.getElementById("btn-dir-select-all")?.addEventListener("click", () => {
      for (const pt of allPoints) {
        this.checkedPointIds.add(pt.id);
      }
      this.treeContainer.querySelectorAll(".dir-check-point").forEach(cb => {
        cb.checked = true;
        cb.closest(".dir-point-row")?.classList.add("is-row-checked");
      });
      this.updateCheckboxCascadeStates(tree);
    });

    document.getElementById("btn-dir-deselect-all")?.addEventListener("click", () => {
      this.checkedPointIds.clear();
      this.treeContainer.querySelectorAll(".dir-check-point").forEach(cb => {
        cb.checked = false;
        cb.closest(".dir-point-row")?.classList.remove("is-row-checked");
      });
      this.updateCheckboxCascadeStates(tree);
    });

    document.getElementById("btn-dir-add-point")?.addEventListener("click", () => {
      this.openDrawerForNew();
    });

    document.getElementById("btn-dir-add-bat")?.addEventListener("click", () => {
      const batLabel = t("org.battalion", "片区");
      const batName = prompt(`请输入新${batLabel}名称 (例如: 3${batLabel}):`);
      if (batName?.trim() && dm?.addBattalion) {
        dm.addBattalion(batName.trim());
        this.render();
      }
    });

    document.getElementById("btn-dir-org-scale")?.addEventListener("click", () => {
      this.openOrgSettings();
    });

    this.treeContainer.querySelector(".btn-dir-rename-root")?.addEventListener("click", () => {
      const current = this.customRootName || tree.root || "业务调度中心";
      const val = prompt("请输入中枢集群自定义名称 (如: 协同调度集群):", current);
      if (val && val.trim()) {
        this.customRootName = val.trim();
        localStorage.setItem("org_custom_root_name", this.customRootName);
        this.render();
      }
    });

    this.treeContainer.querySelector(".btn-dir-add-bat-inline")?.addEventListener("click", () => {
      const batLabel = t("org.battalion", "片区");
      const batName = prompt(`请输入新${batLabel}名称 (例如: 3${batLabel}):`);
      if (batName?.trim() && dm?.addBattalion) {
        dm.addBattalion(batName.trim());
        this.render();
      }
    });

    // 折叠展开点击交互
    this.treeContainer.querySelectorAll(".dir-expander:not(.empty-placeholder)").forEach(btn => {
      btn.addEventListener("click", e => {
        e.stopPropagation();
        const toggleKey = btn.getAttribute("data-toggle");
        if (!toggleKey) return;
        const node = btn.closest(".dir-tree-node") || btn.closest(".dir-tree-root-branch");
        if (!node) return;
        const subList = node.querySelector(":scope > .dir-tree-list");
        if (!subList) return;

        const isCurrentlyCollapsed = this.collapsedKeys.has(toggleKey);
        if (isCurrentlyCollapsed) {
          this.collapsedKeys.delete(toggleKey);
          node.classList.remove("is-collapsed");
          subList.style.display = "";
          btn.textContent = "-";
        } else {
          this.collapsedKeys.add(toggleKey);
          node.classList.add("is-collapsed");
          subList.style.display = "none";
          btn.textContent = "+";
        }
      });
    });

    // 复选框交互及级联更新
    this.treeContainer.querySelectorAll(".dir-check-point").forEach(cb => {
      cb.addEventListener("change", () => {
        const id = cb.getAttribute("data-id");
        if (cb.checked) {
          this.checkedPointIds.add(id);
        } else {
          this.checkedPointIds.delete(id);
        }
        cb.closest(".dir-point-row")?.classList.toggle("is-row-checked", cb.checked);
        this.updateCheckboxCascadeStates(tree);
      });
    });

    this.treeContainer.querySelectorAll(".dir-check-comp").forEach(cb => {
      cb.addEventListener("change", () => {
        const batName = cb.getAttribute("data-bat");
        const compName = cb.getAttribute("data-comp");
        const bat = tree.battalions.find(b => b.name === batName);
        const comp = bat?.companies.find(c => c.name === compName);
        if (comp) {
          for (const pt of comp.points) {
            if (cb.checked) this.checkedPointIds.add(pt.id);
            else this.checkedPointIds.delete(pt.id);
            const ptCb = this.treeContainer.querySelector(`.dir-check-point[data-id="${CSS.escape(pt.id)}"]`);
            if (ptCb) {
              ptCb.checked = cb.checked;
              ptCb.closest(".dir-point-row")?.classList.toggle("is-row-checked", cb.checked);
            }
          }
        }
        this.updateCheckboxCascadeStates(tree);
      });
    });

    this.treeContainer.querySelectorAll(".dir-check-bat").forEach(cb => {
      cb.addEventListener("change", () => {
        const batName = cb.getAttribute("data-bat");
        const bat = tree.battalions.find(b => b.name === batName);
        if (bat) {
          for (const comp of bat.companies) {
            for (const pt of comp.points) {
              if (cb.checked) this.checkedPointIds.add(pt.id);
              else this.checkedPointIds.delete(pt.id);
              const ptCb = this.treeContainer.querySelector(`.dir-check-point[data-id="${CSS.escape(pt.id)}"]`);
              if (ptCb) {
                ptCb.checked = cb.checked;
                ptCb.closest(".dir-point-row")?.classList.toggle("is-row-checked", cb.checked);
              }
            }
          }
        }
        this.updateCheckboxCascadeStates(tree);
      });
    });

    this.treeContainer.querySelector(".dir-check-root")?.addEventListener("change", e => {
      const checked = e.target.checked;
      for (const pt of tree.allPoints) {
        if (checked) this.checkedPointIds.add(pt.id);
        else this.checkedPointIds.delete(pt.id);
      }
      this.treeContainer.querySelectorAll(".dir-check-point").forEach(ptCb => {
        ptCb.checked = checked;
        ptCb.closest(".dir-point-row")?.classList.toggle("is-row-checked", checked);
      });
      this.updateCheckboxCascadeStates(tree);
    });

    // 树内节点快捷操作
    this.treeContainer.querySelectorAll(".btn-dir-add-comp").forEach(btn => {
      btn.addEventListener("click", () => {
        const batName = btn.getAttribute("data-bat");
        const compLabel = t("org.company", "组");
        const compName = prompt(`请输入要在 [${batName}] 下新增的${compLabel}名称 (例如: 3${compLabel}):`);
        if (compName?.trim() && dm?.addCompany) {
          dm.addCompany(batName, compName.trim());
          this.render();
        }
      });
    });

    this.treeContainer.querySelectorAll(".btn-dir-del-comp").forEach(btn => {
      btn.addEventListener("click", () => {
        const batName = btn.getAttribute("data-bat");
        const compName = btn.getAttribute("data-comp");
        const compLabel = t("org.company", "组");
        if (confirm(`确定删除空${compLabel} [${batName} - ${compName}] 吗？`)) {
          dm?.removeCompany?.(batName, compName);
          this.render();
        }
      });
    });

    this.treeContainer.querySelectorAll(".btn-dir-del-bat").forEach(btn => {
      btn.addEventListener("click", () => {
        const batName = btn.getAttribute("data-bat");
        const batLabel = t("org.battalion", "片区");
        if (confirm(`确定删除空${batLabel} [${batName}] 吗？`)) {
          dm?.removeBattalion?.(batName);
          this.render();
        }
      });
    });

    this.treeContainer.querySelectorAll(".btn-dir-add-point-to-comp").forEach(btn => {
      btn.addEventListener("click", () => {
        const batName = btn.getAttribute("data-bat") || ("1" + t("org.battalion", "区"));
        const compName = btn.getAttribute("data-comp") || ("1" + t("org.company", "组"));
        this.openDrawerForNew(batName, compName);
      });
    });

    this.treeContainer.querySelectorAll(".btn-dir-edit-point").forEach(btn => {
      btn.addEventListener("click", () => {
        const id = btn.getAttribute("data-id");
        const pt = tree.allPoints.find(p => p.id === id);
        if (pt) this.openDrawerForEdit(pt, onlineRecons);
      });
    });

    this.treeContainer.querySelectorAll(".btn-dir-fly-point").forEach(btn => {
      btn.addEventListener("click", () => {
        const id = btn.getAttribute("data-id");
        const pt = tree.allPoints.find(p => p.id === id);
        if (pt && pt.longitude && pt.latitude) {
          eventBus.emit("page:switch", "tactical");
          const rangeM = parseRangeMeters(pt.strikeRange);
          this.viewerManager?.flyToFirepowerPoint(pt.longitude, pt.latitude, pt.altitude || 0, rangeM);
          this.close();
        }
      });
    });

    this.treeContainer.querySelectorAll(".btn-dir-del-point").forEach(btn => {
      btn.addEventListener("click", async () => {
        const id = btn.getAttribute("data-id");
        const pt = tree.allPoints.find(p => p.id === id);
        const name = pt ? pt.name : id;
        if (confirm(`确定删除作业单元 [${name}] 吗？`)) {
          if (dm?.removeFirepowerPoint) {
            await dm.removeFirepowerPoint(id);
            this.checkedPointIds.delete(id);
            this.render();
          }
        }
      });
    });

    this.treeContainer.querySelectorAll(".quick-mount-select").forEach(sel => {
      sel.addEventListener("change", async () => {
        const firepowerId = sel.getAttribute("data-id");
        const val = sel.value;
        if (!firepowerId) return;
        if (val === "__UNMOUNT__") {
          await dm?.mountDevice(firepowerId, "");
        } else if (val) {
          await dm?.mountDevice(firepowerId, val);
        }
        this.render();
      });
    });

    // 底部【发送】按钮 (参考图 1 风格)
    document.getElementById("btn-dir-action-send")?.addEventListener("click", () => {
      if (this.checkedPointIds.size === 0) {
        alert("请先勾选需要发送指令的作业单元！");
        return;
      }
      const selectedPts = tree.allPoints.filter(p => this.checkedPointIds.has(p.id));
      eventBus.emit("target:openReportModal", { firepowerId: selectedPts[0]?.id });
    });
  }

  renderReconPool(onlineRecons, allPoints) {
    if (!this.reconPoolContainer) return;
    const dm = this.getDeviceManager();

    if (!onlineRecons.length) {
      this.reconPoolContainer.innerHTML = `
        <div class="tactical-empty-box compact">
          <span class="empty-icon">○</span>
          <span class="empty-text">暂未收到EdgeAgent设备状态</span>
        </div>
      `;
      return;
    }

    this.reconPoolContainer.innerHTML = onlineRecons.map(u => {
      const isOnline = Boolean(u.online);
      const mounted = u.mountedTo;

      return `
        <div class="recon-pool-card ${isOnline ? 'online' : 'offline'}">
          <div class="recon-pool-header">
            <div class="recon-pool-title">
              <span class="status-dot-uav ${isOnline ? 'online' : 'standby'}"></span>
              <strong>${escapeHtml(u.name)}</strong>
            </div>
            <span class="recon-pool-id mono">${escapeHtml(u.deviceId)}</span>
          </div>

          <div class="recon-pool-meta">
            <span class="meta-ip mono">IP: ${escapeHtml(u.deviceIp)}</span>
            <span class="meta-status ${isOnline ? 'highlight-green' : 'dim'}">${isOnline ? '● 设备在线' : '○ 离线'}</span>
          </div>

          <div class="recon-pool-mount-state">
            ${mounted ? `
              <div class="pool-mounted-box">
                <span class="mounted-hint">已挂载:</span>
                <strong class="mounted-target">${escapeHtml(mounted.battalion)}-${escapeHtml(mounted.company)}-${escapeHtml(mounted.name)}</strong>
                <button type="button" class="btn btn-xs btn-pool-unmount" data-point-id="${escapeHtml(mounted.id)}">解绑</button>
              </div>
            ` : `
              <div class="pool-unmounted-box">
                <select class="pool-mount-select text-input text-input-xs" data-device-id="${escapeHtml(u.deviceId)}">
                  <option value="">${t("ui.mountToFirepower", "挂载到作业单元…")}</option>
                  ${allPoints.map(p => `
                    <option value="${escapeHtml(p.id)}">${escapeHtml(p.battalion)}-${escapeHtml(p.company)}-${escapeHtml(p.name)}</option>
                  `).join('')}
                </select>
                <button type="button" class="btn btn-xs btn-primary btn-pool-mount" data-device-id="${escapeHtml(u.deviceId)}">挂载</button>
              </div>
            `}
          </div>
        </div>
      `;
    }).join('');

    this.reconPoolContainer.querySelectorAll(".btn-pool-unmount").forEach(btn => {
      btn.addEventListener("click", async () => {
        const ptId = btn.getAttribute("data-point-id");
        if (ptId && dm?.mountDevice) {
          await dm.mountDevice(ptId, "");
          this.render();
        }
      });
    });

    this.reconPoolContainer.querySelectorAll(".btn-pool-mount").forEach(btn => {
      btn.addEventListener("click", async () => {
        const devId = btn.getAttribute("data-device-id");
        const select = btn.parentElement?.querySelector(".pool-mount-select");
        const ptId = select?.value;
        if (ptId && devId && dm?.mountDevice) {
          await dm.mountDevice(ptId, devId);
          this.render();
        }
      });
    });
  }

  populateMountedSelect(currentVal = "", onlineRecons = []) {
    if (!this.selectMountDevice) return;
    this.selectMountDevice.innerHTML = `
      <option value="">${t("ui.unmountedOffline", "(未挂载 / 作业单元离线)")}</option>
      ${onlineRecons.map(u => `
        <option value="${escapeHtml(u.deviceId)}" ${u.deviceId === currentVal ? 'selected' : ''}>
          ${escapeHtml(u.deviceId)} - ${escapeHtml(u.name)} (${u.online ? '设备在线' : '离线'})
        </option>
      `).join('')}
    `;
    if (currentVal && !onlineRecons.some(u => u.deviceId === currentVal)) {
      const opt = document.createElement("option");
      opt.value = currentVal;
      opt.textContent = `${currentVal} (当前配置/离线)`;
      opt.selected = true;
      this.selectMountDevice.appendChild(opt);
    }
  }

  syncMountedIp() {
    if (!this.inputIp) return;
    if (!this.inputIp.readOnly) this.unmountedDrawerIp = this.inputIp.value;
    const deviceIp = this.getDeviceManager()?.getMountedDeviceIp(this.selectMountDevice?.value);
    this.inputIp.readOnly = Boolean(deviceIp);
    this.inputIp.value = deviceIp || this.unmountedDrawerIp || "127.0.0.1";
  }

  openDrawerForEdit(point, onlineRecons) {
    this.closeOrgSettings();
    this.editingPointId = point.id;
    if (this.drawerTitle) this.drawerTitle.textContent = `编辑单元: ${point.name}`;
    if (this.drawerPointId) this.drawerPointId.value = point.id;

    this.populateOrgSelects(point.battalion || t("org.battalionDefault", "1区"), point.company || t("org.companyDefault", "1组"));

    if (this.inputName) this.inputName.value = point.name || "";
    if (this.inputLon) this.inputLon.value = point.longitude ? point.longitude.toFixed(6) : "118.570452";
    if (this.inputLat) this.inputLat.value = point.latitude ? point.latitude.toFixed(6) : "24.573654";
    if (this.inputAlt) this.inputAlt.value = point.altitude ?? 0;
    if (this.inputRange) this.inputRange.value = point.strikeRange || "12 km";
    if (this.inputMaxTargets) this.inputMaxTargets.value = point.maxTargets ?? 1;
    if (this.inputIp) {
      const deviceIp = this.getDeviceManager()?.getMountedDeviceIp(point.mountedDeviceId);
      this.inputIp.readOnly = false;
      this.inputIp.value = point.unmountedIp ?? (deviceIp === point.ip ? "127.0.0.1" : point.ip || "127.0.0.1");
    }
    if (this.inputPort) this.inputPort.value = point.port || 5567;

    if (this.pickedHint) {
      this.pickedHint.hidden = true;
      this.pickedHint.style.display = "none";
      this.pickedHint.innerHTML = "";
    }

    this.populateMountedSelect(point.mountedDeviceId || "", onlineRecons);
    this.syncMountedIp();

    if (this.drawer) {
      this.drawer.hidden = false;
      this.drawer.style.display = "flex";
    }
  }

  openDrawerForNew(defaultBat = t("org.battalionDefault", "1区"), defaultComp = t("org.companyDefault", "1组")) {
    this.closeOrgSettings();
    this.editingPointId = null;
    const dm = this.getDeviceManager();
    const onlineRecons = dm?.getOnlineReconDevices ? dm.getOnlineReconDevices() : [];
    const all = dm?.getFixedPoints ? dm.getFixedPoints() : [];
    const nextIdx = all.length + 1;

    if (this.drawerTitle) this.drawerTitle.textContent = `新增单元`;
    if (this.drawerPointId) this.drawerPointId.value = "";

    this.populateOrgSelects(defaultBat, defaultComp);

    if (this.inputName) this.inputName.value = t("roles.firepowerDefaultName", `${nextIdx}号作业单元`, { n: nextIdx });
    if (this.inputLon) this.inputLon.value = "118.500000";
    if (this.inputLat) this.inputLat.value = "24.500000";
    if (this.inputAlt) this.inputAlt.value = "0";
    if (this.inputRange) this.inputRange.value = "20 km";
    if (this.inputMaxTargets) this.inputMaxTargets.value = "1";
    if (this.inputIp) {
      this.inputIp.readOnly = false;
      this.inputIp.value = "127.0.0.1";
    }
    if (this.inputPort) this.inputPort.value = "5567";

    if (this.pickedHint) {
      this.pickedHint.hidden = true;
      this.pickedHint.style.display = "none";
      this.pickedHint.innerHTML = "";
    }

    this.populateMountedSelect("", onlineRecons);
    this.syncMountedIp();

    if (this.drawer) {
      this.drawer.hidden = false;
      this.drawer.style.display = "flex";
    }
  }

  closeDrawer() {
    if (this.drawer) {
      this.drawer.hidden = true;
      this.drawer.style.display = "none";
    }
    if (this.pickedHint) {
      this.pickedHint.hidden = true;
      this.pickedHint.style.display = "none";
      this.pickedHint.innerHTML = "";
    }
    this.editingPointId = null;
  }

  async handleSaveDrawer() {
    const dm = this.getDeviceManager();
    if (!dm) return;

    const battalion = this.inputBattalion?.value?.trim() || t("org.battalionDefault", "1区");
    const company = this.inputCompany?.value?.trim() || t("org.companyDefault", "1组");
    const name = this.inputName?.value?.trim() || t("roles.firepowerBase", "地面作业点");
    const mountedDeviceId = this.selectMountDevice?.value?.trim() || "";
    const longitude = parseFloat(this.inputLon?.value);
    const latitude = parseFloat(this.inputLat?.value);
    const altitude = parseFloat(this.inputAlt?.value) || 0;
    const strikeRange = this.inputRange?.value?.trim() || "20 km";
    const maxTargets = parseInt(this.inputMaxTargets?.value, 10) || 1;
    const ip = (this.inputIp?.readOnly ? this.unmountedDrawerIp : this.inputIp?.value)?.trim() || "127.0.0.1";
    const port = parseInt(this.inputPort?.value, 10) || 5567;

    const payload = {
      battalion,
      company,
      name,
      mountedDeviceId,
      longitude: isNaN(longitude) ? 118.500000 : Number(longitude.toFixed(6)),
      latitude: isNaN(latitude) ? 24.500000 : Number(latitude.toFixed(6)),
      altitude: Number(altitude.toFixed(1)),
      strikeRange,
      maxTargets,
      ip,
      port
    };

    const saved = this.editingPointId
      ? await dm.updatePointAffiliation(this.editingPointId, payload)
      : await dm.addFirepowerPoint(payload);
    if (!saved) return;

    this.closeDrawer();
    this.render();
  }
}

export { AffiliationModal as FirepowerAffiliationModal };
