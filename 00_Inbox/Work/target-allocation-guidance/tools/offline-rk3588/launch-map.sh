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

exec /usr/bin/chromium --use-gl="$gl" --enable-webgl \
  --disable-accelerated-video-decode --disable-features=VaapiVideoDecoder \
  --user-data-dir="${XDG_CONFIG_HOME:-$HOME/.config}/target-guidance-browser/$profile" \
  --no-first-run --start-maximized http://127.0.0.1:4177/
