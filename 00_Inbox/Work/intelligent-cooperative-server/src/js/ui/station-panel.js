import { escapeHtml } from "../core/html.js";
import { eventBus } from "../core/event-bus.js";
import { hasGeographicPosition } from "../core/coordinates.js";
import { getPlatformDeviceId, getPlatformDisplayName } from "../core/platform-labels.js";
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

export class StationPanel {
  constructor(cameraHud, viewerManager) {
    this.cameraHud = cameraHud;
    this.viewerManager = viewerManager;
    this.uavList = document.getElementById("uav-list-container");
    this.situationReportStatus = document.getElementById("situation-report-status");
    this.firepowerList = document.getElementById("firepower-list-container");
    this.videoCards = new Map();
    this.pointCards = new Map();
    this.selectedReconId = null;

    eventBus.on("battery:selected", id => this.highlightPoint(id));
    eventBus.on("selection:cleared", () => this.highlightPoint(null));
  }

  update(uavs, points, simulation = null) {
    this.videoCards.clear();
    this.pointCards.clear();
    if (this.cameraHud.activeUav && !uavs.some(uav => uav.id === this.cameraHud.activeUav.id)) {
      this.cameraHud.toggle(this.cameraHud.activeUav);
    }
    this.renderList(this.uavList, uavs.map(uav => this.makeUavCard(uav, simulation)),
      simulation ? t("ui.waitingRecon", "等待监测席广播...") : t("ui.noRecon", "【无监测席】"));
    this.renderList(this.firepowerList, points.map(point => this.makePointCard(point, simulation)),
      simulation ? t("ui.waitingFirepower", "暂无作业单元 (可从监测席转换)") : t("ui.noFirepower", "【无作业单元】"));
    if (this.selectedReconId && !uavs.some(uav => uav.id === this.selectedReconId)) {
      this.selectedReconId = null;
      eventBus.emit("recon-seat:selected", null);
    }
    if (!this.selectedReconId && uavs.length === 1) this.selectReconSeat(uavs[0]);
    this.updateReconSeatSelection();
    this.updateVideoButtons();
  }

  selectReconSeat(uav) {
    this.selectedReconId = uav?.id ?? null;
    this.updateReconSeatSelection();
    eventBus.emit("recon-seat:selected", uav ? {
      id: uav.id,
      deviceId: getPlatformDeviceId(uav),
      name: getPlatformDisplayName(uav, "recon")
    } : null);
  }

  updateReconSeatSelection() {
    for (const [id, card] of this.videoCards) {
      const selected = id === this.selectedReconId;
      card.classList.toggle("is-locate-selected", selected);
      if (selected) card.setAttribute("aria-current", "true");
      else card.removeAttribute("aria-current");
    }
  }

  renderList(list, cards, emptyText) {
    if (cards.length) {
      list.replaceChildren(...cards);
    } else {
      const empty = document.createElement("div");
      empty.className = "tactical-empty-box";
      empty.innerHTML = `
        <span class="empty-icon">○</span>
        <span class="empty-text">${emptyText}</span>
      `;
      list.replaceChildren(empty);
    }
  }

