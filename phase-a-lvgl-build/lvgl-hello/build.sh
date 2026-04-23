#!/bin/sh
set -eu
export PATH="/workspace/output/host/bin:$PATH"
SRC="$(find lvgl/src -type f -name '*.c')"
arm-buildroot-linux-musleabi-gcc \
  -Os -static -s -Wl,--gc-sections \
  -ffunction-sections -fdata-sections \
  -march=armv5te -mtune=arm926ej-s \
  -DLV_CONF_INCLUDE_SIMPLE=1 \
  -I. -Ilvgl \
  main.c $SRC -lm \
  -o lvgl-hello-armv5
ls -lh lvgl-hello-armv5
