#!/usr/bin/env bash
set -euo pipefail

die() { printf '打包失败：%s\n' "$*" >&2; exit 1; }
[[ $EUID -eq 0 ]] || die '请在独立构建环境中以 root 运行。'
. /etc/os-release

here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
web=$(cd -- "$here/../.." && pwd)
edge=${EDGEAGENT_SOURCE:-"$web/../../../01_Projects/Work/EdgeAgent/EdgeAgent"}
[[ -f "$edge/EdgeAgent.pro" && -d "$edge/src" ]] || die '没有找到 EdgeAgent 源码。'
case "$ID" in
  ubuntu|debian) ;;
  *) die "暂只支持 Debian/Ubuntu 系 DEB 构建环境，当前为 ${PRETTY_NAME:-未知系统}。" ;;
esac
architecture=$(dpkg --print-architecture 2>/dev/null || true)
[[ "$architecture" == arm64 ]] || die "构建环境需为 ARM64，当前架构为 ${architecture:-未知}。"
node_version=${NODE_VERSION:-v24.22.2}
[[ $node_version =~ ^v24\.[0-9]+\.[0-9]+$ ]] || die 'NODE_VERSION 应为明确的 Node.js 24 发行版本。'
output=${1:-"$PWD/dist"}
mkdir -p -- "$output"
output=$(cd -- "$output" && pwd)

