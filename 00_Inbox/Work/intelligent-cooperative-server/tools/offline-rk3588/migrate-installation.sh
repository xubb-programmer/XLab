#!/usr/bin/env bash
# Sourced by install.sh after the bundle is verified and before service cutover.

old_name=dispatch-server
for candidate in intelligent-command-server dispatch-server command-server target-guidance; do
  if [[ -e "/etc/$candidate" || -e "/var/lib/$candidate" ]] || getent passwd "$candidate" >/dev/null 2>&1; then
    old_name=$candidate
    break
  fi
done
old_conf=/etc/$old_name
old_state=/var/lib/$old_name
old_cache=/var/cache/$old_name
new_cache=/var/cache/$name
install_started=0
install_complete=0
previous=
previous_services=()
newly_enabled_services=()
moved_conf=0
moved_state=0
moved_cache=0
moved_broker=0
renamed_user=0
renamed_group=0

path_exists() { [[ -e "$1" || -L "$1" ]]; }

check_migration() {
  local pair old new
  for pair in "$old_conf:$conf" "$old_state:$state" "$old_cache:$new_cache" "$old_broker_file:$broker_file"; do
    old=${pair%%:*}
    new=${pair#*:}
    if path_exists "$old" && path_exists "$new"; then
      die "旧路径 $old 与新路径 $new 同时存在，请先核对内容。"
    fi
  done
  if getent passwd "$old_name" >/dev/null; then
    getent passwd "$name" >/dev/null && die "新旧服务账户同时存在，请先核对数据归属。"
    [[ $(id -gn "$old_name") == "$old_name" ]] || die "旧服务账户使用了自定义主组。"
    getent group "$name" >/dev/null && die "新服务组已存在，请先核对数据归属。"
  elif getent group "$old_name" >/dev/null; then
    die "旧服务账户缺失，请先核对旧服务组和数据归属。"
  elif path_exists "$old_state" || path_exists "$old_conf"; then
    die "旧服务账户缺失，请先核对旧配置和数据归属。"
  fi
  for old_svc in intelligent-command-server dispatch-server command-server target-guidance; do
    if [[ "$old_svc" != "$name" ]]; then
      if systemctl is-active --quiet "$old_svc.service"; then
        systemctl stop "$old_svc.service" 2>/dev/null || true
      fi
      if systemctl is-enabled --quiet "$old_svc.service"; then
        systemctl disable "$old_svc.service" 2>/dev/null || true
      fi
      if [[ -f "/etc/systemd/system/$old_svc.service" ]]; then
        rm -f "/etc/systemd/system/$old_svc.service"
      fi
    fi
  done
  return 0
}

begin_install() {
  local service unit
  previous=$(readlink "$prefix/current" || true)
  for service in "${services[@]}"; do
    unit="/etc/systemd/system/$service"
    if path_exists "$unit"; then cp -a -- "$unit" "$tmp/$service"; fi
    if systemctl is-active --quiet "$service"; then previous_services+=("$service"); fi
    if ! systemctl is-enabled --quiet "$service"; then newly_enabled_services+=("$service"); fi
  done
  install_started=1
  if (("${#previous_services[@]}")); then systemctl stop "${previous_services[@]}"; fi
}

migrate_installation() {
  local file
  if getent passwd "$old_name" >/dev/null; then
    old_home=$(getent passwd "$old_name" | cut -d: -f6)
    groupmod -n "$name" "$old_name"
    renamed_group=1
    usermod -l "$name" -d "$state" "$old_name"
    renamed_user=1
  fi
  if path_exists "$old_conf"; then
    mv -T -- "$old_conf" "$conf"
    moved_conf=1
    for file in web.env edgeagent.json; do
      if [[ -f "$conf/$file" ]]; then cp -p -- "$conf/$file" "$tmp/original-$file"; fi
    done
    if [[ -f "$conf/web.env" ]]; then
      sed -i -E \
        -e "s|^([[:space:]]*)(TARGET_GUIDANCE_|COMMAND_SERVER_|DISPATCH_SERVER_|INTELLIGENT_COMMAND_SERVER_)|\1INTELLIGENT_COOPERATIVE_SERVER_|" \
        -e "/^[[:space:]]*(INTELLIGENT_COOPERATIVE_SERVER_(SCENARIO|VIDEO_CONFIG|TARGETS_CONFIG|REPORTING_CONFIG|REPORTING_DB|MANAGEMENT_DB)|DEVICE_SIM_STATE_DIR)=/s#/etc/$old_name/#$conf/#g" \
        -e "/^[[:space:]]*(INTELLIGENT_COOPERATIVE_SERVER_(SCENARIO|VIDEO_CONFIG|TARGETS_CONFIG|REPORTING_CONFIG|REPORTING_DB|MANAGEMENT_DB)|DEVICE_SIM_STATE_DIR)=/s#/var/lib/$old_name/#$state/#g" \
        "$conf/web.env"
    fi
  fi
  if path_exists "$old_state"; then
    mv -T -- "$old_state" "$state"
    moved_state=1
  fi
  if path_exists "$old_cache"; then
    mv -T -- "$old_cache" "$new_cache"
    moved_cache=1
  fi
  if path_exists "$old_broker_file"; then
    mv -T -- "$old_broker_file" "$broker_file"
    moved_broker=1
  fi
}

update_release_links() {
  local link
  for link in "$prefix/releases/"*/web/public/scenarios; do
    if [[ -L "$link" && $(readlink "$link") == "$old_state/scenarios" ]]; then
      ln -sfnT -- "$state/scenarios" "$link"
    fi
  done
}

rollback_install() {
  local service unit file failed=0
  for service in "${services[@]}"; do
    if systemctl is-active --quiet "$service"; then systemctl stop "$service" || failed=1; fi
  done
  if (("${#newly_enabled_services[@]}")); then
    systemctl disable "${newly_enabled_services[@]}" || failed=1
  fi
  if [[ -f "$tmp/edgeagent-previous.json" ]]; then
    cp -p -- "$tmp/edgeagent-previous.json" "$conf/edgeagent.json" || failed=1
  fi
  if ((moved_conf)); then
    for file in web.env edgeagent.json; do
      if [[ -f "$tmp/original-$file" ]]; then
        cp -p -- "$tmp/original-$file" "$conf/$file" || failed=1
      fi
    done
    mv -T -- "$conf" "$old_conf" || failed=1
  fi
  if ((moved_state)); then mv -T -- "$state" "$old_state" || failed=1; fi
  if ((moved_cache)); then mv -T -- "$new_cache" "$old_cache" || failed=1; fi
  if ((moved_broker)); then mv -T -- "$broker_file" "$old_broker_file" || failed=1; fi
  if ((renamed_user)); then usermod -l "$old_name" -d "$old_home" "$name" || failed=1; fi
  if ((renamed_group)); then groupmod -n "$old_name" "$name" || failed=1; fi
  for service in "${services[@]}"; do
    unit="/etc/systemd/system/$service"
    if path_exists "$tmp/$service"; then
      cp -a --remove-destination -- "$tmp/$service" "$unit" || failed=1
    else
      rm -f -- "$unit" || failed=1
    fi
  done
  if [[ -n "$previous" ]]; then
    switch_release "$previous" || failed=1
  elif [[ -L "$prefix/current" ]]; then
    rm -- "$prefix/current" || failed=1
  fi
  systemctl daemon-reload || failed=1
  if (("${#previous_services[@]}")); then systemctl start "${previous_services[@]}" || failed=1; fi
  return "$failed"
}

finish_install() {
  local code=$?
  trap - EXIT
  set +e
  if ((code && install_started && !install_complete)); then
    if ! rollback_install; then
      printf '自动恢复未全部完成，安装前的配置副本保存在 %s。\n' "$tmp" >&2
      exit "$code"
    fi
  fi
  rm -rf -- "$tmp"
  if [[ -n "$staging" ]]; then rm -rf -- "$staging"; fi
  exit "$code"
}