  makeUavCard(uav, simulation) {
    const card = document.createElement("article");
    const positioned = hasGeographicPosition(uav);
    card.className = "uav-recon-card tactical-card" + (positioned ? "" : " is-unlocated");
    card.dataset.id = uav.id;

    const displayName = getPlatformDisplayName(uav, "recon");
    const videoCount = uav.videoSources ?? uav.videoChannels?.length ?? 0;
    const isOnline = uav.online === true;
    const targetIp = uav.deviceIp || uav.ip || "";
    const canReport = Boolean(targetIp && simulation?.reportSituation);
    const reportHint = canReport
      ? `向 ${targetIp}:28081 发送 UDP 态势上报指令`
      : t("ui.waitingReconIp", "等待监测设备上报 IP（或点击[配置]手动指定）");

    card.innerHTML = `
      <div class="card-corner-tl"></div>
      <div class="card-corner-br"></div>
      
      <div class="uav-header">
        <div class="uav-title">
          <span class="status-dot-uav ${isOnline ? 'online' : 'standby'}" aria-hidden="true"></span>
          <strong>${escapeHtml(displayName)}</strong>
        </div>
        <div class="header-tags">
          <button type="button" class="btn btn-sm btn-firepower-config btn-tactical-edit-point" aria-label="${t("ui.configReconTitle", "配置监测席参数")}" title="${t("ui.configReconTitle", "配置监测席参数")}">
            <svg class="btn-icon-svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
              <circle cx="12" cy="12" r="3"/>
              <path d="M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 1 1-2.83 2.83l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 1 1-4 0v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 1 1-2.83-2.83l.06-.06a1.65 1.65 0 0 0 .33-1.82 1.65 1.65 0 0 0-1.51-1H3a2 2 0 1 1 0-4h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 1 1 2.83-2.83l.06.06a1.65 1.65 0 0 0 1.82.33H9a1.65 1.65 0 0 0 1-1.51V3a2 2 0 1 1 4 0v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 1 1 2.83 2.83l-.06-.06a1.65 1.65 0 0 0-.33 1.82V9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 1 1 0 4h-.09a1.65 1.65 0 0 0-1.51 1z"/>
            </svg>
            <span class="btn-text">配置</span>
          </button>
        </div>
      </div>

      <div class="card-essential-info-row">
        <span class="situation-report-control" title="${escapeHtml(reportHint)}">
          <button type="button" class="btn btn-sm" ${canReport ? "" : "disabled"}>
            <svg class="btn-icon-svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
              <polyline points="23 4 23 10 17 10"/>
              <polyline points="1 20 1 14 7 14"/>
              <path d="M3.51 9a9 9 0 0 1 14.85-3.36L23 10M1 14l4.64 4.36A9 9 0 0 0 20.49 15"/>
            </svg>
            <span>${t("ui.situationSync", "状态同步")}</span>
          </button>
        </span>
        <div class="essential-actions-group" aria-label="${t("roles.recon", "监测席")}在线状态">
          <span class="status-essential-tag btn-uav-video ${isOnline ? 'online' : 'standby'}" title="${videoCount} 路视频源，点击卡片选择${t("roles.recon", "监测席")}并打开视频">
            ${isOnline ? '● 就绪' : '○ 离线'}
          </span>
        </div>
      </div>

    `;

    card.querySelector(".btn-tactical-edit-point")?.addEventListener("click", e => {
      e.stopPropagation();
      if (simulation) {
        this.selectReconSeat(uav);
        simulation.edit(uav, "recon");
      }
    });

    const uavTitleEl = card.querySelector(".uav-title strong");
    if (uavTitleEl) {
      uavTitleEl.style.cursor = "pointer";
      uavTitleEl.title = "点击[配置]或双击名称修改侦察席名称";
      uavTitleEl.addEventListener("dblclick", e => {
        e.stopPropagation();
        if (simulation) {
          this.selectReconSeat(uav);
          simulation.edit(uav, "recon");
        }
      });
    }

    const reportControl = card.querySelector(".situation-report-control");
    reportControl.addEventListener("click", e => e.stopPropagation());
    const reportBtn = reportControl.querySelector("button");
    reportBtn.addEventListener("click", async () => {
      if (!canReport || reportBtn.disabled) return;
      const status = this.situationReportStatus;
      reportBtn.disabled = true;
      status.hidden = false;
      status.textContent = `${displayName}：` + t("ui.sendingSyncCommand", "正在发送状态同步指令…");
      try {
        const { address, port } = await simulation.reportSituation(uav.id, targetIp);
        status.textContent = `${displayName}：指令已发往 ${address}:${port}（UDP）`;
      } catch (error) {
        status.textContent = `${displayName}：${error.message}`;
      } finally {
        reportBtn.disabled = false;
      }
    });

    card.tabIndex = 0;
    const openVideo = () => {
      this.selectReconSeat(uav);
      this.cameraHud.toggle(uav);
      this.updateVideoButtons();
    };
    card.addEventListener("click", openVideo);
    card.addEventListener("keydown", event => {
      if (event.target !== card || (event.key !== "Enter" && event.key !== " ")) return;
      event.preventDefault();
      openVideo();
    });
    this.videoCards.set(uav.id, card);
    return card;
  }

