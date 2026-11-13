import { backup, DatabaseSync } from "node:sqlite";
import { existsSync } from "node:fs";
import { mkdir, readFile, writeFile } from "node:fs/promises";
import { join } from "node:path";

const [source, destination] = process.argv.slice(2);
const sourceDatabase = join(source, "tiles.db");
const sourceManifest = join(source, "manifest.json");

if (!existsSync(sourceDatabase) && !existsSync(sourceManifest)) process.exit(0);

const manifest = JSON.parse(await readFile(sourceManifest, "utf8"));
if (manifest.scheme !== "xyz" || manifest.crs !== "EPSG:3857" || manifest.format !== "jpeg") {
  throw new Error("离线高清瓦片库需要 Web Mercator XYZ JPEG 格式");
}

await mkdir(destination, { recursive: true });
const snapshotPath = join(destination, "tiles.db");
const sourceDb = new DatabaseSync(sourceDatabase, { readOnly: true });
try {
  await backup(sourceDb, snapshotPath);
} finally {
  sourceDb.close();
}

const snapshot = new DatabaseSync(snapshotPath);
try {
  snapshot.exec("PRAGMA journal_mode = DELETE");
  if (snapshot.prepare("PRAGMA quick_check").get().quick_check !== "ok") {
    throw new Error("离线瓦片库完整性检查失败");
  }
  const count = snapshot.prepare(`
    SELECT COUNT(*) AS totalTiles FROM (
      SELECT DISTINCT zoom_level, tile_column, tile_row FROM tiles
    )
  `).get().totalTiles;
  if (count === 0) throw new Error("离线瓦片库为空");
  manifest.totalTiles = count;
  await writeFile(join(destination, "manifest.json"), JSON.stringify(manifest, null, 2) + "\n");
  console.log(`已打包高清离线瓦片库：${count} 张瓦片`);
} finally {
  snapshot.close();
}
