import { readJpegFrames } from "../core/mjpeg.js";
import { eventBus } from "../core/event-bus.js";
import { getPlatformDeviceId, getPlatformName } from "../core/platform-labels.js";
import { t } from "../core/terms.js";

/**
 * 态势视频流播放器单元
 * 独立管理单个载荷视频通道的 MJPEG 接收、OSD 叠加、帧率统计与中止注销
 */
class CellStreamPlayer {
  constructor(cellElement, stream, onFocusUav, onToggleMaximize) {
    this.cell = cellElement;
    this.stream = stream;
    this.onFocusUav = onFocusUav;
    this.onToggleMaximize = onToggleMaximize;

    this.controller = null;
    this.frameUrl = null;
    this.frameCount = 0;
    this.fpsInterval = null;

    this.imgEl = this.cell.querySelector(".cell-video-img");
    this.msgOverlay = this.cell.querySelector(".cell-message-overlay");
    this.msgText = this.cell.querySelector(".message-text");
    this.osdOverlay = this.cell.querySelector(".cell-hud-osd");
    this.statusPill = this.cell.querySelector(".cell-status-pill");
    this.fpsTag = this.cell.querySelector(".cell-fps-tag");
    this.retryBtn = this.cell.querySelector(".btn-cell-retry");

    this.bindEvents();
  }

  bindEvents() {
    this.retryBtn?.addEventListener("click", e => {
      e.stopPropagation();
      this.start();
    });

    const btnFocus = this.cell.querySelector(".btn-cell-focus");
    btnFocus?.addEventListener("click", e => {
      e.stopPropagation();
      this.onFocusUav(this.stream);
    });

    const btnMax = this.cell.querySelector(".btn-cell-expand");
    btnMax?.addEventListener("click", e => {
      e.stopPropagation();
      this.onToggleMaximize(this.stream.channelKey);
    });
  }

  showMessage(text, type = "info") {
    if (this.msgText) this.msgText.textContent = text;
    if (this.msgOverlay) {
      this.msgOverlay.hidden = false;
      this.msgOverlay.dataset.state = type;
    }
    if (this.retryBtn) {
      this.retryBtn.hidden = type !== "error";
    }
  }

  hideMessage() {
    if (this.msgOverlay) this.msgOverlay.hidden = true;
  }

  async start() {
    this.stop();
    if (!this.stream.available) {
      this.showMessage("此视频源当前无法播放", "standby");
      if (this.statusPill) {
        this.statusPill.className = "cell-status-pill standby";
        this.statusPill.textContent = "○ 暂不可播放";
      }
      return;
    }

    const controller = new AbortController();
    this.controller = controller;
    const { signal } = controller;

    this.showMessage("正在建立 RTSP 专线数据流…", "loading");
    if (this.statusPill) {
      this.statusPill.className = "cell-status-pill connecting";
      this.statusPill.innerHTML = '<span class="dot-connecting">◐</span> 正在连接';
    }

    let idleTimer = setTimeout(() => controller.abort(), 14000);
    try {
      const response = await fetch(this.stream.streamUrl, {
        cache: "no-store",
        signal
      });

      if (!response.ok || !response.body) {
        throw new Error(`视频通道返回状态: ${response.status}`);
      }
      signal.throwIfAborted();

      this.frameCount = 0;
      this.fpsInterval = setInterval(() => {
        if (this.fpsTag) {
          const fps = Math.min(30, this.frameCount);
          this.fpsTag.textContent = `${fps} FPS`;
        }
        this.frameCount = 0;
      }, 1000);

      for await (const frame of readJpegFrames(response.body)) {
        if (signal.aborted) break;

        if (this.frameUrl) {
          URL.revokeObjectURL(this.frameUrl);
        }
        this.frameUrl = URL.createObjectURL(frame);
        this.imgEl.src = this.frameUrl;
        await this.imgEl.decode();
        if (signal.aborted) break;

        clearTimeout(idleTimer);
        idleTimer = setTimeout(() => controller.abort(), 8000);
        this.frameCount++;

        if (this.imgEl.hidden) {
          this.imgEl.hidden = false;
          if (this.osdOverlay) this.osdOverlay.hidden = false;
          this.hideMessage();
          if (this.statusPill) {
            this.statusPill.className = "cell-status-pill live";
            this.statusPill.innerHTML = '<span class="dot-live">●</span> 实时推流';
          }
        }
      }
      throw new Error("视频传输流终止");
    } catch (err) {
      if (this.controller === controller) {
        this.stop();
        this.showMessage("视频连接已断开，请核实 RTSP 推流状态", "error");
        if (this.statusPill) {
          this.statusPill.className = "cell-status-pill error";
          this.statusPill.innerHTML = '<span class="dot-error">✕</span> 链路中断';
        }
      }
    } finally {
      clearTimeout(idleTimer);
    }
  }

