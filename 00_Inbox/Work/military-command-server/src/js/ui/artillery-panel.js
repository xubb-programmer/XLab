import { escapeHtml } from "../core/html.js";
import { eventBus } from "../core/event-bus.js";
import { hasGeographicPosition } from "../core/coordinates.js";
import { getPlatformDeviceId, getPlatformDisplayName } from "../core/platform-labels.js";

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

export class ArtilleryPanel {
  constructor(cameraHud, viewerManager) {
    this.cameraHud = cameraHud;
    this.viewerManager = viewerManager;
    this.uavList = document.getElementById("uav-list-container");
    this.firepowerList = document.getElementById("firepower-list-container");
    this.videoCards = new Map();
    this.pointCards = new Map();

    eventBus.on("battery:selected", id => this.highlightPoint(id));
    eventBus.on("selection:cleared", () => this.highlightPoint(null));
  }

  update(uavs, points, simulation = null) {
    this.videoCards.clear();
    this.pointCards.clear();
    if (this.cameraHud.activeUav?.simulation && !uavs.some(uav => uav.id === this.cameraHud.activeUav.id)) {
      this.cameraHud.toggle(this.cameraHud.activeUav);
    }
    this.renderList(this.uavList, uavs.map(uav => this.makeUavCard(uav, simulation)),
      simulation ? "等待侦察席广播..." : "【无侦察席】");
    this.renderList(this.firepowerList, points.map(point => this.makePointCard(point, simulation)),
      simulation ? "暂无火力点 (可从侦察席转换)" : "【无火力点】");
    this.updateVideoButtons();
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

    const deviceId = getPlatformDeviceId(uav);
    const displayName = getPlatformDisplayName(uav, "recon");
    const videoCount = uav.videoSources ?? uav.videoChannels?.length ?? 0;
    const isOnline = uav.online === true;
    const mounted = uav.mountedTo;

    card.innerHTML = `
      <div class="card-corner-tl"></div>
      <div class="card-corner-br"></div>
      
      <div class="uav-header">
        <div class="uav-title">
          <span class="status-dot-uav ${isOnline ? 'online' : 'standby'}" aria-hidden="true"></span>
          <strong>${escapeHtml(displayName)}</strong>
        </div>
        <div class="header-tags">
          <button type="button" class="btn btn-sm btn-firepower-config btn-tactical-edit-point" aria-label="配置侦察席" title="配置侦察席参数">
            <span class="btn-text">配置</span>
          </button>
        </div>
      </div>

      <div class="card-essential-info-row">
        <div class="essential-meta-item">
          <span class="meta-label">设备ID</span>
          <strong class="meta-val-id mono">${escapeHtml(deviceId || "未绑定设备")}</strong>
        </div>
        <div class="essential-actions-group" aria-label="侦察席在线状态">
          <span class="status-essential-tag btn-uav-video ${isOnline ? 'online' : 'standby'}" title="${videoCount} 路视频源，点击卡片打开视频">
            ${isOnline ? '● 就绪' : '○ 离线'}
          </span>
        </div>
      </div>

      <div class="card-affiliation-row">
        <div class="recon-mount-status">
          <span class="meta-label">火力挂载</span>
          <span class="recon-mount-tag ${mounted ? 'is-mounted' : 'is-unmounted'}" title="${mounted ? `已挂载至 ${escapeHtml(mounted.battalion)}-${escapeHtml(mounted.company)}-${escapeHtml(mounted.name)}` : '未挂载火力点'}">
            ${mounted ? `${escapeHtml(mounted.battalion)}·${escapeHtml(mounted.company)}·${escapeHtml(mounted.name)}` : '空闲未挂载'}
          </span>
        </div>
        <div class="recon-mount-action">
          <button type="button" class="btn btn-xs btn-tactical-mount" title="选择挂载至哪个火力点">
            <span class="btn-text">${mounted ? '更改阵位' : '挂载阵位'}</span>
          </button>
        </div>
      </div>
      <div class="recon-mount-quick-picker" hidden>
        <select class="recon-quick-select text-input text-input-xs">
          <option value="">选择挂载火力点…</option>
        </select>
        <button type="button" class="btn btn-xs btn-confirm-mount">确认</button>
        ${mounted ? '<button type="button" class="btn btn-xs btn-unmount-recon">解绑</button>' : ''}
      </div>
    `;

    card.querySelector(".btn-tactical-edit-point")?.addEventListener("click", e => {
      e.stopPropagation();
      if (simulation) {
        simulation.edit(uav, "recon");
      }
    });

    const mountBtn = card.querySelector(".btn-tactical-mount");
    const picker = card.querySelector(".recon-mount-quick-picker");
    const select = card.querySelector(".recon-quick-select");
    const confirmBtn = card.querySelector(".btn-confirm-mount");
    const unmountBtn = card.querySelector(".btn-unmount-recon");

    mountBtn?.addEventListener("click", e => {
      e.stopPropagation();
      if (!simulation?.mount || !simulation?.getFirepowerPoints) return;
      const isOpen = !picker.hidden;
      if (isOpen) {
        picker.hidden = true;
      } else {
        const points = simulation.getFirepowerPoints();
        select.innerHTML = `<option value="">选择挂载火力点…</option>` +
          points.map(p => `<option value="${escapeHtml(p.id)}" ${mounted && mounted.id === p.id ? 'selected' : ''}>${escapeHtml(p.battalion || '1营')}-${escapeHtml(p.company || '1连')}-${escapeHtml(p.name)}</option>`).join("");
        picker.hidden = false;
      }
    });

    confirmBtn?.addEventListener("click", async e => {
      e.stopPropagation();
      const ptId = select.value;
      if (ptId && simulation?.mount) {
        confirmBtn.disabled = true;
        try {
          if (await simulation.mount(ptId, deviceId || uav.id)) picker.hidden = true;
        } finally {
          confirmBtn.disabled = false;
        }
      }
    });

    unmountBtn?.addEventListener("click", async e => {
      e.stopPropagation();
      if (mounted?.id && simulation?.mount) {
        if (await simulation.mount(mounted.id, "")) picker.hidden = true;
      }
    });

    picker?.addEventListener("click", e => e.stopPropagation());

    card.tabIndex = 0;
    const openVideo = () => {
      this.cameraHud.toggle(uav);
      this.updateVideoButtons();
    };
    card.addEventListener("click", openVideo);
    card.addEventListener("keydown", event => {
      if (event.key !== "Enter" && event.key !== " ") return;
      event.preventDefault();
      openVideo();
    });
    this.videoCards.set(uav.id, card);
    return card;
  }