work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
chmod 0755 "$work"
bundle="$work/intelligent-cooperative-server-offline"
mkdir -p "$bundle"/{bin,config,debs,services,node,web} "$work"/{apt/partial,edge/build}
if compgen -G '/var/cache/apt/archives/*.deb' >/dev/null; then
  while IFS= read -r archive; do
    name=$(basename -- "$archive")
    decoded=${name//%2b/+}
    decoded=${decoded//%2B/+}
    decoded=${decoded//%7e/~}
    decoded=${decoded//%7E/~}
    cp -a -- "$archive" "$work/apt/$decoded"
  done < <(find /var/cache/apt/archives -maxdepth 1 -type f -name '*.deb' -print)
fi

mapfile -t runtime < "$here/runtime-packages.txt"
export DEBIAN_FRONTEND=noninteractive
apt_online=(-o Acquire::Retries=4 -o Acquire::Languages=none -o "Dir::Cache::archives=$work/apt")
if [[ ${OFFLINE_BUILD:-0} != 1 ]]; then
  apt-get "${apt_online[@]}" update
fi
: > "$work/empty-status"
# Resolve the runtime closure independently of the builder's installed packages.
apt-get "${apt_online[@]}" -y --download-only --no-install-recommends \
  -o Dir::State::status="$work/empty-status" \
  install "${runtime[@]}"
cp -l "$work/apt/"*.deb "$bundle/debs/"
apt-get "${apt_online[@]}" -y --no-install-recommends install "${runtime[@]}" ca-certificates xz-utils \
  build-essential dpkg-dev qt5-qmake qtbase5-dev libqt5serialport5-dev libqt5serialbus5-dev \
  libmosquitto-dev libspdlog-dev libfmt-dev nlohmann-json3-dev
(cd "$bundle/debs" && dpkg-scanpackages . /dev/null | gzip -n > Packages.gz)

cp -a "$edge/EdgeAgent.pro" "$edge/src" "$work/edge/"
(
  cd "$work/edge/build"
  qmake ../EdgeAgent.pro
  make -j "${BUILD_JOBS:-2}"
)
install -m 0755 "$work/edge/build/edgeagent" "$bundle/bin/edgeagent"
ldd "$bundle/bin/edgeagent" > "$work/edge-ldd"
if grep -q 'not found' "$work/edge-ldd"; then cat "$work/edge-ldd" >&2; die 'EdgeAgent 缺少运行库。'; fi

node_tar="node-$node_version-linux-arm64.tar.xz"
release_url="https://nodejs.org/dist/$node_version"
node_cache=${NODE_CACHE_DIR:-/var/cache/intelligent-cooperative-server}
if [[ -f "$node_cache/SHASUMS256.txt" && -f "$node_cache/$node_tar" ]]; then
  cp -a "$node_cache/SHASUMS256.txt" "$work/node-checksums"
  cp -a "$node_cache/$node_tar" "$work/$node_tar"
else
  curl --fail --location --retry 2 "$release_url/SHASUMS256.txt" -o "$work/node-checksums"
  curl --fail --location --retry 2 "$release_url/$node_tar" -o "$work/$node_tar"
fi
(
  cd "$work"
  awk -v file="$node_tar" '$2 == file { print; found=1 } END { if (!found) exit 1 }' node-checksums | sha256sum -c -
)
tar -xJf "$work/$node_tar" -C "$bundle/node" --strip-components=1
export PATH="$bundle/node/bin:$PATH"
[[ $(node -p 'process.arch') == arm64 ]] || die 'Node.js 架构检查失败。'

cp -a "$web"/{package.json,package-lock.json,server,src,public,config} "$bundle/web/"
map_snapshot_args=()
if [[ "${ALLOW_INCOMPLETE_OFFLINE_MAP:-0}" == "1" ]]; then
  map_snapshot_args+=(--allow-active-snapshot)
fi
node "$here/merge-maps.mjs" "$bundle/web/dist/offline-maps/combined-theaters" \
  "$web/dist/offline-maps/highres-satellite" "$web/dist/offline-maps/zhurihe-chifeng" "${map_snapshot_args[@]}"
mv "$bundle/web/public/scenarios" "$bundle/config/scenarios"
cp "$web/config/app.json" "$bundle/config/app.json"
if [[ -f "$web/deployment-terms.json" ]]; then
  cp "$web/deployment-terms.json" "$bundle/config/terms.json"
else
  cp "$web/config/terms.json" "$bundle/config/terms.json"
fi
node "$here/expand-lan-peers.mjs" "$here/edgeagent.json" > "$bundle/config/edgeagent.json"
cp -a "$web/config/mosquitto" "$bundle/config/" 2>/dev/null || true
node --input-type=module - "$bundle/config" <<'JS'
import { readFileSync, writeFileSync } from "node:fs";
import { join } from "node:path";
const dir = process.argv[2];
const read = file => JSON.parse(readFileSync(join(dir, file), "utf8"));
const write = (file, data) => writeFileSync(join(dir, file), JSON.stringify(data, null, 2) + "\n");
const app = read("app.json");
write("app.json", {
  ...app,
  server: {
    ...app.server,
    host: "192.168.0.201",
    port: 4177
  },
  video: {
    ...app.video,
    rtspTimeoutOption: "stimeout"
  }
});
const file = "scenarios/scenario-default.json";
const source = read(file);
write(file, { ...source, mapMode: "offline" });
JS
(
  cd "$bundle/web"
  if [[ -n "${NODE_MODULES_SOURCE:-}" ]]; then
    [[ -d "$NODE_MODULES_SOURCE" ]] || die "NODE_MODULES_SOURCE 不存在：$NODE_MODULES_SOURCE"
    cp -a "$NODE_MODULES_SOURCE" ./node_modules
  else
    npm ci --omit=dev --no-audit --no-fund
  fi
  npm run check
)

cp "$here"/{README.md,install.sh,migrate-installation.sh,launch-intelligent-cooperative.sh,open-desktop-tool.sh,intelligent-cooperative-tools.desktop,runtime-packages.txt,migrate-edge-reporting.mjs} "$bundle/"
cp "$web/README.md" "$bundle/PROJECT-README.md"
cp "$here"/*.service "$bundle/services/"
printf 'os_id=%s\nos_version_id=%s\nos_version_codename=%s\narchitecture=%s\npackage_format=deb\nnode=%s\n' \
  "$ID" "$VERSION_ID" "${VERSION_CODENAME:-}" "$architecture" "$node_version" > "$bundle/BUILD.txt"
(
  cd "$bundle"
  find . -type f ! -path ./SHA256SUMS -print0 | sort -z | xargs -0 sha256sum > SHA256SUMS
)
archive="$output/intelligent-cooperative-server-${ID}${VERSION_ID}-${architecture}.tar.gz"
tar -C "$work" -czf "$work/bundle.tar.gz" intelligent-cooperative-server-offline
mv "$work/bundle.tar.gz" "$archive"
(cd "$output" && sha256sum "$(basename "$archive")" > "$(basename "$archive").sha256")
printf '离线包已生成：%s\n' "$archive"