  stop() {
    if (this.controller) {
      this.controller.abort();
      this.controller = null;
    }
    if (this.fpsInterval) {
      clearInterval(this.fpsInterval);
      this.fpsInterval = null;
    }
    if (this.frameUrl) {
      URL.revokeObjectURL(this.frameUrl);
      this.frameUrl = null;
    }
    if (this.imgEl) {
      this.imgEl.removeAttribute("src");
      this.imgEl.hidden = true;
    }
    if (this.osdOverlay) {
      this.osdOverlay.hidden = true;
    }
  }
}

/**
 * 监测载荷全视频链路总览矩阵管理中心
 */
export class VideoMatrixModal {
  constructor(viewerManager, getUavsCallback, getScenarioUavs) {
    this.viewerManager = viewerManager;
    this.getUavsCallback = getUavsCallback;
    this.getScenarioUavs = getScenarioUavs;
    this.configuredUavs = [];

    this.modal = document.getElementById("page-video-matrix") || document.getElementById("recon-video-matrix-modal");
    this.grid = document.getElementById("video-matrix-grid");
    this.headerCount = document.getElementById("matrix-header-count");
    this.topbarBadge = document.getElementById("nav-video-source-count");
    this.sourceSelect = document.getElementById("matrix-source-select");
    this.title = document.getElementById("video-matrix-title");
    this.directory = document.getElementById("video-seat-directory");
    this.seatList = document.getElementById("video-seat-list");
    this.emptyList = document.getElementById("video-seat-empty");
    this.playback = document.getElementById("video-matrix-playback");
    this.footer = document.getElementById("video-matrix-footer");
    this.layoutControls = this.modal?.querySelector(".grid-layout-switcher");
    this.refreshButton = document.getElementById("btn-refresh-video-matrix");
    this.backButton = document.getElementById("btn-matrix-back-to-list");

    this.players = new Map();
    this.currentLayout = null;
    this.selectedSeatId = null;
    this.selectedChannelKey = null;
    this.preferredSource = null;
    this.visible = false;

    this.initDom();
    this.renderView();
    void this.refreshScenarioStreams();

    eventBus.on("devices:updated", () => {
      this.renderView();
      if (JSON.stringify(this.getScenarioUavs()) !== this.scenarioSignature) void this.refreshScenarioStreams();
    });
  }

  initDom() {
    if (!this.modal) return;

    this.seatList?.addEventListener("click", event => {
      const button = event.target.closest(".video-seat-button");
      if (!button) return;
      this.selectedSeatId = button.dataset.seatId;
      const preferred = this.preferredSource;
      this.selectedChannelKey = preferred?.seatId === this.selectedSeatId && preferred.channelId
        ? `${this.selectedSeatId}::${preferred.channelId}` : null;
      this.preferredSource = null;
      this.currentLayout = this.selectedChannelKey ? "1" : null;
      this.renderView();
      this.title.focus();
    });
    this.backButton?.addEventListener("click", () => this.returnToList());

    this.sourceSelect?.addEventListener("change", () => {
      this.selectedChannelKey = this.sourceSelect.value;
      this.setLayout("1");
    });

    // 布局切换按钮
    const layoutBtns = this.modal.querySelectorAll(".btn-matrix-layout");
    layoutBtns.forEach(btn => {
      btn.addEventListener("click", () => {
        const layout = btn.dataset.layout;
        this.setLayout(layout);
      });
    });

    // 刷新链路按钮
    this.refreshButton?.addEventListener("click", () => void this.refreshScenarioStreams(true));

    const returnToTactical = () => {
      this.close();
      eventBus.emit("page:switch", "tactical");
    };

    document.getElementById("btn-matrix-close-foot")?.addEventListener("click", returnToTactical);

    // 蒙层点击关闭与按 ESC 关闭
    this.modal.addEventListener("click", e => {
      if (e.target === this.modal) returnToTactical();
    });

    document.addEventListener("keydown", e => {
      if (e.key === "Escape" && this.visible) {
        if (this.selectedSeatId) this.returnToList();
        else returnToTactical();
      }
    });
  }

