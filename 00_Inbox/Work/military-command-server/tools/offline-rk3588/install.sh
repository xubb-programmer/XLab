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

name=military-command-server
prefix=/opt/jw/$name
conf=/etc/$name
state=/var/lib/$name
tmp=$(mktemp -d /var/tmp/military-command-server-apt.XXXXXX)
staging=
services=(server-edge.service military-command-server.service)
broker_file=/etc/mosquitto/conf.d/military-command-server-local.conf
old_broker_file=/etc/mosquitto/conf.d/target-guidance-local.conf
source "$bundle/migrate-installation.sh"
trap finish_install EXIT
switch_release() {
  ln -sfn "$1" "$prefix/.current.new" &&
    mv -Tf "$prefix/.current.new" "$prefix/current"
}
exec 9>/run/lock/military-command-server-install.lock
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
  if [[ ! -e "$broker_file" && ! -e "$old_broker_file" ]]; then
    printf 'listener 1883 127.0.0.1\nallow_anonymous true\n' > "$broker_file"
  fi
fi
apt-get "${apt_local[@]}" -y --no-install-recommends --no-remove install "${runtime[@]}"

install -d -m 0755 "$prefix/releases"

release_id=$(sha256sum "$bundle/SHA256SUMS" | cut -c1-16)
release="$prefix/releases/$release_id"
if [[ ! -d "$release" ]]; then
  staging=$(mktemp -d "$prefix/releases/.stage.XXXXXX")
  cp -a "$bundle"/{bin,node,web} "$staging/"
  install -m 0755 "$bundle/launch-military-command.sh" "$staging/launch-military-command.sh"
  install -m 0755 "$bundle/open-desktop-tool.sh" "$staging/open-desktop-tool.sh"
  install -m 0644 "$bundle/military-command-tools.desktop" "$staging/military-command-tools.desktop"
  ln -s "$state/scenarios" "$staging/web/public/scenarios"
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
for config in targets video reporting edgeagent; do
  seed_config "$bundle/config/$config.json" "$conf/$config.json"
done
if [[ ! -e "$conf/web.env" ]]; then
  source_ips=$("$bundle/node/bin/node" -e '
    const config = JSON.parse(require("node:fs").readFileSync(process.argv[1], "utf8"));
    const channel = config.channels.find(channel => channel.channel_id === "device-status-udp");
    if (!channel?.allowed_peers?.length) process.exit(1);
    process.stdout.write(channel.allowed_peers.map(peer => peer.slice(0, peer.lastIndexOf(":"))).join(","));
  ' "$bundle/config/edgeagent.json")
  printf 'MILITARY_COMMAND_SERVER_BIND_HOST=192.168.0.201\nPORT=4177\nDEVICE_SIM_SOURCE_IPS=%s\n' "$source_ips" > "$tmp/web.env"
  seed_config "$tmp/web.env" "$conf/web.env"
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
  "$edge_config" "$scenario" "$conf/reporting.json" "$tmp/edgeagent-migrated.json" "$bundle/config/edgeagent.json"
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
  set -a
  . "$conf/web.env"
  set +a
  host=${MILITARY_COMMAND_SERVER_BIND_HOST:-127.0.0.1}
  [[ "$host" != 0.0.0.0 ]] || host=127.0.0.1
  if [[ "$host" == *:* ]]; then host="[$host]"; fi
  "$prefix/current/node/bin/node" --input-type=module - "http://$host:${PORT:-4177}" <<'JS'
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
  die '启动检查未通过，请查看 journalctl -u military-command-server -u server-edge -u mosquitto。'
fi
install_complete=1
update_release_links

printf '离线部署完成。网页端口和监听地址见 %s/web.env。\n' "$conf"
