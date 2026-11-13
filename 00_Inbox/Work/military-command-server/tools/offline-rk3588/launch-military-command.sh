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
if [[ -f "$release/military-command-tools.desktop" ]] && command -v xdg-mime >/dev/null; then
  applications="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
  install -D -m 0644 "$release/military-command-tools.desktop" "$applications/military-command-tools.desktop"
  xdg-mime default military-command-tools.desktop x-scheme-handler/military-command
fi

# Trust the local voice assistant certificate by public key in this browser profile.
browser_flags=()
certificate=/opt/jw/.ssl/cert.pem
if [[ -r "$certificate" ]] && command -v openssl >/dev/null; then
  if pin=$(openssl x509 -in "$certificate" -pubkey -noout | openssl pkey -pubin -outform DER | openssl dgst -sha256 -binary | openssl base64 -A) && [[ -n "$pin" ]]; then
    browser_flags+=("--ignore-certificate-errors-spki-list=$pin")
  fi
fi

exec /usr/bin/chromium "${browser_flags[@]}" --use-gl="$gl" --enable-webgl \
  --disable-accelerated-video-decode --disable-features=VaapiVideoDecoder \
  --user-data-dir="${XDG_CONFIG_HOME:-$HOME/.config}/target-guidance-browser/$profile" \
  --no-first-run --start-maximized http://127.0.0.1:4177/
