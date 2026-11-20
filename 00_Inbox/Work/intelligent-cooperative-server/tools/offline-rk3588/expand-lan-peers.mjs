import { readFile } from "node:fs/promises";

const [configPath] = process.argv.slice(2);
const config = JSON.parse((await readFile(configPath, "utf8")).replace(/^\uFEFF/, ""));
const lanPrefixes = ["192.168.0.", "192.168.10."];
const lanPeers = lanPrefixes.flatMap(prefix =>
  Array.from({ length: 254 }, (_, index) => `${prefix}${index + 1}:0`)
);

for (const [channelId, peers] of [
  ["target-input-udp", ["127.0.0.1:0", ...lanPeers]],
  ["device-status-udp", lanPeers]
]) {
  const channel = config.channels.find(channel => channel.channel_id === channelId && channel.type === "udp");
  if (!channel) throw new Error(`找不到 UDP 通道 ${channelId}`);
  channel.allowed_peers = [...new Set([...channel.allowed_peers, ...peers])];
}

process.stdout.write(JSON.stringify(config, null, 2) + "\n");
