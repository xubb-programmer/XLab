import assert from "node:assert/strict";
import { createServer as createTcpServer } from "node:net";
import { openDeviceSimulation } from "../server/device-simulation.mjs";
import { getCooperativeDatabase } from "../server/db.mjs";

console.log("=== 开始自测：态势上报触发控制指令改用 EdgeAgent ===");

// 1. 创建简单的 Mock MQTT Broker
class MockBroker {
  constructor() {
    this.server = createTcpServer(socket => this.handleClient(socket));
    this.clients = new Set();
    this.publishedMessages = [];
  }

  async listen() {
    return new Promise(resolve => {
      this.server.listen(0, "127.0.0.1", () => {
        this.port = this.server.address().port;
        resolve();
      });
    });
  }

  handleClient(socket) {
    this.clients.add(socket);
    let buffer = Buffer.alloc(0);

    socket.on("data", chunk => {
      buffer = Buffer.concat([buffer, chunk]);
      while (buffer.length > 0) {
        const type = buffer[0] >> 4;
        if (buffer.length < 2) break;
        let multiplier = 1;
        let length = 0;
        let digit = 0;
        let i = 1;
        do {
          if (i >= buffer.length) return;
          digit = buffer[i++];
          length += (digit & 127) * multiplier;
          multiplier *= 128;
        } while ((digit & 128) !== 0);

        const totalPacketLength = i + length;
        if (buffer.length < totalPacketLength) break;

        const packet = buffer.subarray(0, totalPacketLength);
        buffer = buffer.subarray(totalPacketLength);

        if (type === 1) { // CONNECT
          socket.write(Buffer.from([0x20, 0x02, 0x00, 0x00]));
        } else if (type === 8) { // SUBSCRIBE
          socket.write(Buffer.from([0x90, 0x04, packet.readUInt8(i), packet.readUInt8(i + 1), 0x01, 0x01]));
        } else if (type === 3) { // PUBLISH
          const qos = (packet[0] & 0x06) >> 1;
          let offset = i;
          const topicLen = packet.readUInt16BE(offset);
          offset += 2;
          const topic = packet.subarray(offset, offset + topicLen).toString("utf8");
          offset += topicLen;
          let packetId = 0;
          if (qos > 0) {
            packetId = packet.readUInt16BE(offset);
            offset += 2;
            socket.write(Buffer.from([0x40, 0x02, (packetId >> 8) & 0xff, packetId & 0xff]));
          }
          const payload = packet.subarray(offset).toString("utf8");
          this.publishedMessages.push({ topic, payload });
          this.onMessage?.({ topic, payload, socket });
        }
      }
    });

    socket.on("close", () => this.clients.delete(socket));
  }

  publish(topic, payload) {
    const topicBuf = Buffer.from(topic, "utf8");
    const payloadBuf = Buffer.from(payload, "utf8");
    const varHeader = Buffer.alloc(2 + topicBuf.length);
    varHeader.writeUInt16BE(topicBuf.length, 0);
    topicBuf.copy(varHeader, 2);

    const totalLen = varHeader.length + payloadBuf.length;
    const remainingLenBytes = [];
    let rem = totalLen;
    do {
      let digit = rem % 128;
      rem = Math.floor(rem / 128);
      if (rem > 0) digit |= 128;
      remainingLenBytes.push(digit);
    } while (rem > 0);

    const packet = Buffer.concat([
      Buffer.from([0x30, ...remainingLenBytes]),
      varHeader,
      payloadBuf
    ]);
    for (const client of this.clients) {
      client.write(packet);
    }
  }

  async close() {
    for (const client of this.clients) client.destroy();
    return new Promise(resolve => this.server.close(resolve));
  }
}

const db = getCooperativeDatabase(":memory:");

const mockScenario = {
  read: async () => ({
    firepowerPoints: [],
    scenarioUavs: [{ id: "UAV-01", name: "1号侦察机" }]
  }),
  enqueue: async fn => fn()
};