  makePointCard(point, simulation) {
    const positioned = hasGeographicPosition(point);
    const isDevicePoint = Boolean(point.device || point.simulation);
    const card = document.createElement("article");
    card.className = "artillery-card tactical-card" + (positioned ? " is-positioned" : " is-unlocated");
    card.dataset.id = point.id;

    const deviceId = getPlatformDeviceId(point);
    const isOnline = point.online === true;
    const displayName = getPlatformDisplayName(point, "firepower");
    const affiliation = `${point.battalion || "1营"} / ${point.company || "1连"}`;
    const mountedDev = point.mountedDeviceId || "";

    card.innerHTML = `
      <div class="card-corner-tl"></div>
      <div class="card-corner-br"></div>

      <div class="artillery-header">
        <div class="artillery-title">
          <span class="status-dot-point ${isOnline ? 'online' : 'standby'}" aria-hidden="true"></span>
          <strong></strong>
          <span class="artillery-affiliation" title="战术编制隶属"></span>
        </div>
        <div class="header-tags">
          <button type="button" class="btn btn-sm btn-firepower-config btn-tactical-edit-point" aria-label="配置火力点参数" title="配置火力点参数">
            <span class="btn-text">配置</span>
          </button>
        </div>
      </div>

      <div class="card-essential-info-row">
        <div class="essential-meta-item">
          <span class="meta-label">挂载设备</span>
          <strong class="meta-val-id mono ${mountedDev ? 'highlight-cyan' : ''}">${escapeHtml(mountedDev || "未挂载")}</strong>
        </div>
        <div class="essential-actions-group">
          <span class="status-essential-tag ${isOnline ? 'online' : 'standby'}" title="${isOnline ? 'EdgeAgent转发设备在线就绪' : '未挂载或设备离线'}">
            ${isOnline ? '● 在线' : '○ 离线'}
          </span>
        </div>
      </div>
    `;

    card.querySelector(".artillery-title strong").textContent = displayName;
    card.querySelector(".artillery-affiliation").textContent = affiliation;

    const editBtn = card.querySelector(".btn-tactical-edit-point");
    editBtn?.addEventListener("click", e => {
      e.stopPropagation();
      if (isDevicePoint && simulation?.edit) {
        simulation.edit(point);
      } else {
        eventBus.emit("firepower:openConfigModal", point);
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
        if (isDevicePoint && simulation?.edit) {
          simulation.edit(point);
        } else {
          eventBus.emit("firepower:openConfigModal", point);
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
      tags.push(`<span class="device-tag-point-role">已设为阵位</span>`);
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
