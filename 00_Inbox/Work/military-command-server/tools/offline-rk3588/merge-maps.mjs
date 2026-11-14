import { backup, DatabaseSync } from "node:sqlite";
import { basename, dirname, join, relative, resolve, sep } from "node:path";
import { existsSync } from "node:fs";
import { mkdir, mkdtemp, readFile, rename, rm, stat, writeFile } from "node:fs/promises";

const args = process.argv.slice(2);
const allowActiveSnapshot = args.at(-1) === "--allow-active-snapshot";
if (allowActiveSnapshot) args.pop();
const [destinationArg, ...sourceArgs] = args;
if (!destinationArg || sourceArgs.length < 2) {
  throw new Error("用法：node merge-maps.mjs <输出目录> <地图源目录1> <地图源目录2> [--allow-active-snapshot]");
}

const destination = resolve(destinationArg);
const sources = sourceArgs.map(source => resolve(source));
const pathContains = (parent, child) => {
  const rel = relative(parent, child);
  return rel === "" || (!rel.startsWith(`..${sep}`) && rel !== ".." && !resolve(rel).startsWith(sep));
};

for (const source of sources) {
  if (!existsSync(join(source, "tiles.db")) || !existsSync(join(source, "manifest.json"))) {
    throw new Error(`地图源缺少 tiles.db 或 manifest.json：${source}`);
  }
  if (pathContains(source, destination) || pathContains(destination, source)) {
    throw new Error(`输出目录与地图源目录不能相互包含：${destination}`);
  }
}

const outputParent = dirname(destination);
await mkdir(outputParent, { recursive: true });
const workDir = await mkdtemp(join(outputParent, `.${basename(destination)}-work-`));
const snapshots = [];
const sourceData = [];
let merged;

function tileRangeForBounds(bounds, z) {
  const n = 2 ** z;
  const clampLatitude = latitude => Math.max(-85.0511287798066, Math.min(85.0511287798066, latitude));
  const xFor = longitude => Math.floor((longitude + 180) / 360 * n);
  const yFor = latitude => {
    const radians = clampLatitude(latitude) * Math.PI / 180;
    return Math.floor((1 - Math.asinh(Math.tan(radians)) / Math.PI) / 2 * n);
  };
  return {
    x0: Math.max(0, xFor(bounds[0])),
    x1: Math.min(n - 1, xFor(bounds[2])),
    y0: Math.max(0, yFor(bounds[3])),
    y1: Math.min(n - 1, yFor(bounds[1]))
  };
}

function layerArea(layer) {
  const [west, south, east, north] = layer.bounds;
  return (east - west) * (north - south);
}

function addTile(layer, z, x, y) {
  let rows = layer.tileRows.get(z);
  if (!rows) layer.tileRows.set(z, rows = new Map());
  let columns = rows.get(y);
  if (!columns) rows.set(y, columns = new Set());
  columns.add(x);
}

function intervals(values) {
  const sorted = [...values].sort((a, b) => a - b);
  const result = [];
  for (const value of sorted) {
    const previous = result.at(-1);
    if (previous && value <= previous[1] + 1) previous[1] = value;
    else result.push([value, value]);
  }
  return result;
}

function makeCoverage(tileRows) {
  const coverage = {};
  for (const [z, rows] of [...tileRows].sort((a, b) => a[0] - b[0])) {
    const rowRanges = {};
    const allX = [];
    const yValues = [...rows.keys()].sort((a, b) => a - b);
    for (const y of yValues) {
      const ranges = intervals(rows.get(y));
      rowRanges[y] = ranges;
      for (const [x0, x1] of ranges) allX.push(x0, x1);
    }
    coverage[z] = {
      x0: Math.min(...allX),
      x1: Math.max(...allX),
      y0: yValues[0],
      y1: yValues.at(-1),
      rows: rowRanges
    };
  }
  return coverage;
}

function bestLayer(candidates) {
  return candidates.sort((a, b) => layerArea(a) - layerArea(b) || a.order - b.order)[0];
}