  async refreshScenarioStreams(restartStreams = false) {
    const uavs = this.getScenarioUavs();
    const signature = JSON.stringify(uavs);
    this.scenarioSignature = signature;
    const configuredUavs = await Promise.all(uavs.map(async uav => {
      try {
        const query = new URLSearchParams({ uav: uav.id });
        const response = await fetch(`/api/video/status?${query}`, {
          cache: "no-store",
          signal: AbortSignal.timeout(5000)
        });
        if (!response.ok) return null;
        const { state, streams } = await response.json();
        if (state !== "configured" || !Array.isArray(streams)) return null;
        return { ...uav, videoChannels: streams.filter(channel => channel.available && channel.id) };
      } catch {
        return null;
      }
    }));

    if (this.scenarioSignature !== signature) return;
    this.configuredUavs = configuredUavs.filter(Boolean);
    this.renderView(restartStreams);
  }

  getSeats() {
    const devices = this.getUavsCallback().filter(uav => !uav.isScenario);
    const deviceIds = new Set(devices.map(uav => uav.id));
    const configuredIds = new Set(this.getScenarioUavs().map(uav => uav.id));
    return [
      ...devices.map(uav => ({ uav, device: true })),
      ...this.configuredUavs.filter(uav => configuredIds.has(uav.id) && !deviceIds.has(uav.id)).map(uav => ({ uav, device: false }))
    ];
  }

  renderView(restartStreams = false) {
    const seats = this.getSeats();
    const total = seats.reduce((sum, { uav }) => sum + videoSourceCount(uav), 0);
    if (this.topbarBadge) this.topbarBadge.textContent = total ? `${total} 路视频源` : "暂无视频";
    if (!this.visible) return;

    const seat = seats.find(({ uav }) => uav.id === this.selectedSeatId);
    const playing = Boolean(seat);
    this.directory.hidden = playing;
    this.playback.hidden = !playing;
    this.footer.hidden = !playing;
    this.layoutControls.hidden = !playing;
    this.refreshButton.hidden = !playing;
    this.backButton.hidden = !playing;
    const count = seat ? videoSourceCount(seat.uav) : seats.length;
    this.headerCount.hidden = playing && count === 0;
    this.headerCount.textContent = playing ? `${count} 路视频源` : t("ui.reconSeatCount", `共 ${count} 个监测席`, { count });

    if (seat) {
      this.title.textContent = `${getPlatformName(seat.uav, "recon")} · 视频`;
      this.renderGrid(seat, restartStreams);
    } else {
      const seatRemoved = this.selectedSeatId !== null;
      this.selectedSeatId = null;
      this.stopPlayers();
      this.sourceSelect.parentElement.hidden = true;
      this.title.textContent = t("ui.reconSeatList", "监测席列表");
      this.renderSeatList(seats);
      if (seatRemoved) this.title.focus();
    }
  }

  renderSeatList(seats) {
    const rows = seats.map(({ uav, device }) => ({
      id: uav.id,
      name: getPlatformName(uav, "recon"),
      deviceId: getPlatformDeviceId(uav),
      count: videoSourceCount(uav),
      online: device && uav.online === true,
      status: device ? (uav.online ? "在线" : "离线") : "已配置"
    }));
    const signature = JSON.stringify(rows);
    if (signature === this.seatListSignature) return;
    this.seatListSignature = signature;
    this.seatList.hidden = rows.length === 0;
    this.emptyList.hidden = rows.length !== 0;
    const focusedId = this.seatList.contains(document.activeElement) ? document.activeElement.dataset.seatId : null;
    this.seatList.innerHTML = rows.map(row => `
      <li>
        <button type="button" class="btn video-seat-button" data-seat-id="${escapeHtml(row.id)}">
          <span class="video-seat-identity">
            <strong>${escapeHtml(row.name)}</strong>
            ${row.deviceId ? `<span>设备 ID ${escapeHtml(row.deviceId)}</span>` : ""}
          </span>
          <span class="video-seat-count">${row.count} 路视频源</span>
          <span class="video-seat-status${row.online ? " online" : ""}">${row.status}</span>
        </button>
      </li>
    `).join("");
    if (focusedId) this.focusSeat(focusedId);
  }

  focusSeat(seatId) {
    const button = [...this.seatList.querySelectorAll(".video-seat-button")]
      .find(item => item.dataset.seatId === seatId);
    (button ?? this.title).focus();
  }

  open(preferredSource = null) {
    if (!this.modal) return;
    this.visible = true;
    this.modal.hidden = false;
    this.modal.style.display = "flex";
    this.modal.classList.remove("is-hidden");

    this.preferredSource = preferredSource;
    this.selectedSeatId = null;
    this.renderView();
  }

