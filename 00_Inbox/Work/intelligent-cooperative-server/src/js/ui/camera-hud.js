import { readJpegFrames } from "../core/mjpeg.js";
import { eventBus } from "../core/event-bus.js";
import { getPlatformName } from "../core/platform-labels.js";

export class CameraHud {
  constructor() {
    this.activeUav = null;
    this.preferredStreams = new Map();
    this.videoAttempt = 0;
    this.initDom();
  }

  initDom() {
    const mapViewport = document.querySelector(".map-viewport");
    if (!mapViewport) return;

    this.container = document.createElement("div");
    this.container.id = "uav-video-panel";
    this.container.className = "camera-hud-panel";
    this.container.hidden = true;
    this.container.innerHTML = `
      <div class="camera-hud-header">
        <div class="camera-hud-heading">
          <span class="status-dot-uav online" aria-hidden="true"></span>
          <strong id="hud-pod-title">无人机视频</strong>
        </div>
        <select id="camera-stream-select" class="select-input camera-stream-picker" aria-label="视频通道" hidden></select>
        <button type="button" class="hud-chip-btn" id="btn-hud-to-matrix" title="打开视频总览">总览</button>
      </div>
      <div class="camera-hud-body">
        <div class="camera-video-viewport">
          <img id="camera-video" alt="无人机实时画面" hidden>
          
          <!-- HUD OSD 引导刻度层 -->
          <div class="camera-hud-osd-overlay" id="camera-hud-osd" hidden>
            <div class="hud-osd-top">
              <span class="hud-osd-live-badge"><span class="hud-osd-live-dot"></span> 实时画面</span>
              <span class="hud-osd-cam-type" id="hud-cam-tag" hidden></span>
            </div>
            
            <div class="hud-osd-center-reticle">
              <div class="reticle-crosshair-h"></div>
              <div class="reticle-crosshair-v"></div>
              <div class="reticle-center-circle"></div>
              <div class="reticle-center-dot"></div>
            </div>

            <div class="hud-osd-bracket-tl"></div>
            <div class="hud-osd-bracket-tr"></div>
            <div class="hud-osd-bracket-bl"></div>
            <div class="hud-osd-bracket-br"></div>

          </div>

          <div class="camera-video-message">
            <span role="status" id="camera-video-status">正在读取视频配置…</span>
            <button type="button" class="hud-chip-btn" id="btn-camera-retry" hidden>重试</button>
          </div>
        </div>
      </div>
    `;
    mapViewport.appendChild(this.container);

    const get = id => this.container.querySelector("#" + id);
    this.title = get("hud-pod-title");
    this.videoImage = get("camera-video");
    this.videoStatus = get("camera-video-status");
    this.retryButton = get("btn-camera-retry");
    this.streamSelect = get("camera-stream-select");
    this.osdOverlay = get("camera-hud-osd");
    this.camTag = get("hud-cam-tag");

    this.streamSelect.addEventListener("change", () => {
      this.connectVideo(this.streamSelect.value);
    });
    this.retryButton.addEventListener("click", () => this.connectVideo());

    const btnMatrix = get("btn-hud-to-matrix");
    btnMatrix?.addEventListener("click", () => {
      const source = this.activeUav ? {
        seatId: this.activeUav.id,
        channelId: this.streamSelect.value
      } : null;
      eventBus.emit("recon-video:open-matrix", source);
    });
  }

  toggle(uav) {
    if (!this.container) return;
    this.activeUav = this.activeUav?.id === uav.id ? null : uav;
    this.container.hidden = !this.activeUav;
    if (!this.activeUav) {
      this.stopVideo();
      return;
    }
    const name = getPlatformName(uav, "recon");
    this.title.textContent = name;
    this.title.title = name;
    this.selectStream([]);
    this.connectVideo();
  }

