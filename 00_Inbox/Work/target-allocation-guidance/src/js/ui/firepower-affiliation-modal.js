/**
 * 战术火力整备与编制隶属管理弹窗 (完整树状拓扑图 + 营连建制规模设置 + EdgeAgent在线设备挂载)
 */
import { escapeHtml } from "../core/html.js";
import { eventBus } from "../core/event-bus.js";

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

export class FirepowerAffiliationModal {
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

    // 营连编制规模设置抽屉
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
      if (confirm("确定重置为默认标准战术建制（2个营，各2个连）吗？")) {
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

    // 营连规模设置相关事件
    this.btnCloseOrgModal?.addEventListener("click", () => this.closeOrgSettings());
    this.btnFinishOrgModal?.addEventListener("click", () => this.closeOrgSettings());
    this.btnApplyOrgScale?.addEventListener("click", () => this.handleApplyOrgScale());
    this.btnResetOrgDefault?.addEventListener("click", () => this.handleResetOrgDefault());
    this.btnAddBat?.addEventListener("click", () => this.handleAddBattalion());

    // 编辑抽屉营连下拉联动
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

  // ===== 营连编制规模设置抽屉 =====
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
      this.orgManageList.innerHTML = `<div class="tactical-empty-box compact"><span class="empty-text">当前暂未配置营级建制</span></div>`;
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
              <span class="bat-points-count">(${pointsInBat}个火力点)</span>
            </div>
            <div class="bat-manage-actions">
              <button type="button" class="btn btn-xs btn-secondary btn-org-add-comp" data-battalion="${escapeHtml(bat.name)}">+ 连</button>
              ${pointsInBat === 0 ? `
                <button type="button" class="btn btn-xs btn-secondary-danger btn-org-del-bat" data-battalion="${escapeHtml(bat.name)}" title="删除空营">✕</button>
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
                    <button type="button" class="comp-chip-del btn-org-del-comp" data-battalion="${escapeHtml(bat.name)}" data-company="${escapeHtml(comp)}" title="删除空连">✕</button>
                  ` : ''}
                </span>
              `;
            }).join('')}
            <button type="button" class="comp-chip-add btn-org-add-comp-inline" data-battalion="${escapeHtml(bat.name)}" title="新增连">+</button>
          </div>
        </div>
      `;
    }).join('');

    // 绑定营连管理事件
    this.orgManageList.querySelectorAll(".btn-org-add-comp, .comp-chip-add").forEach(btn => {
      btn.addEventListener("click", () => {
        const batName = btn.getAttribute("data-battalion");
        const compName = prompt(`请输入要在 [${batName}] 下新增的连名称 (如: 3连):`);
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
        if (confirm(`确定删除空营 [${batName}] 吗？`)) {
          dm?.removeBattalion?.(batName);
          this.renderOrgManageList();
          this.render();
        }
      });
    });
  }

  // ===== 编辑抽屉营连下拉填充与联动 =====
  populateOrgSelects(selectedBat = "1营", selectedComp = "1连") {
    const dm = this.getDeviceManager();
    const orgRaw = dm?.getOrgStructure ? dm.getOrgStructure() : null;
    const battalions = Array.isArray(orgRaw) ? orgRaw : (orgRaw?.battalions || [
      { name: "1营", companies: ["1连", "2连"] },
      { name: "2营", companies: ["1连", "2连"] }
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
    const comps = batObj?.companies || ["1连", "2连"];

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

  renderTreeCanvas(tree, onlineRecons) {
    if (!this.treeContainer) return;
    const dm = this.getDeviceManager();

    const rootHtml = `
      <div class="tree-root-wrapper">
        <div class="tree-node tree-node-root">
          <div class="node-root-badge">指挥中枢</div>
          <div class="node-root-title">
            <span class="status-dot-point online" aria-hidden="true"></span>
            <strong>${tree.root}</strong>
          </div>
          <div class="node-root-meta">
            <span>下辖 <strong>${tree.battalions.length}</strong> 个营</span>
            <span class="meta-sep">/</span>
            <span>共 <strong>${tree.totalPoints}</strong> 个火力点</span>
            <span class="meta-sep">/</span>
            <span class="highlight-green"><strong>${tree.onlinePoints}</strong> 在线就绪</span>
          </div>
          <div class="node-branch-actions root-branch-actions">
            <button type="button" class="btn btn-xs btn-tactical-tool highlight-tool" id="btn-tree-open-org-cfg" title="配置营连编制架构">
              <span>营连建制设置</span>
            </button>
            <button type="button" class="btn btn-xs btn-secondary" id="btn-tree-add-bat-quick" title="快速新增营建制">
              <span>+ 添加营</span>
            </button>
          </div>
        </div>
        <div class="tree-stem-vertical"></div>
      </div>
    `;

    const battalionsHtml = `
      <div class="tree-battalions-container">
        ${tree.battalions.map(bat => `
          <div class="tree-branch-battalion">
            <div class="tree-connector-to-battalion"></div>
            <div class="tree-node tree-node-battalion">
              <div class="node-badge-level">营级建制</div>
              <div class="node-title-row">
                <span class="status-dot-point ${bat.onlinePoints > 0 ? 'online' : 'standby'}"></span>
                <strong>${escapeHtml(bat.name)}</strong>
              </div>
              <div class="node-meta-row">
                <span>下辖 ${bat.companies.length} 个连</span>
                <span>${bat.totalPoints} 个火力点 (${bat.onlinePoints} 在线)</span>
              </div>
              <div class="node-branch-actions">
                <button type="button" class="btn btn-xs btn-secondary btn-tree-add-comp-quick" data-battalion="${escapeHtml(bat.name)}" title="在此营下新增连">+ 添加连</button>
                ${bat.totalPoints === 0 ? `
                  <button type="button" class="btn btn-xs btn-tactical-tool btn-tree-del-bat-quick" data-battalion="${escapeHtml(bat.name)}" title="删除空营建制">✕ 删除营</button>
                ` : ''}
              </div>
            </div>
            <div class="tree-stem-sub-vertical"></div>

            <div class="tree-companies-container">
              ${bat.companies.map(comp => `
                <div class="tree-branch-company">
                  <div class="tree-connector-to-company"></div>
                  <div class="tree-node tree-node-company">
                    <div class="node-badge-level company-badge">连级阵地</div>
                    <div class="node-title-row">
                      <span class="status-dot-point ${comp.onlinePoints > 0 ? 'online' : 'standby'}"></span>
                      <strong>${escapeHtml(comp.name)}</strong>
                    </div>
                    <div class="node-meta-row">
                      <span>${comp.points.length} 个火力点</span>
                    </div>
                    <div class="node-branch-actions">
                      <button type="button" class="btn btn-xs btn-tactical-tool highlight-tool btn-tree-add-point-to-comp" data-battalion="${escapeHtml(bat.name)}" data-company="${escapeHtml(comp.name)}" title="在该连新增火力点">+ 新增火力</button>
                      ${comp.points.length === 0 ? `
                        <button type="button" class="btn btn-xs btn-tactical-tool btn-tree-del-comp-quick" data-battalion="${escapeHtml(bat.name)}" data-company="${escapeHtml(comp.name)}" title="删除此连编制">✕ 删除连</button>
                      ` : ''}
                    </div>
                  </div>
                  <div class="tree-stem-sub-vertical"></div>

                  <div class="tree-points-container">
                    ${comp.points.length === 0 ? `
                      <div class="tree-node-empty-company">
                        <span class="empty-comp-text">该连暂无火力点</span>
                        <button type="button" class="btn btn-xs btn-tactical-tool highlight-tool btn-tree-add-point-to-comp" data-battalion="${escapeHtml(bat.name)}" data-company="${escapeHtml(comp.name)}">
                          <span>+ 新增火力</span>
                        </button>
                      </div>
                    ` : comp.points.map(pt => {
                      const isOnline = Boolean(pt.online);
                      const mountedDevId = pt.mountedDeviceId || "";
                      const mountedDevice = onlineRecons.find(u => u.deviceId === mountedDevId || u.id === mountedDevId);
                      const isMulticastActive = mountedDevice && mountedDevice.online;

                      return `
                        <div class="tree-node tree-node-point ${isOnline ? 'is-online' : 'is-offline'}" data-point-id="${escapeHtml(pt.id)}">
                          <div class="card-corner-tl"></div>
                          <div class="card-corner-br"></div>

                          <div class="point-node-header">
                            <div class="point-node-title">
                              <span class="status-dot-point ${isOnline ? 'online' : 'standby'}" aria-hidden="true"></span>
                              <strong>${escapeHtml(pt.name)}</strong>
                              <span class="point-affiliation-tag">${escapeHtml(pt.battalion)}·${escapeHtml(pt.company)}</span>
                            </div>
                            <div class="point-status-pill ${isOnline ? 'online' : 'offline'}">
                              ${isOnline ? '● 在线' : '○ 离线'}
                            </div>
                          </div>

                          <div class="point-node-body">
                            <!-- EdgeAgent发现设备挂载区 -->
                            <div class="point-mount-box ${mountedDevId ? 'has-mount' : 'no-mount'}">
                              <div class="mount-box-header">
                                <span class="mount-label">挂载在线设备ID</span>
                                <span class="mount-status-tag ${isMulticastActive ? 'active' : 'inactive'}">
                                  ${isMulticastActive ? '● 设备在线' : (mountedDevId ? '○ 设备离线' : '未挂载')}
                                </span>
                              </div>
                              <div class="mount-device-row">
                                <strong class="mount-dev-id mono ${mountedDevId ? 'highlight-cyan' : ''}">${escapeHtml(mountedDevId || '无挂载设备 (火力点离线)')}</strong>
                                ${mountedDevice ? `<span class="mount-dev-name">${escapeHtml(mountedDevice.name)}</span>` : ''}
                              </div>
                            </div>

                            <!-- 火力点战术规格 -->
                            <div class="point-specs-grid">
                              <div class="spec-cell">
                                <span class="spec-lbl">打击范围</span>
                                <strong class="spec-val mono">${escapeHtml(pt.strikeRange || '40 km')}</strong>
                              </div>
                              <div class="spec-cell">
                                <span class="spec-lbl">通道容量</span>
                                <strong class="spec-val mono">${pt.maxTargets ?? 1} 批次</strong>
                              </div>
                              <div class="spec-cell span-2">
                                <span class="spec-lbl">通信与坐标</span>
                                <span class="spec-val mono coord-val">${pt.longitude ? `${pt.longitude.toFixed(4)}°, ${pt.latitude.toFixed(4)}°` : '待配坐标'}</span>
                              </div>
                            </div>
                          </div>

                          <div class="point-node-actions">
                            <button type="button" class="btn btn-xs btn-tree-edit" data-id="${escapeHtml(pt.id)}" title="编辑所属营连及火力点参数">
                              <span>编辑隶属</span>
                            </button>
                            <div class="quick-mount-inline">
                              <select class="quick-mount-select text-input text-input-xs" data-id="${escapeHtml(pt.id)}">
                                <option value="">${mountedDevId ? '更改挂载…' : '挂载设备…'}</option>
                                <option value="__UNMOUNT__">✕ 解除挂载</option>
                                ${onlineRecons.map(u => `
                                  <option value="${escapeHtml(u.deviceId)}" ${u.deviceId === mountedDevId ? 'selected' : ''}>
                                    ${u.online ? '●' : '○'} ${escapeHtml(u.deviceId)} (${escapeHtml(u.name)})
                                  </option>
                                `).join('')}
                              </select>
                            </div>
                            <button type="button" class="btn btn-xs btn-tree-fly" data-id="${escapeHtml(pt.id)}" title="地图视口飞向该火力点">
                              <span>定位</span>
                            </button>
                            <button type="button" class="btn btn-xs btn-tree-del" data-id="${escapeHtml(pt.id)}" title="删除该火力点">
                              <span>删除</span>
                            </button>
                          </div>
                        </div>
                      `;
                    }).join('')}
                  </div>
                </div>
              `).join('')}
            </div>
          </div>
        `).join('')}
      </div>
    `;

    this.treeContainer.innerHTML = `
      <div class="tactical-tree-wrapper">
        ${rootHtml}
        ${battalionsHtml}
      </div>
    `;

    // 绑定拓扑树快捷交互
    document.getElementById("btn-tree-open-org-cfg")?.addEventListener("click", () => this.openOrgSettings());
    document.getElementById("btn-tree-add-bat-quick")?.addEventListener("click", () => {
      const batName = prompt("请输入新营名称 (例如: 3营):");
      if (batName?.trim() && dm?.addBattalion) {
        dm.addBattalion(batName.trim());
        this.render();
      }
    });

    this.treeContainer.querySelectorAll(".btn-tree-add-comp-quick").forEach(btn => {
      btn.addEventListener("click", () => {
        const batName = btn.getAttribute("data-battalion");
        const compName = prompt(`请输入要在 [${batName}] 下新增的连名称 (例如: 3连):`);
        if (compName?.trim() && dm?.addCompany) {
          dm.addCompany(batName, compName.trim());
          this.render();
        }
      });
    });

    this.treeContainer.querySelectorAll(".btn-tree-del-comp-quick").forEach(btn => {
      btn.addEventListener("click", () => {
        const batName = btn.getAttribute("data-battalion");
        const compName = btn.getAttribute("data-company");
        if (confirm(`确定删除空连 [${batName} - ${compName}] 吗？`)) {
          dm?.removeCompany?.(batName, compName);
          this.render();
        }
      });
    });

    this.treeContainer.querySelectorAll(".btn-tree-del-bat-quick").forEach(btn => {
      btn.addEventListener("click", () => {
        const batName = btn.getAttribute("data-battalion");
        if (confirm(`确定删除空营 [${batName}] 吗？`)) {
          dm?.removeBattalion?.(batName);
          this.render();
        }
      });
    });

    this.treeContainer.querySelectorAll(".btn-tree-add-point-to-comp").forEach(btn => {
      btn.addEventListener("click", () => {
        const batName = btn.getAttribute("data-battalion") || "1营";
        const compName = btn.getAttribute("data-company") || "1连";
        this.openDrawerForNew(batName, compName);
      });
    });

    this.treeContainer.querySelectorAll(".btn-tree-edit").forEach(btn => {
      btn.addEventListener("click", () => {
        const id = btn.getAttribute("data-id");
        const pt = tree.allPoints.find(p => p.id === id);
        if (pt) this.openDrawerForEdit(pt, onlineRecons);
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

    this.treeContainer.querySelectorAll(".btn-tree-fly").forEach(btn => {
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

    this.treeContainer.querySelectorAll(".btn-tree-del").forEach(btn => {
      btn.addEventListener("click", async () => {
        const id = btn.getAttribute("data-id");
        if (confirm("确定要删除该火力点吗？")) {
          await dm?.removeFirepowerPoint(id);
          this.render();
        }
      });
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
                  <option value="">挂载到火力点…</option>
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
      <option value="">(未挂载 / 火力点离线)</option>
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
    if (this.drawerTitle) this.drawerTitle.textContent = `编辑火力: ${point.name}`;
    if (this.drawerPointId) this.drawerPointId.value = point.id;

    this.populateOrgSelects(point.battalion || "1营", point.company || "1连");

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

  openDrawerForNew(defaultBat = "1营", defaultComp = "1连") {
    this.closeOrgSettings();
    this.editingPointId = null;
    const dm = this.getDeviceManager();
    const onlineRecons = dm?.getOnlineReconDevices ? dm.getOnlineReconDevices() : [];
    const all = dm?.getFixedPoints ? dm.getFixedPoints() : [];
    const nextIdx = all.length + 1;

    if (this.drawerTitle) this.drawerTitle.textContent = `新增火力`;
    if (this.drawerPointId) this.drawerPointId.value = "";

    this.populateOrgSelects(defaultBat, defaultComp);

    if (this.inputName) this.inputName.value = `${nextIdx}号火力点`;
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

    const battalion = this.inputBattalion?.value?.trim() || "1营";
    const company = this.inputCompany?.value?.trim() || "1连";
    const name = this.inputName?.value?.trim() || "地面火力点";
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

    if (this.editingPointId) {
      await dm.updatePointAffiliation(this.editingPointId, payload);
    } else {
      await dm.addFirepowerPoint(payload);
    }

    this.closeDrawer();
    this.render();
  }
}
