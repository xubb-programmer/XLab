#!/usr/bin/env bash
set -euo pipefail

die() { printf '安装失败：%s\n' "$*" >&2; exit 1; }
[[ $# -eq 0 ]] || die '用法为 sudo bash install.sh。'
. /etc/os-release
[[ $EUID -eq 0 ]] || die '请使用 sudo bash install.sh。'
[[ -d /run/systemd/system ]] || die '请在已启动 systemd 的目标系统中运行。'

bundle=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
[[ -f "$bundle/BUILD.txt" && -s "$bundle/debs/Packages.gz" && -f "$bundle/SHA256SUMS" ]] ||
  die '请先完整解包，再运行包内的 install.sh。'
(cd "$bundle" && sha256sum --quiet -c SHA256SUMS) || die '安装包校验失败。'
declare -A build
while IFS='=' read -r key value; do
  [[ "$key" =~ ^[a-z_]+$ ]] || continue
  build["$key"]=$value
done < "$bundle/BUILD.txt"
[[ ${build[os_id]:-} == "$ID" && ${build[os_version_id]:-} == "$VERSION_ID" ]] ||
  die "此系统包要求 ${build[os_id]:-未知系统} ${build[os_version_id]:-未知版本}，当前为 ${PRETTY_NAME:-未知系统}。"
[[ ${build[package_format]:-} == deb ]] || die '此离线包不是 DEB 系安装包。'
architecture=$(dpkg --print-architecture 2>/dev/null || true)
[[ ${build[architecture]:-} == "$architecture" && "$architecture" == arm64 ]] ||
  die "此系统包要求 ${build[architecture]:-未知} ARM64，当前架构为 ${architecture:-未知}。"
[[ $("$bundle/node/bin/node" -p 'process.arch') == arm64 ]] || die '包内 Node.js 无法在本机运行。'

name=intelligent-cooperative-server
prefix=/opt/jw/$name
conf=/etc/$name
state=/var/lib/$name
tmp=$(mktemp -d /var/tmp/intelligent-cooperative-server-apt.XXXXXX)
staging=
services=(server-edge.service intelligent-cooperative-server.service)
broker_file=/etc/mosquitto/conf.d/intelligent-cooperative-server-local.conf
old_broker_file=/etc/mosquitto/conf.d/intelligent-command-server-local.conf
older_broker_file=/etc/mosquitto/conf.d/dispatch-server-local.conf
ancient_broker_file=/etc/mosquitto/conf.d/command-server-local.conf
source "$bundle/migrate-installation.sh"
trap finish_install EXIT
switch_release() {
  ln -sfn "$1" "$prefix/.current.new" &&
    mv -Tf "$prefix/.current.new" "$prefix/current"
}
exec 9>/run/lock/intelligent-cooperative-server-install.lock
flock -n 9 || die '已有安装任务在运行。'
check_migration
chmod 0755 "$tmp"
cp -a "$bundle/debs" "$tmp/debs"
chmod -R a+rX "$tmp/debs"
# 板卡镜像可能锁定了定制库，版本号与包内发行版库相同也不能覆盖。
held_packages=$(apt-mark showhold)
if [[ -n "$held_packages" ]]; then
  gzip -dc "$tmp/debs/Packages.gz" |
    awk -v held_packages="$held_packages" '
      BEGIN {
        RS = ""; ORS = "\n\n"
        count = split(held_packages, names, "\n")
        for (i = 1; i <= count; i++) {
          sub(/:[^:]+$/, "", names[i])
          held[names[i]] = 1
        }
      }
      {
        if (match($0, /(^|\n)Package: [^\n]+/)) {
          name = substr($0, RSTART, RLENGTH)
          sub(/^\n?Package: /, "", name)
          if (name in held) next
        }
        print
      }
    ' | gzip -n > "$tmp/Packages.gz"
  mv -T -- "$tmp/Packages.gz" "$tmp/debs/Packages.gz"
fi
mkdir -p "$tmp"/{lists/partial,archives/partial}
printf 'deb [trusted=yes] file:%s/debs ./\n' "$tmp" > "$tmp/sources.list"
apt_local=(
  -o "Dir::Etc::sourcelist=$tmp/sources.list" -o "Dir::Etc::sourceparts=-"
  -o "Dir::State::lists=$tmp/lists" -o "Dir::Cache::archives=$tmp/archives"
  -o Acquire::Languages=none
)
mapfile -t runtime < "$bundle/runtime-packages.txt"
export DEBIAN_FRONTEND=noninteractive
apt-get "${apt_local[@]}" update
apt-get "${apt_local[@]}" -s --no-install-recommends --no-remove install "${runtime[@]}" >/dev/null

# A fresh Mosquitto install must listen only on loopback, including first start.
if [[ $(dpkg-query -W -f='${Status}' mosquitto 2>/dev/null || true) != 'install ok installed' ]]; then
  install -d -m 0755 /etc/mosquitto/conf.d
  if [[ ! -e "$broker_file" && ! -e "$old_broker_file" && ! -e "$older_broker_file" ]]; then
    if [[ -f "$bundle/config/mosquitto/intelligent-cooperative-server-local.conf" ]]; then
      install -m 0644 "$bundle/config/mosquitto/intelligent-cooperative-server-local.conf" "$broker_file"
    elif [[ -f "$bundle/config/mosquitto/intelligent-command-server-local.conf" ]]; then
      install -m 0644 "$bundle/config/mosquitto/intelligent-command-server-local.conf" "$broker_file"
    elif [[ -f "$bundle/config/mosquitto/dispatch-server-local.conf" ]]; then
      install -m 0644 "$bundle/config/mosquitto/dispatch-server-local.conf" "$broker_file"
    else
      printf 'listener 1883 127.0.0.1\nallow_anonymous true\n' > "$broker_file"
    fi
  fi
fi
apt-get "${apt_local[@]}" -y --no-install-recommends --no-remove install "${runtime[@]}"

install -d -m 0755 "$prefix/releases"

build_date=$(date +%Y%m%d)
release_hash=$(sha256sum "$bundle/SHA256SUMS" | cut -c1-8)
release_id="${build_date}-${release_hash}"
release="$prefix/releases/$release_id"
if [[ ! -d "$release" ]]; then
  staging=$(mktemp -d "$prefix/releases/.stage.XXXXXX")
  cp -a "$bundle"/{bin,node,web} "$staging/"
  install -m 0755 "$bundle/launch-intelligent-cooperative.sh" "$staging/launch-intelligent-cooperative.sh"
  install -m 0755 "$bundle/open-desktop-tool.sh" "$staging/open-desktop-tool.sh"
  install -m 0644 "$bundle/intelligent-cooperative-tools.desktop" "$staging/intelligent-cooperative-tools.desktop"
  ln -s "$state/scenarios" "$staging/web/public/scenarios"
  if [[ -d "$state/offline-maps" ]]; then
    mkdir -p "$staging/web/dist"
    ln -sfn "$state/offline-maps" "$staging/web/dist/offline-maps"
  fi
  chown -R root:root "$staging"
  chmod -R go-w "$staging"
  chmod 0755 "$staging"
  mv -T "$staging" "$release"
  staging=
fi
ldd "$release/bin/edgeagent" > "$tmp/edge-ldd"
if grep -q 'not found' "$tmp/edge-ldd"; then cat "$tmp/edge-ldd" >&2; die 'EdgeAgent 运行库不齐。'; fi

begin_install
migrate_installation
if ! id "$name" &>/dev/null; then
  useradd --system --user-group --home-dir "$state" --shell /usr/sbin/nologin "$name"
fi
install -d -m 0750 -o root -g "$name" "$conf"
install -d -m 0750 -o "$name" -g "$name" "$state" "$state/scenarios" "$state/edge"
seed_config() {
  if [[ ! -e "$2" ]]; then install -m 0640 -o root -g "$name" "$1" "$2"; fi
}
for config in app edgeagent terms; do
  if [[ -f "$bundle/config/$config.json" ]]; then
    seed_config "$bundle/config/$config.json" "$conf/$config.json"
  fi
done
if [[ -f "$conf/app.json" ]]; then
  source_ips=$("$bundle/node/bin/node" -e '
    const config = JSON.parse(require("node:fs").readFileSync(process.argv[1], "utf8"));
    const channel = config.channels.find(channel => channel.channel_id === "device-status-udp");
    if (!channel?.allowed_peers?.length) process.exit(1);
    process.stdout.write(channel.allowed_peers.map(peer => peer.slice(0, peer.lastIndexOf(":"))).join(","));
  ' "$bundle/config/edgeagent.json" || true)
  if [[ -n "$source_ips" ]]; then
    "$bundle/node/bin/node" -e '
      const fs = require("node:fs");
      const path = process.argv[1];
      const app = JSON.parse(fs.readFileSync(path, "utf8"));
      app.server = app.server || {};
      if (!app.server.deviceSimSourceIps) {
        app.server.deviceSimSourceIps = process.argv[2];
        fs.writeFileSync(path, JSON.stringify(app, null, 2) + "\n");
      }
    ' "$conf/app.json" "$source_ips"
  fi
fi
scenario="$state/scenarios/scenario-default.json"
if [[ ! -e "$scenario" ]]; then
  install -m 0640 -o "$name" -g "$name" "$bundle/config/scenarios/scenario-default.json" "$scenario"
fi
for service in "${services[@]}"; do
  install -m 0644 "$bundle/services/$service" "/etc/systemd/system/$service"
done

edge_config="$conf/edgeagent.json"
"$bundle/node/bin/node" "$bundle/migrate-edge-reporting.mjs" \
  "$edge_config" "$scenario" "$conf/app.json" "$tmp/edgeagent-migrated.json" "$bundle/config/edgeagent.json"
replace_edge_config() {
  local next
  next=$(mktemp "$conf/.edgeagent.XXXXXX")
  install -m 0640 -o root -g "$name" "$1" "$next"
  mv -Tf "$next" "$edge_config"
}
if [[ -f "$tmp/edgeagent-migrated.json" ]]; then
  cp -p "$edge_config" "$tmp/edgeagent-previous.json"
  replace_edge_config "$tmp/edgeagent-migrated.json"
fi

switch_release "$release"
systemctl daemon-reload
systemctl enable --now mosquitto.service
systemctl enable "${services[@]}"
service_ready() (
  url=$("$prefix/current/node/bin/node" -e '
    const conf = JSON.parse(require("node:fs").readFileSync(process.argv[1], "utf8"));
    const host = process.env.INTELLIGENT_COOPERATIVE_SERVER_BIND_HOST || conf.server?.host || "127.0.0.1";
    const port = process.env.PORT || conf.server?.port || 4177;
    const urlHost = host.includes(":") ? `[${host}]` : host;
    process.stdout.write(`http://${urlHost}:${port}`);
  ' "$conf/app.json")
  "$prefix/current/node/bin/node" --input-type=module - "$url" <<'JS'
const url = process.argv[2];
for (let i = 0; i < 15; i++) {
  try {
    const response = await fetch(url + "/api/targets", { signal: AbortSignal.timeout(1000) });
    if (response.ok && (await response.json()).state === "subscribed") process.exit(0);
  } catch {}
  await new Promise(resolve => setTimeout(resolve, 1000));
}
process.exit(1);
JS
)
if ! systemctl restart "${services[@]}" || ! service_ready || ! systemctl is-active --quiet server-edge.service; then
  die '启动检查未通过，请查看 journalctl -u intelligent-cooperative-server -u server-edge -u mosquitto。'
fi
install_complete=1
update_release_links

prune_old_releases() {
  local current_target count=0 keep=1 dir clean_dir
  current_target=$(readlink -f "$prefix/current" 2>/dev/null || true)
  rm -rf "$prefix/releases"/.stage.* 2>/dev/null || true
  while IFS= read -r dir; do
    [[ -n "$dir" ]] || continue
    clean_dir="${dir%/}"
    [[ -d "$clean_dir" ]] || continue
    if [[ "$clean_dir" == "$current_target" ]]; then continue; fi
    ((count++))
    if ((count > keep)); then
      rm -rf -- "$clean_dir"
    fi
  done < <(ls -1td "$prefix/releases"/*/ 2>/dev/null || true)
}
prune_old_releases

printf '离线部署完成。平台配置见 %s/app.json。\n' "$conf"