  returnToList() {
    const seatId = this.selectedSeatId;
    this.selectedSeatId = null;
    this.renderView();
    this.focusSeat(seatId);
  }

  /**
   * 关闭矩阵并释放所有视频播放连接与内存
   */
  close() {
    if (!this.modal) return;
    this.visible = false;
    this.modal.hidden = true;
    this.modal.style.display = "none";
    this.modal.classList.add("is-hidden");
    this.stopPlayers();
  }

  stopPlayers() {
    // 彻底停止所有视频流，释放 ffmpeg 与 Blob URLs
    for (const player of this.players.values()) {
      player.stop();
    }
    this.players.clear();
    if (this.grid) this.grid.innerHTML = "";
  }

  /**
   * 设置分屏网格布局
   */
  setLayout(layout) {
    this.currentLayout = layout;
    this.renderView();
  }

  updateLayout() {
    const layout = this.currentLayout ?? "1";
    const layoutBtns = this.modal?.querySelectorAll(".btn-matrix-layout");
    layoutBtns?.forEach(btn => {
      btn.classList.toggle("active", btn.dataset.layout === layout);
    });

    if (this.grid) {
      this.grid.className = `video-matrix-grid layout-grid-${layout}`;
    }
  }

  toggleMaximize(channelKey) {
    if (this.currentLayout === "1" && this.selectedChannelKey === channelKey) {
      this.setLayout("2");
    } else {
      this.selectedChannelKey = channelKey;
      this.setLayout("1");
    }
  }

  updateSourceOptions(sources, selected) {
    if (!this.sourceSelect) return;
    const options = sources.map(source => ({
      value: source.channelKey,
      label: source.available ? source.label : `${source.label}（暂不可播放）`
    }));
    const currentOptions = this.sourceSelect.options;
    if (currentOptions.length !== options.length || options.some(({ value, label }, index) =>
      currentOptions[index].value !== value || currentOptions[index].text !== label
    )) {
      this.sourceSelect.replaceChildren(...options.map(({ value, label }) => new Option(label, value)));
    }
    this.sourceSelect.value = selected?.channelKey ?? "";
    this.sourceSelect.title = this.sourceSelect.selectedOptions[0]?.text ?? "";
    this.sourceSelect.parentElement.hidden = sources.length === 0;
  }

  focusUavOnMap(stream) {
    const uav = stream.uav;
    if (uav && typeof uav.longitude === "number" && typeof uav.latitude === "number") {
      eventBus.emit("page:switch", "tactical");
      this.viewerManager.flyToEntity(uav.longitude, uav.latitude, uav.altitude || 100);
      this.showToast(t("ui.focusReconUav", `已在三维地球中聚焦巡检无人机：${stream.uavName}`, { name: stream.uavName }));
    } else {
      this.showToast(`「${stream.uavName}」尚未标定经纬高，请先在左侧卡片点击【配置】`, "warning");
    }
  }

  showToast(message, type = "info") {
    let toast = document.getElementById("matrix-tactical-toast");
    if (!toast) {
      toast = document.createElement("div");
      toast.id = "matrix-tactical-toast";
      toast.className = "matrix-tactical-toast";
      this.modal.appendChild(toast);
    }
    toast.textContent = message;
    toast.dataset.type = type;
    toast.classList.add("visible");
    setTimeout(() => toast.classList.remove("visible"), 3200);
  }

  /**
   * 渲染视频流网格
   */
  renderGrid({ uav, device }, restartStreams) {
    if (!this.grid) return;
    const sources = collectStreams(uav, device);
    const playable = sources.filter(source => source.available);
    const selected = sources.find(source => source.channelKey === this.selectedChannelKey) ?? playable[0] ?? sources[0];
    if (selected) {
      this.selectedChannelKey = selected.channelKey;
      this.currentLayout ??= playable.length > 1 ? "2" : "1";
    }
    this.updateSourceOptions(sources, selected);
    this.updateLayout();

    const pair = selected?.available
      ? [selected, ...playable.filter(source => source !== selected)]
      : playable;
    const streams = this.currentLayout === "2" ? pair.slice(0, 2) : selected ? [selected] : [];
    if (streams.length === 0) {
      this.stopPlayers();
      this.grid.innerHTML = `
        <div class="video-matrix-empty">
          <div class="empty-title">${videoSourceCount(uav) ? "暂无可播放视频" : "暂无视频源"}</div>
        </div>
      `;
      return;
    }

    // 如果流清单发生变化，重建网格
    const isSame = this.players.size === streams.length && streams.every(stream => {
      const current = this.players.get(stream.channelKey)?.stream;
      return current?.streamUrl === stream.streamUrl && current.uavName === stream.uavName
        && current.uavIp === stream.uavIp && current.label === stream.label && current.available === stream.available;
    });

    if (!isSame) {
      this.stopPlayers();

      for (const stream of streams) {
        const cell = this.createCellDom(stream);
        this.grid.appendChild(cell);

        const player = new CellStreamPlayer(
          cell,
          stream,
          s => this.focusUavOnMap(s),
          key => this.toggleMaximize(key)
        );
        this.players.set(stream.channelKey, player);
        player.start();
      }
    } else {
      for (const stream of streams) {
        const player = this.players.get(stream.channelKey);
        player.stream = stream;
        if (restartStreams) player.start();
      }
    }
  }

