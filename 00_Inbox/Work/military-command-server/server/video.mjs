import { spawn } from "node:child_process";
import { readFile } from "node:fs/promises";

const BOUNDARY = "uavvideo";
const STREAM_TYPE = `multipart/x-mixed-replace; boundary=${BOUNDARY}`;
const DEFAULT_UAV_ID = "UAV-RECON-01";
const CONFIG_ID = /^[a-zA-Z0-9][a-zA-Z0-9_-]{0,63}$/;

export async function loadVideoConfig(source) {
  try {
    const config = typeof source === "object" && source !== null
      ? source
      : JSON.parse((await readFile(source, "utf8")).replace(/^\uFEFF/, ""));
    if (!config || typeof config !== "object" || Array.isArray(config)) return { state: "invalid" };

    const { rtspUrl = "", ffmpegPath = "ffmpeg", rtspTimeoutOption = "timeout", streams: channels = [] } = config;
    if (typeof rtspUrl !== "string" || typeof ffmpegPath !== "string" || !ffmpegPath.trim()
        || !["timeout", "stimeout"].includes(rtspTimeoutOption) || !Array.isArray(channels)) {
      return { state: "invalid" };
    }

    const streams = [];
    const ids = new Set();
    const addStream = (uavId, id, label, address) => {
      const key = uavId + ":" + id;
      if (typeof id !== "string" || !CONFIG_ID.test(id) || typeof uavId !== "string" || !CONFIG_ID.test(uavId) || ids.has(key)
          || typeof label !== "string" || !label.trim() || typeof address !== "string") {
        throw new Error("Invalid video channel");
      }
      ids.add(key);
      let rtspUrl = address.trim();
      if (rtspUrl) {
        const url = new URL(rtspUrl);
        if (url.protocol !== "rtsp:" || !url.hostname) throw new Error("Invalid RTSP URL");
        rtspUrl = url.href;
      }
      streams.push({ uavId, id, label: label.trim(), rtspUrl });
    };
    for (const channel of channels) {
      if (!channel || typeof channel !== "object" || Array.isArray(channel)) return { state: "invalid" };
      addStream(channel.uavId ?? DEFAULT_UAV_ID, channel.id, channel.label ?? channel.id, channel.rtspUrl ?? "");
    }
    if (!streams.some(stream => stream.rtspUrl) && rtspUrl.trim()) {
      streams.length = 0;
      ids.clear();
      addStream(DEFAULT_UAV_ID, "default", "无人机视频", rtspUrl);
    }
    const state = streams.some(stream => stream.rtspUrl) ? "configured" : "unconfigured";
    return { state, streams, ffmpegPath: ffmpegPath.trim(), rtspTimeoutOption };
  } catch (err) {
    return { state: err.code === "ENOENT" ? "unconfigured" : "invalid" };
  }
}

export function serveVideo(req, res, config, pathname, streamId, uavId = DEFAULT_UAV_ID) {
  res.setHeader("Cache-Control", "no-store");
  res.setHeader("Cross-Origin-Resource-Policy", "same-origin");
  if (req.headers["sec-fetch-site"] && !["same-origin", "none"].includes(req.headers["sec-fetch-site"])) {
    res.writeHead(403).end();
    return;
  }

  const streams = config.streams?.filter(stream => stream.uavId === uavId) ?? [];
  if (pathname === "/api/video/status") {
    res.writeHead(200, { "Content-Type": "application/json; charset=utf-8" });
    const state = config.state === "invalid" ? "invalid"
      : streams.some(stream => stream.rtspUrl) ? "configured" : "unconfigured";
    const status = { state };
    if (streams.length) status.streams = streams.map(({ id, label, rtspUrl }) => ({ id, label, available: Boolean(rtspUrl) }));
    res.end(req.method === "HEAD" ? undefined : JSON.stringify(status));
    return;
  }

  if (config.state !== "configured") {
    res.writeHead(config.state === "invalid" ? 503 : 404).end();
    return;
  }
  const stream = streamId === null ? streams.find(channel => channel.rtspUrl) : streams.find(({ id }) => id === streamId);
  if (!stream?.rtspUrl) {
    res.writeHead(404).end();
    return;
  }
  if (req.method === "HEAD") {
    res.writeHead(200, { "Content-Type": STREAM_TYPE }).end();
    return;
  }

  const ffmpeg = spawn(config.ffmpegPath, [
    "-nostdin", "-hide_banner", "-loglevel", "error",
    "-rtsp_transport", "tcp", `-${config.rtspTimeoutOption}`, "5000000", "-i", stream.rtspUrl,
    "-map", "0:v:0", "-an", "-vf", "fps=12,scale=w='min(960,iw)':h=-2",
    "-c:v", "mjpeg", "-q:v", "5", "-f", "mpjpeg", "-boundary_tag", BOUNDARY, "pipe:1"
  ], { stdio: ["ignore", "pipe", "ignore"], windowsHide: true });

  const deadline = setTimeout(() => finish(504), 12_000);

  function finish(status) {
    clearTimeout(deadline);
    if (ffmpeg.exitCode === null && !ffmpeg.killed) ffmpeg.kill();
    if (res.destroyed || res.writableEnded) return;
    if (res.headersSent) res.destroy();
    else res.writeHead(status).end();
  }

  res.once("close", () => finish(502));
  ffmpeg.once("error", err => finish(err.code === "ENOENT" ? 503 : 502));
  ffmpeg.once("close", () => finish(502));
  ffmpeg.stdout.on("error", () => finish(502));
  ffmpeg.stdout.once("data", chunk => {
    if (res.destroyed || res.writableEnded) return;
    clearTimeout(deadline);
    res.writeHead(200, { "Content-Type": STREAM_TYPE });
    res.write(chunk);
    ffmpeg.stdout.pipe(res, { end: false });
  });
}
