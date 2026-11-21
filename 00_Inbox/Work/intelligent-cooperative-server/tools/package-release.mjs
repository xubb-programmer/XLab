import { createWriteStream, existsSync, statSync } from "node:fs";
import { resolve } from "node:path";
import { execSync } from "node:child_process";
import { fileURLToPath } from "node:url";

const root = fileURLToPath(new URL("../", import.meta.url));
const isLite = process.argv.includes("--lite");

const baseFiles = [
  "src",
  "server",
  "config",
  "deployment-terms.json",
  "package.json",
  "package-lock.json",
  "README.md",
  "doc"
];

const mapFiles = [
  "data/offline-maps/combined-theaters/manifest.json",
  "data/offline-maps/combined-theaters/tiles.db"
];

const filesToPack = [...baseFiles];

if (!isLite) {
  const mapDb = resolve(root, "data/offline-maps/combined-theaters/tiles.db");
  const mapManifest = resolve(root, "data/offline-maps/combined-theaters/manifest.json");
  if (existsSync(mapDb) && existsSync(mapManifest)) {
    filesToPack.push("data/offline-maps/combined-theaters");
    const mb = (statSync(mapDb).size / (1024 * 1024)).toFixed(1);
    console.log(`[Package] 包含离线高清地图数据库 (${mb} MB, 覆盖朱日和/东南沿海战区)`);
  } else {
    console.warn("[Package] 警告: 未找到 tiles.db，将仅打包基础代码");
  }
}

const outName = isLite
  ? "intelligent-cooperative-server-lite.tar.gz"
  : "intelligent-cooperative-server-full.tar.gz";

const outPath = resolve(root, outName);
console.log(`[Package] 开始打包 -> ${outName}...`);

const fileList = filesToPack.filter(f => existsSync(resolve(root, f))).join(" ");
try {
  execSync(`tar -czf "${outPath}" ${fileList}`, { cwd: root, stdio: "inherit" });
  const sizeMb = (statSync(outPath).size / (1024 * 1024)).toFixed(2);
  console.log(`[Package] 打包成功! 文件: ${outName} (${sizeMb} MB)`);
} catch (e) {
  console.error("[Package] 打包失败:", e.message);
  process.exit(1);
}