async function run() {
  const broker = new MockBroker();
  await broker.listen();

  const mqttConfig = {
    brokerUrl: `mqtt://127.0.0.1:${broker.port}`,
    topic: "edge/v1/devices/target-input-device/data",
    deviceStatusTopic: "edge/v1/devices/device-status-device/data",
    deviceStatusRelayIp: "",
    username: "",
    password: ""
  };

  // 先插入模拟设备观测状态
  db.prepare(`
    INSERT INTO device_snapshots (identity, report)
    VALUES (?, ?)
  `).run("uav-01", JSON.stringify({
    identity: "uav-01",
    key: "UAV-01",
    deviceId: "UAV-01",
    deviceIp: "192.168.10.130",
    name: "1号侦察机",
    lastSeen: Date.now()
  }));

  const simulation = await openDeviceSimulation("timeout", mqttConfig, mockScenario, db);

  // 等待 MQTT 连接成功
  await new Promise(r => setTimeout(r, 400));

  // --- 测试用例 1: 正常发送态势指令，EdgeAgent 成功确认 ---
  console.log("\n[Test 1] 测试通过 EdgeAgent 发送态势上报指令 (Success)...");
  let lastCommandId = null;
  broker.onMessage = ({ topic, payload }) => {
    if (topic === "edge/v1/devices/target-input-device/commands") {
      const envelope = JSON.parse(payload);
      console.log("  -> MockBroker 拦截到 EdgeAgent 下行命令:");
      console.log("     id:", envelope.id);
      console.log("     transport:", envelope.transport);
      console.log("     endpoint:", envelope.meta?.endpoint);
      assert.equal(envelope.transport, "udp");
      assert.equal(envelope.meta?.endpoint?.address, "192.168.10.130");
      assert.equal(envelope.meta?.endpoint?.port, 28081);

      // 解码 Hex 检查内部业务 JSON
      const decodedPayload = Buffer.from(envelope.data, "hex").toString("utf8");
      console.log("     hex解码内容:", decodedPayload);
      const parsedCmd = JSON.parse(decodedPayload);
      assert.equal(parsedCmd["定时上报"], true);
      assert.equal(parsedCmd["上报间隔毫秒"], 2000);
      assert.equal(parsedCmd["上报端口"], 28080);
      lastCommandId = envelope.id;

      // 模拟 EdgeAgent 立即在 command-results 发布成功回执
      setTimeout(() => {
        broker.publish("edge/v1/devices/target-input-device/command-results", JSON.stringify({
          id: envelope.id,
          status: "sent"
        }));
      }, 50);
    }
  };

  // 模拟 HTTP 请求触发 /situation-report
  let resStatus = 0;
  let resHeaders = {};
  let resBody = "";
  const createMockReq = bodyObj => ({
    method: "POST",
    headers: { "content-type": "application/json" },
    setEncoding() {},
    [Symbol.asyncIterator]: async function* () {
      yield Buffer.from(JSON.stringify(bodyObj));
    }
  });
  const mockRes = {
    writeHead(status, headers) {
      resStatus = status;
      resHeaders = headers;
    },
    end(body) {
      resBody = body;
    }
  };

  await simulation.serve(createMockReq({ key: "UAV-01" }), mockRes, new URL("http://localhost/api/device-simulation/situation-report"));
  console.log("  -> HTTP 响应状态码:", resStatus);
  console.log("  -> HTTP 响应数据:", resBody);
  assert.equal(resStatus, 200);
  const respJson = JSON.parse(resBody);
  assert.equal(respJson.sent, true);
  assert.equal(respJson.via, "edgeagent");
  assert.equal(respJson.address, "192.168.10.130");
  assert.equal(respJson.port, 28081);
  console.log("  ✓ Test 1 通过：成功通过 EdgeAgent 封装下发并在收到 sent 回执后返回 200！");

  // --- 测试用例 2: EdgeAgent 报告发送失败 (Failed) ---
  console.log("\n[Test 2] 测试 EdgeAgent 发送失败回执 (Failed)...");
  broker.onMessage = ({ topic, payload }) => {
    if (topic === "edge/v1/devices/target-input-device/commands") {
      const envelope = JSON.parse(payload);
      setTimeout(() => {
        broker.publish("edge/v1/devices/target-input-device/command-results", JSON.stringify({
          id: envelope.id,
          status: "failed",
          error: {
            code: "udp_target_rejected",
            message: "目标地址不在 allowed_peers 中"
          }
        }));
      }, 50);
    }
  };

  await simulation.serve(createMockReq({ key: "UAV-01" }), mockRes, new URL("http://localhost/api/device-simulation/situation-report"));
  console.log("  -> HTTP 响应状态码:", resStatus);
  console.log("  -> HTTP 响应数据:", resBody);
  assert.equal(resStatus, 502);
  const errJson = JSON.parse(resBody);
  assert.match(errJson.error, /udp_target_rejected/);
  console.log("  ✓ Test 2 通过：EdgeAgent 发送失败时正确返回 502 并暴露具体错误原因！");

  // --- 测试用例 3: 手动指定 deviceIp 兜底机制 (即使无组播心跳也能下发) ---
  console.log("\n[Test 3] 测试手动指定 deviceIp 兜底机制...");
  let lastSentCommand = null;
  broker.onMessage = ({ topic, payload }) => {
    if (topic === "edge/v1/devices/target-input-device/commands") {
      const envelope = JSON.parse(payload);
      lastSentCommand = envelope;
      setTimeout(() => {
        broker.publish("edge/v1/devices/target-input-device/command-results", JSON.stringify({
          id: envelope.id,
          status: "sent"
        }));
      }, 50);
    }
  };

  await simulation.serve(
    createMockReq({ key: "UNKNOWN-UAV", deviceIp: "192.168.0.88" }),
    mockRes,
    new URL("http://localhost/api/device-simulation/situation-report")
  );
  console.log("  -> HTTP 响应状态码:", resStatus);
  console.log("  -> HTTP 响应数据:", resBody);
  assert.equal(resStatus, 200);
  const respJson3 = JSON.parse(resBody);
  assert.equal(respJson3.address, "192.168.0.88");
  assert.equal(lastSentCommand?.meta?.endpoint?.address, "192.168.0.88");
  console.log("  ✓ Test 3 通过：手动指定 deviceIp 成功兜底并经由 EdgeAgent 代理下发！");

  // 收尾
  simulation.close();
  await broker.close();
  console.log("\n=== 全部自测用例通过！===");
}

run().catch(err => {
  console.error("自测失败:", err);
  process.exit(1);
});
