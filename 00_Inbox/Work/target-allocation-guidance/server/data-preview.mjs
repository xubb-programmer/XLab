import { createServer } from "node:http";
import { readFile } from "node:fs/promises";
import { resolve, extname, sep } from "node:path";
import { fileURLToPath } from "node:url";
import { openOperationalStore } from "./operational-store.mjs";
import { openPreviewRegistry } from "./preview-device-registry.mjs";
import { serveDataManagement } from "./data-management.mjs";

const root = fileURLToPath(new URL("../", import.meta.url));
const source = resolve(root, "src");
const dbPath = resolve(root, "dist/data-preview/records.sqlite");
const operations = openOperationalStore(dbPath);
const registry = openPreviewRegistry(dbPath);
const types = {
  ".html": "text/html; charset=utf-8",
  ".js": "text/javascript; charset=utf-8",
  ".css": "text/css; charset=utf-8"
};

const server = createServer(async (req, res) => {
  try {
    const url = new URL(req.url, "http://127.0.0.1:4196");
    const pathname = decodeURIComponent(url.pathname);
    if (pathname.startsWith("/api/management/")) {
      await serveDataManagement(req, res, url, pathname, operations, registry);
      return;
    }
    const file = resolve(source, pathname === "/" ? "data-preview.html" : "." + pathname);
    const extension = extname(file);
    if (req.method !== "GET" || !file.startsWith(source + sep) || !Object.hasOwn(types, extension)) {
      res.writeHead(404).end();
      return;
    }
    const content = await readFile(file);
    res.writeHead(200, {
      "Content-Type": types[extension],
      "Cache-Control": "no-store",
      "X-Content-Type-Options": "nosniff"
    }).end(content);
  } catch (error) {
    res.writeHead(error.code === "ENOENT" ? 404 : 500).end();
  }
});

server.on("close", () => {
  registry.close();
  operations.close();
});
server.listen(4196, "127.0.0.1", () => console.log("http://127.0.0.1:4196/"));
