#!/usr/bin/env bash
set -eu

# LubanCat's native X11 session can use Mali EGL. Its XRDP virtual screen
# cannot initialize the vendor EGL driver, so use Mesa GLX there.
mali=/usr/lib/aarch64-linux-gnu/mali
case ${DISPLAY:-} in
  :0|:0.0)
    profile=local
    if [[ -e "$mali/libEGL.so.1" ]]; then
      unset LIBGL_ALWAYS_SOFTWARE
      export LD_LIBRARY_PATH="$mali${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
      gl=egl
    else
      gl=desktop
    fi
    ;;
  *)
    profile=remote
    gl=desktop
    ;;
esac

# Register native tool links for the current desktop user.
release=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
desktop_file="$release/intelligent-cooperative-tools.desktop"
[[ -f "$desktop_file" ]] || desktop_file="$release/intelligent-command-tools.desktop"
[[ -f "$desktop_file" ]] || desktop_file="$release/dispatch-tools.desktop"
[[ -f "$desktop_file" ]] || desktop_file="$release/command-tools.desktop"
if [[ -f "$desktop_file" ]] && command -v xdg-mime >/dev/null; then
  applications="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
  install -D -m 0644 "$desktop_file" "$applications/intelligent-cooperative-tools.desktop"
  xdg-mime default intelligent-cooperative-tools.desktop x-scheme-handler/intelligent-cooperative
  xdg-mime default intelligent-cooperative-tools.desktop x-scheme-handler/intelligent-cooperative-tools
  xdg-mime default intelligent-cooperative-tools.desktop x-scheme-handler/intelligent-command
  xdg-mime default intelligent-cooperative-tools.desktop x-scheme-handler/intelligent-command-tools
  xdg-mime default intelligent-cooperative-tools.desktop x-scheme-handler/dispatch-tools
fi

# Trust the local voice assistant certificate by public key in this browser profile.
browser_flags=()
certificate=/opt/jw/.ssl/cert.pem
if [[ -r "$certificate" ]] && command -v openssl >/dev/null; then
  if pin=$(openssl x509 -in "$certificate" -pubkey -noout | openssl pkey -pubin -outform DER | openssl dgst -sha256 -binary | openssl base64 -A) && [[ -n "$pin" ]]; then
    browser_flags+=("--ignore-certificate-errors-spki-list=$pin")
  fi
fi

config_dir="${XDG_CONFIG_HOME:-$HOME/.config}"
browser_dir="$config_dir/intelligent-cooperative-server-browser"
old_browser_dir="$config_dir/intelligent-command-server-browser"
older_browser_dir="$config_dir/dispatch-server-browser"
ancient_browser_dir="$config_dir/command-server-browser"
if [[ -d "$ancient_browser_dir" && ! -e "$older_browser_dir" ]]; then
  mv -T -- "$ancient_browser_dir" "$older_browser_dir"
fi
if [[ -d "$older_browser_dir" && ! -e "$old_browser_dir" ]]; then
  mv -T -- "$older_browser_dir" "$old_browser_dir"
fi
if [[ -d "$old_browser_dir" && ! -e "$browser_dir" && ! -L "$browser_dir" ]]; then
  if pgrep -u "$(id -u)" -f -- "--user-data-dir=$old_browser_dir/" >/dev/null; then
    printf '请先关闭旧协同作业平台的 Chromium 窗口，再启动新版。\n' >&2
    exit 1
  fi
  mv -T -- "$old_browser_dir" "$browser_dir"
fi

exec /usr/bin/chromium "${browser_flags[@]}" --use-gl="$gl" --enable-webgl \
  --disable-accelerated-video-decode --disable-features=VaapiVideoDecoder \
  --user-data-dir="$browser_dir/$profile" \
  --no-first-run --start-maximized http://192.168.0.201:4177/
