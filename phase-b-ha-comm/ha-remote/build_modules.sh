#!/bin/sh
set -eu
export PATH="/workspace/output/host/bin:$PATH"
SRC="$(find lvgl/src -type f -name '*.c')"
ALSA_PREFIX="../../.deps/alsa-armv5"

 arm-buildroot-linux-musleabi-gcc \
   -Os -static -s -Wl,--gc-sections -pthread \
   -ffunction-sections -fdata-sections \
   -march=armv5te -mtune=arm926ej-s \
   -DLV_CONF_INCLUDE_SIMPLE=1 \
   -I. -Ilvgl -Imicroservices -I"$ALSA_PREFIX/include" \
   microservices/main.c \
   microservices/fb.c \
   microservices/input.c \
   microservices/audio_feedback.c \
   microservices/hal.c \
   microservices/power_manager.c \
   microservices/status_cache.c \
   microservices/assets/jive_assets.c \
   microservices/ui.c \
   microservices/ha_config.c \
   microservices/ha_rest.c \
   microservices/ha_ws.c \
   microservices/ws_io.c \
   microservices/stockui.c \
   microservices/crypto.c \
   $SRC "$ALSA_PREFIX/lib/libasound.a" -lm -ldl \
   -o ha-squeeze-remote-armv5

ls -lh ha-squeeze-remote-armv5