  createCellDom(stream) {
    const cell = document.createElement("div");
    cell.className = "video-matrix-cell";
    cell.dataset.channelKey = stream.channelKey;

    cell.innerHTML = `
      <div class="cell-topbar">
        <div class="cell-title-area">
          <span class="cell-uav-badge">${escapeHtml(stream.uavName)}</span>
          <span class="cell-stream-name">${escapeHtml(stream.label)}</span>
        </div>
        <div class="cell-tags-area">
          <span class="cell-tag-ip mono">${escapeHtml(stream.uavIp)}</span>
          <span class="cell-status-pill connecting">
            <span class="dot-connecting">◐</span> 正在连接
          </span>
          <button type="button" class="btn btn-xs btn-cell-focus" title="在三维地球上聚焦该无人机">聚焦</button>
          <button type="button" class="btn btn-xs btn-cell-expand" title="单屏放大/还原">⛶</button>
        </div>
      </div>

      <div class="cell-viewport">
        <img class="cell-video-img" alt="${escapeHtml(stream.label)}" hidden>

        <!-- HUD OSD 引导刻度层 -->
        <div class="cell-hud-osd" hidden>
          <div class="hud-osd-top">
            <span class="hud-live-tag"><span class="hud-dot"></span> RTSP LIVE</span>
            <span class="hud-stream-id">${escapeHtml(stream.channelId)}</span>
          </div>

          <div class="cell-reticle">
            <div class="reticle-crosshair-h"></div>
            <div class="reticle-crosshair-v"></div>
            <div class="reticle-center-circle"></div>
            <div class="reticle-center-dot"></div>
          </div>

          <div class="hud-bracket-tl"></div>
          <div class="hud-bracket-tr"></div>
          <div class="hud-bracket-bl"></div>
          <div class="hud-bracket-br"></div>

          <div class="hud-osd-bottom">
            <span class="cell-fps-tag mono">12 FPS</span>
            <span class="hud-res-tag">1080P · H.264/MJPEG</span>
          </div>
        </div>

        <!-- 状态/待命/错误浮层 -->
        <div class="cell-message-overlay" data-state="loading">
          <div class="message-content">
            <span class="message-radar-icon"></span>
            <span class="message-text">正在建立 RTSP 专线连接…</span>
            <button type="button" class="btn btn-xs btn-cell-retry" hidden>⟳ 重试连接</button>
          </div>
        </div>
      </div>

      <div class="cell-bottom-bar">
        <span class="cell-meta-info">通道 ID: <strong class="mono">${escapeHtml(stream.channelId)}</strong></span>
        ${stream.available ? '<span class="cell-protocol-info mono">协议: RTSP TCP ➔ MJPEG</span>' : ""}
      </div>
    `;
    return cell;
  }
}

function videoSourceCount(uav) {
  return uav.videoSources ?? uav.videoChannels?.length ?? 0;
}

function collectStreams(uav, device) {
  const videoApi = device ? "/api/device-simulation/video" : "/api/video";
  return (uav.videoChannels ?? [])
    .filter(channel => channel.id)
    .map(channel => {
      const query = new URLSearchParams({ uav: uav.id, id: channel.id });
      return {
        channelKey: `${uav.id}::${channel.id}`,
        uavName: getPlatformName(uav, "recon"),
        uavIp: uav.deviceIp || uav.ip || "",
        uav,
        channelId: channel.id,
        label: channel.label || channel.id,
        available: Boolean(channel.available && (!device || uav.online)),
        streamUrl: `${videoApi}/stream?${query}`
      };
    });
}

function escapeHtml(str) {
  if (typeof str !== "string") return "";
  return str.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/"/g, "&quot;");
}

export { VideoMatrixModal as ReconVideoMatrixModal };