  selectStream(streams, preferredId) {
    const channel = streams.find(stream => stream.id === preferredId)
      ?? streams.find(stream => stream.available !== false)
      ?? streams[0];
    this.streamSelect.replaceChildren(...streams.map(({ id, label }) => {
      const option = new Option(this.shortChannelName(label), id);
      option.title = label;
      return option;
    }));
    this.streamSelect.value = channel?.id ?? "";
    this.streamSelect.title = channel?.label ?? "";
    this.streamSelect.hidden = streams.length < 2;
    this.camTag.textContent = channel?.label ? this.shortChannelName(channel.label) : "";
    this.camTag.hidden = !channel?.label;
    return channel;
  }

  shortChannelName(label) {
    for (const name of [this.activeUav?.name, this.title.textContent]) {
      if (!name || !label.startsWith(name)) continue;
      const suffix = label.slice(name.length);
      const shortName = suffix.replace(/^\s*[-_·:：–—]\s*/u, "");
      if (shortName && shortName !== suffix) return shortName;
    }
    return label;
  }

  stopVideo() {
    this.videoAttempt++;
    this.videoController?.abort();
    if (this.osdOverlay) this.osdOverlay.hidden = true;
    if (!this.videoImage) return;
    this.videoImage.removeAttribute("src");
    this.videoImage.hidden = true;
    if (this.frameUrl) URL.revokeObjectURL(this.frameUrl);
    this.frameUrl = null;
  }

  showVideoStatus(message, canRetry = false) {
    this.videoStatus.textContent = message;
    this.videoStatus.parentElement.hidden = !message;
    this.retryButton.hidden = !canRetry;
  }

  async connectVideo(preferredId = this.preferredStreams.get(this.activeUav?.id)) {
    const uavId = this.activeUav?.id;
    if (!uavId) return;
    const videoApi = this.activeUav.simulation ? "/api/device-simulation/video" : "/api/video";
    this.stopVideo();
    const attempt = this.videoAttempt;
    const controller = new AbortController();
    this.videoController = controller;
    let idleTimer;
    let failure = "无法读取视频配置";
    this.showVideoStatus("正在读取视频配置…");
    try {
      const query = new URLSearchParams({ uav: uavId });
      const response = await fetch(videoApi + "/status?" + query, {
        cache: "no-store",
        signal: AbortSignal.any([controller.signal, AbortSignal.timeout(5000)])
      });
      if (!response.ok) throw new Error("Video config unavailable");
      const { state, streams = [] } = await response.json();
      if (attempt !== this.videoAttempt) return;
      const channel = this.selectStream(streams, preferredId);
      if (state === "invalid" || !channel) {
        this.showVideoStatus(state === "invalid" ? "视频配置无效" : "未配置 RTSP 视频");
        return;
      }
      this.preferredStreams.set(uavId, channel.id);
      if (state !== "configured" || channel.available === false) {
        this.showVideoStatus(this.activeUav.simulation ? `「${channel.label}」当前无可播放的 RTSP 流`
          : `「${channel.label}」未配置 RTSP 地址`);
        return;
      }

      failure = "无法播放 RTSP 视频";
      this.showVideoStatus("正在建立 RTSP 专线连接…");
      idleTimer = setTimeout(() => controller.abort(), 14_000);
      query.set("id", channel.id);
      const stream = await fetch(videoApi + "/stream?" + query, { cache: "no-store", signal: controller.signal });
      if (!stream.ok || !stream.body) throw new Error("Video unavailable");

      for await (const frame of readJpegFrames(stream.body)) {
        if (attempt !== this.videoAttempt) return;
        clearTimeout(idleTimer);
        idleTimer = setTimeout(() => controller.abort(), 8000);
        if (this.frameUrl) URL.revokeObjectURL(this.frameUrl);
        this.frameUrl = URL.createObjectURL(frame);
        this.videoImage.src = this.frameUrl;
        await this.videoImage.decode();
        if (attempt !== this.videoAttempt) return;
        if (this.videoImage.hidden) {
          this.videoImage.hidden = false;
          if (this.osdOverlay) this.osdOverlay.hidden = false;
          this.showVideoStatus("");
          failure = "视频连接已断开";
        }
      }
      throw new Error("Video ended");
    } catch {
      if (attempt === this.videoAttempt) {
        this.stopVideo();
        this.showVideoStatus(failure, true);
      }
    } finally {
      clearTimeout(idleTimer);
    }
  }
}
