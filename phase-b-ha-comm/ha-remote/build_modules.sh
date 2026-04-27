#!/bin/sh
set -eu
export PATH="/workspace/output/host/bin:$PATH"
SRC="$(find lvgl/src -type f -name '*.c')"

 arm-buildroot-linux-musleabi-gcc \
   -Os -static -s -Wl,--gc-sections \
   -ffunction-sections -fdata-sections \
   -march=armv5te -mtune=arm926ej-s \
   -DLV_CONF_INCLUDE_SIMPLE=1 \
   -I. -Ilvgl -Imicroservices \
   microservices/main.c \
   microservices/fb.c \
   microservices/input.c \
   microservices/hal.c \
   microservices/power_manager.c \
   microservices/ui.c \
   microservices/ha_config.c \
   microservices/ha_ws.c \
   microservices/ws_io.c \
   microservices/stockui.c \
   microservices/crypto.c \
   $SRC -lm \
   -o ha-squeeze-remote-armv5

ls -lh ha-squeeze-remote-armv5