  makePointCard(point, simulation) {
    const positioned = hasGeographicPosition(point);
    const card = document.createElement("article");
    card.className = "artillery-card tactical-card" + (positioned ? " is-positioned" : " is-unlocated");
    card.dataset.id = point.id;

    const isOnline = point.online === true;
    const displayName = getPlatformDisplayName(point, "firepower");

    card.innerHTML = `
      <div class="card-corner-tl"></div>
      <div class="card-corner-br"></div>

      <div class="artillery-header">
        <div class="artillery-title">
          <span class="status-dot-point ${isOnline ? 'online' : 'standby'}" aria-hidden="true"></span>
          <strong></strong>
        </div>
        <div class="header-tags">
          <button type="button" class="btn btn-sm btn-firepower-config btn-tactical-edit-point" aria-label="${t("ui.configFirepowerTitle", "配置火力点参数")}" title="${t("ui.configFirepowerTitle", "配置火力点参数")}">
            <svg class="btn-icon-svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
              <circle cx="12" cy="12" r="3"/>
              <path d="M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 1 1-2.83 2.83l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 1 1-4 0v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 1 1-2.83-2.83l.06-.06a1.65 1.65 0 0 0 .33-1.82 1.65 1.65 0 0 0-1.51-1H3a2 2 0 1 1 0-4h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 1 1 2.83-2.83l.06.06a1.65 1.65 0 0 0 1.82.33H9a1.65 1.65 0 0 0 1-1.51V3a2 2 0 1 1 4 0v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 1 1 2.83 2.83l-.06-.06a1.65 1.65 0 0 0-.33 1.82V9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 1 1 0 4h-.09a1.65 1.65 0 0 0-1.51 1z"/>
            </svg>
            <span class="btn-text">配置</span>
          </button>
        </div>
      </div>

      <div class="card-essential-info-row">
        <div class="essential-actions-group">
          <span class="status-essential-tag ${isOnline ? 'online' : 'standby'}" title="${isOnline ? 'EdgeAgent转发设备在线就绪' : '未挂载或设备离线'}">
            ${isOnline ? '● 在线' : '○ 离线'}
          </span>
        </div>
      </div>
    `;

    const fpTitleEl = card.querySelector(".artillery-title strong");
    fpTitleEl.textContent = displayName;
    fpTitleEl.style.cursor = "pointer";
    fpTitleEl.title = "点击[配置]或双击名称修改火力点名称";
    fpTitleEl.addEventListener("dblclick", e => {
      e.stopPropagation();
      if (simulation?.edit) {
        simulation.edit(point);
      } else {
        eventBus.emit("firepower:openAffiliationModal");
      }
    });

    const editBtn = card.querySelector(".btn-tactical-edit-point");
    editBtn?.addEventListener("click", e => {
      e.stopPropagation();
      if (simulation?.edit) {
        simulation.edit(point);
      } else {
        eventBus.emit("firepower:openAffiliationModal");
      }
    });

    if (positioned) {
      card.style.cursor = "pointer";
      card.addEventListener("click", () => {
        eventBus.emit("battery:selected", point.id);
        const rangeM = parseRangeMeters(point.strikeRange ?? point.maxRangeKm);
        this.viewerManager?.flyToFirepowerPoint(point.longitude, point.latitude, point.altitude, rangeM);
      });
    } else {
      card.addEventListener("click", () => {
        if (simulation?.edit) {
          simulation.edit(point);
        } else {
          eventBus.emit("firepower:openAffiliationModal");
        }
      });
    }

    this.pointCards.set(point.id, card);
    return card;
  }

  renderDeviceMeta(container, device, role) {
    if (!container) return;
    const tags = [];
    if (device.deviceId) {
      tags.push(`<span class="device-tag-id" title="设备编号">${escapeHtml(device.deviceId)}</span>`);
    }
    const ip = device.deviceIp || device.ip;
    if (ip) {
      tags.push(`<span class="device-tag-ip mono" title="设备通信IP">${escapeHtml(ip)}</span>`);
    }
    tags.push(`<span class="device-tag-status ${device.online ? 'online' : 'offline'}">${device.online ? '● 在线' : '○ 离线'}</span>`);
    if (device.online && device.videoSources != null) {
      tags.push(`<span class="device-tag-video">${device.videoSources} 路视频源</span>`);
    }
    if (role === "firepower") {
      tags.push(`<span class="device-tag-point-role">已设为工位</span>`);
    }
    if (device.duplicateId) {
      tags.push(`<span class="device-tag-warn" title="检测到重复设备 ID">ID重复</span>`);
    }
    if (device.staleSnapshot) {
      tags.push(`<span class="device-tag-warn" title="设备状态快照待更新">快照待更</span>`);
    }
    container.innerHTML = tags.join('<span class="meta-divider">·</span>');
  }

  roleButton(label, action, disabled = false, title = "", isRevert = false) {
    const button = document.createElement("button");
    button.type = "button";
    button.className = "btn btn-sm btn-tactical-role" + (isRevert ? " btn-tactical-role-revert" : "");
    button.textContent = label;
    button.disabled = disabled;
    if (title) button.title = title;
    button.addEventListener("click", e => {
      e.stopPropagation();
      action();
    });
    return button;
  }

  updateVideoButtons() {
    for (const [id, card] of this.videoCards) {
      const open = this.cameraHud.activeUav?.id === id;
      card.classList.toggle("selected", open);
      card.setAttribute("aria-pressed", String(open));
    }
  }

  highlightPoint(selectedId) {
    for (const [id, card] of this.pointCards) {
      card.classList.toggle("selected", id === selectedId);
    }
  }
}

export { StationPanel as ArtilleryPanel };
