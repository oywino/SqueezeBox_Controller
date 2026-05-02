#!/bin/sh
set -eu

export PATH="/workspace/output/host/bin:$PATH"

CC=arm-buildroot-linux-musleabi-gcc
ALSA_PREFIX="../../.deps/alsa-armv5"
BUILD_DIR=".build-armv5"
OUT="ha-squeeze-remote-armv5"

CFLAGS="-Os -ffunction-sections -fdata-sections -march=armv5te -mtune=arm926ej-s -DLV_CONF_INCLUDE_SIMPLE=1 -I. -Ilvgl -Imicroservices -I$ALSA_PREFIX/include"
LDFLAGS="-static -s -Wl,--gc-sections -pthread"
LIBS="$ALSA_PREFIX/lib/libasound.a -lm -ldl"

SOURCES="
microservices/main.c
microservices/fb.c
microservices/input.c
microservices/audio_feedback.c
microservices/hal.c
microservices/power_manager.c
microservices/status_cache.c
microservices/assets/jive_assets.c
microservices/ui.c
microservices/ha_config.c
microservices/ha_rest.c
microservices/ha_ws.c
microservices/ws_io.c
microservices/stockui.c
microservices/crypto.c
"

LVGL_SOURCES="$(find lvgl/src -type f -name '*.c')"

mkdir -p "$BUILD_DIR"

OBJECTS=""
for src in $SOURCES $LVGL_SOURCES; do
  obj="$BUILD_DIR/$(printf '%s' "$src" | sed 's#[/\\]#_#g').o"
  dep="$obj.d"
  rebuild=0

  if [ ! -f "$obj" ] || [ "$src" -nt "$obj" ]; then
    rebuild=1
  elif [ -f "$dep" ]; then
    deps="$(sed -e 's/\\$//' -e 's/^[^:]*://' "$dep")"
    for hdr in $deps; do
      if [ -f "$hdr" ] && [ "$hdr" -nt "$obj" ]; then
        rebuild=1
        break
      fi
    done
  fi

  if [ "$rebuild" -eq 1 ]; then
    $CC $CFLAGS -MMD -MP -c "$src" -o "$obj"
  fi
  OBJECTS="$OBJECTS $obj"
done

$CC $LDFLAGS $OBJECTS $LIBS -o "$OUT"
ls -lh "$OUT"
