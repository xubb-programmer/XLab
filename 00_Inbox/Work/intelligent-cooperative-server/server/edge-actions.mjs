import { projectGaussKruger } from "./gauss-kruger.mjs";

export const edgeDeviceIdPattern = /^[a-zA-Z0-9][a-zA-Z0-9._-]{0,63}$/;
const notSent = error => ({ state: "not_sent", pause: true, error });
const unknown = (error, timeout = false) => ({ state: "unknown", pause: true, error, timeout });

export function createEdgeActionSender(client) {
  const subscriptions = new Map();
  const pending = new Map();

  function subscribe(prefix) {
    if (!subscriptions.has(prefix)) {
      const topics = [`${prefix}/command-results`, `${prefix}/data`];
      const subscription = client.subscribeAsync(topics, { qos: 1 }).then(granted => {
        if (!topics.every(topic => granted.some(entry => entry.topic === topic && entry.qos <= 1))) {
          throw new Error("Broker 拒绝订阅 EdgeAgent 回执");
        }
      });
      subscriptions.set(prefix, subscription);
      void subscription.catch(() => {
        if (subscriptions.get(prefix) === subscription) subscriptions.delete(prefix);
      });
    }
    return subscriptions.get(prefix);
  }

  client.on("close", () => {
    subscriptions.clear();
    for (const request of pending.values()) {
      request.finish(request.dispatched
        ? unknown("MQTT 已断开，上报结果未知")
        : notSent("MQTT 已断开，上报未发出"));
    }
  });

  client.on("message", (topic, message, packet) => {
    const match = /^edge\/v1\/devices\/([^/]+)\/(command-results|data)$/.exec(topic);
    if (!match || packet.retain || message.length > 65_536 || pending.size === 0) return;
    const [, deviceId, kind] = match;
    try {
      const envelope = JSON.parse(message.toString("utf8"));
      if (kind === "command-results") {
        const request = pending.get(envelope?.id);
        if (request?.dispatched && request.deviceId === deviceId && envelope.status === "failed") {
          request.finish(notSent(envelope.error?.message || "EdgeAgent 转发失败"));
        }
        return;
      }
      if (envelope?.transport !== "tcp" || typeof envelope.data !== "string" ||
          !/^(?:[\da-f]{2})+$/i.test(envelope.data)) return;
      const response = JSON.parse(Buffer.from(envelope.data, "hex").toString("utf8"));
      const request = pending.get(response?.request_id);
      if (!request?.dispatched || request.deviceId !== deviceId || typeof response.ok !== "boolean") return;
      if (response.ok === true) {
        request.finish({ state: "confirmed" });
        return;
      }
      const error = typeof response.error === "string" ? response.error : "接收端返回失败";
      const queueFull = response.code === "queue_full" || error === "action queue full";
      const busy = queueFull || response.code === "busy";
      request.finish(response.processed === false || (queueFull && response.processed !== true)
        ? { state: "rejected", pause: busy, error }
        : unknown(error));
    } catch {
      // 其他设备上行或无法关联的回执由各自的业务处理。
    }
  });

  return function sendEdgeAction({ deviceId, params, requestId, timeoutMs, endpoint }) {
    if (typeof deviceId !== "string" || !edgeDeviceIdPattern.test(deviceId)) {
      return Promise.resolve(notSent("EdgeAgent 设备 ID 无效"));
    }
    if (!client.connected) return Promise.resolve(notSent("MQTT 未连接，上报未发出"));
    const prefix = `edge/v1/devices/${deviceId}`;
    const { x, y } = projectGaussKruger(params.lon, params.lat);
    if (!Number.isFinite(x) || !Number.isFinite(y)) {
      return Promise.resolve(notSent("高斯坐标转换失败，上报未发出"));
    }
    const wireParams = { ...params, x: Number(x.toFixed(2)), y: Number(y.toFixed(2)) };
    const bytes = Buffer.from(JSON.stringify({ name: "Action", params: wireParams, request_id: requestId }) + "\n");
    const commandPayload = { id: requestId, transport: "tcp", data: bytes.toString("hex") };
    if (endpoint && typeof endpoint.address === "string" && endpoint.address) {
      commandPayload.meta = {
        endpoint: {
          address: endpoint.address,
          port: Number(endpoint.port) || 5567
        }
      };
    }
    const command = JSON.stringify(commandPayload);

    return new Promise(resolve => {
      const request = {
        deviceId,
        dispatched: false,
        finish(result) {
          if (pending.get(requestId) !== request) return;
          pending.delete(requestId);
          clearTimeout(timer);
          resolve(result);
        }
      };
      const timer = setTimeout(() => request.finish(request.dispatched
        ? unknown("等待接收端回执超时", true)
        : notSent("订阅 EdgeAgent 回执超时，上报未发出")), timeoutMs);
      pending.set(requestId, request);

      subscribe(prefix).then(() => {
        if (pending.get(requestId) !== request) return;
        if (!client.connected) return request.finish(notSent("MQTT 已断开，上报未发出"));
        request.dispatched = true;
        // 上报命令不保留，避免 MQTT 重投造成接收端重复处理。
        client.publish(`${prefix}/commands`, command, { qos: 0, retain: false }, error => {
          if (error) request.finish(unknown(`发布 EdgeAgent 命令后状态异常：${error.message}`));
        });
      }).catch(error => request.finish(request.dispatched ? unknown(error.message) : notSent(error.message)));
    });
  };
}