try {
  for (let index = 0; index < sources.length; index++) {
    const source = sources[index];
    const sidecar = join(source, "tiles.db-wal");
    if (!allowActiveSnapshot && existsSync(sidecar) && (await stat(sidecar)).size > 0) {
      throw new Error(`地图源仍有未检查点写入日志，当前下载可能尚未完成：${source}`);
    }

    const manifest = JSON.parse(await readFile(join(source, "manifest.json"), "utf8"));
    if (manifest.scheme !== "xyz" || manifest.crs !== "EPSG:3857" || manifest.format !== "jpeg" ||
        !Array.isArray(manifest.layers) || manifest.layers.length === 0) {
      throw new Error(`地图源不是有效的 Web Mercator XYZ JPEG 图集：${source}`);
    }

    const sourceDb = new DatabaseSync(join(source, "tiles.db"), { readOnly: true });
    const snapshotPath = join(workDir, `source-${index}.sqlite`);
    try {
      const table = sourceDb.prepare("PRAGMA table_info(tiles)").all().map(column => column.name);
      if (!["zoom_level", "tile_column", "tile_row", "tile_data"].every(column => table.includes(column))) {
        throw new Error(`瓦片表结构不符合 XYZ 格式：${source}`);
      }
      await backup(sourceDb, snapshotPath);
    } finally {
      sourceDb.close();
    }

    const snapshotDb = new DatabaseSync(snapshotPath, { readOnly: true });
    if (snapshotDb.prepare("PRAGMA quick_check").get().quick_check !== "ok") {
      snapshotDb.close();
      throw new Error(`地图源快照完整性检查失败：${source}`);
    }
    snapshots.push(snapshotPath);
    sourceData.push({ source, manifest, database: snapshotDb });
  }

  const mergedPath = join(workDir, "tiles.db");
  merged = new DatabaseSync(mergedPath);
  merged.exec(`
    PRAGMA journal_mode = DELETE;
    CREATE TABLE tiles (
      zoom_level TEXT NOT NULL,
      tile_column INTEGER NOT NULL,
      tile_row INTEGER NOT NULL,
      tile_data BLOB NOT NULL,
      UNIQUE (zoom_level, tile_column, tile_row)
    );
  `);
  const putTile = merged.prepare(`
    INSERT INTO tiles (zoom_level, tile_column, tile_row, tile_data)
    VALUES (?, ?, ?, ?)
    ON CONFLICT (zoom_level, tile_column, tile_row)
    DO UPDATE SET tile_data = excluded.tile_data
  `);

  for (const { database } of sourceData) {
    merged.exec("BEGIN IMMEDIATE");
    try {
      for (const tile of database.prepare(`
        SELECT zoom_level, tile_column, tile_row, tile_data FROM tiles ORDER BY rowid
      `).iterate()) {
        const z = Number(tile.zoom_level);
        const x = Number(tile.tile_column);
        const y = Number(tile.tile_row);
        if (!Number.isInteger(z) || z < 0 || z > 22 || !Number.isInteger(x) || !Number.isInteger(y) ||
            x < 0 || y < 0 || x >= 2 ** z || y >= 2 ** z || !tile.tile_data) {
          throw new Error("地图源包含无效的 XYZ 瓦片记录");
        }
        putTile.run(String(z), x, y, tile.tile_data);
      }
      merged.exec("COMMIT");
    } catch (error) {
      merged.exec("ROLLBACK");
      throw error;
    }
  }

  const layers = [];
  const sourceSummaries = [];
  for (const { source, manifest, database } of sourceData) {
    const sourceId = typeof manifest.id === "string" ? manifest.id : basename(source);
    const layerModels = manifest.layers.map((layer, order) => {
      if (!Array.isArray(layer.bounds) || layer.bounds.length !== 4 ||
          !Number.isFinite(Number(layer.minimumLevel)) || !Number.isFinite(Number(layer.maximumLevel))) {
        throw new Error(`图层范围或层级无效：${sourceId}/${layer.id}`);
      }
      const id = layers.some(existing => existing.id === layer.id) ? `${sourceId}_${layer.id}` : layer.id;
      return {
        ...layer,
        id,
        sourceMap: sourceId,
        minimumLevel: Number(layer.minimumLevel),
        maximumLevel: Number(layer.maximumLevel),
        order,
        tileRows: new Map(),
        tileCount: 0
      };
    });

    for (const tile of database.prepare(`
      SELECT DISTINCT CAST(zoom_level AS INTEGER) AS z, tile_column AS x, tile_row AS y FROM tiles
    `).iterate()) {
      const z = Number(tile.z);
      const x = Number(tile.x);
      const y = Number(tile.y);
      const inBounds = layer => {
        const range = tileRangeForBounds(layer.bounds, z);
        return x >= range.x0 && x <= range.x1 && y >= range.y0 && y <= range.y1;
      };
      const spatial = layerModels.filter(inBounds);
      const declared = spatial.filter(layer => z >= layer.minimumLevel && z <= layer.maximumLevel);
      const layer = bestLayer(declared.length ? declared : spatial);
      if (!layer) throw new Error(`瓦片 ${z}/${x}/${y} 不在源地图任何图层范围内：${sourceId}`);
      addTile(layer, z, x, y);
      layer.tileCount++;
      layer.minimumLevel = Math.min(layer.minimumLevel, z);
      layer.maximumLevel = Math.max(layer.maximumLevel, z);
    }

    for (const layer of layerModels) {
      layer.coverage = makeCoverage(layer.tileRows);
      delete layer.tileRows;
      delete layer.order;
      layers.push(layer);
    }

    const actual = database.prepare(`
      SELECT COUNT(*) AS rows, COUNT(DISTINCT CAST(zoom_level AS INTEGER) || ':' || tile_column || ':' || tile_row) AS tiles
      FROM tiles
    `).get();
    sourceSummaries.push({
      id: sourceId,
      directory: basename(source),
      manifestTiles: manifest.totalTiles ?? null,
      snapshotTiles: Number(actual.tiles),
      snapshotRows: Number(actual.rows),
      bounds: manifest.bounds
    });
  }

  layers.sort((a, b) => a.minimumLevel - b.minimumLevel || layerArea(b) - layerArea(a));
  const mapTileRows = new Map();
  for (const tile of merged.prepare(`
    SELECT DISTINCT CAST(zoom_level AS INTEGER) AS z, tile_column AS x, tile_row AS y FROM tiles
  `).iterate()) {
    addTile({ tileRows: mapTileRows }, Number(tile.z), Number(tile.x), Number(tile.y));
  }
  const counts = merged.prepare(`
    SELECT COUNT(*) AS rows,
      COUNT(DISTINCT CAST(zoom_level AS INTEGER) || ':' || tile_column || ':' || tile_row) AS tiles,
      MIN(CAST(zoom_level AS INTEGER)) AS minimumLevel,
      MAX(CAST(zoom_level AS INTEGER)) AS maximumLevel
    FROM tiles
  `).get();
  if (!counts.tiles || merged.prepare("PRAGMA quick_check").get().quick_check !== "ok") {
    throw new Error("合并后的瓦片库为空或完整性检查失败");
  }

  const bounds = layers.reduce((result, layer) => {
    result[0] = Math.min(result[0], layer.bounds[0]);
    result[1] = Math.min(result[1], layer.bounds[1]);
    result[2] = Math.max(result[2], layer.bounds[2]);
    result[3] = Math.max(result[3], layer.bounds[3]);
    return result;
  }, [Infinity, Infinity, -Infinity, -Infinity]);
  const firstManifest = sourceData[0].manifest;
  const manifest = {
    scheme: "xyz",
    crs: "EPSG:3857",
    tileSize: firstManifest.tileSize ?? 256,
    format: "jpeg",
    storage: "sqlite",
    database: "tiles.db",
    id: "combined-theaters",
    name: "多战区高精度离线图集",
    createdAt: new Date().toISOString(),
    totalTiles: Number(counts.tiles),
    minimumLevel: Number(counts.minimumLevel),
    maximumLevel: Number(counts.maximumLevel),
    bounds,
    coverage: makeCoverage(mapTileRows),
    layers: layers.map(({ tileCount, ...layer }) => ({ ...layer, tileCount })),
    sourceMaps: sourceSummaries,
    snapshotState: allowActiveSnapshot ? "provisional" : "built"
  };

  merged.exec("PRAGMA optimize;");
  merged.close();
  merged = null;
  for (const item of sourceData) {
    item.database.close();
    item.database = null;
  }
  for (const snapshotPath of snapshots) {
    await rm(snapshotPath, { force: true });
    await rm(`${snapshotPath}-wal`, { force: true });
    await rm(`${snapshotPath}-shm`, { force: true });
  }
  await writeFile(join(workDir, "manifest.json"), JSON.stringify(manifest, null, 2) + "\n");

  const destinationExists = existsSync(destination);
  const previousPath = destinationExists ? `${destination}.previous-${Date.now()}` : null;
  if (destinationExists) await rename(destination, previousPath);
  try {
    await rename(workDir, destination);
  } catch (error) {
    if (previousPath) await rename(previousPath, destination).catch(() => {});
    throw error;
  }
  if (previousPath) await rm(previousPath, { recursive: true, force: true });
  console.log(`合成图集已生成：${destination}`);
  console.log(`实际唯一瓦片：${manifest.totalTiles}；层级 ${manifest.minimumLevel}–${manifest.maximumLevel}；状态 ${manifest.snapshotState}`);
  for (const source of sourceSummaries) {
    console.log(`${source.id}：清单 ${source.manifestTiles ?? "未记录"}，快照 ${source.snapshotTiles} 张`);
  }
} finally {
  merged?.close();
  for (const item of sourceData) item.database?.close();
  await rm(workDir, { recursive: true, force: true });
}
