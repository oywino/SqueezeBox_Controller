#!/bin/sh
set -eu

# Fetch binaries for offline inspection
scp -O \
  -c aes128-cbc \
  -oKexAlgorithms=+diffie-hellman-group1-sha1 \
  -oHostKeyAlgorithms=+ssh-rsa \
  -oPubkeyAcceptedAlgorithms=+ssh-rsa \
  -oMACs=+hmac-sha1 \
  root@192.168.1.94:/usr/bin/jive ./jive

scp -O \
  -c aes128-cbc \
  -oKexAlgorithms=+diffie-hellman-group1-sha1 \
  -oHostKeyAlgorithms=+ssh-rsa \
  -oPubkeyAcceptedAlgorithms=+ssh-rsa \
  -oMACs=+hmac-sha1 \
  root@192.168.1.94:/usr/bin/jive_alsa ./jive_alsa || true

export PATH="/workspace/output/host/bin:$PATH"

echo "=== strings: misc device names ==="
arm-buildroot-linux-musleabi-strings -a ./jive | grep -E '/dev/misc|jive_mgmt|irtx|watchdog|fb0|/dev/input' || true

echo "=== readelf: NEEDED libs ==="
arm-buildroot-linux-musleabi-readelf -d ./jive 2>/dev/null | grep -E 'NEEDED|RPATH|RUNPATH' || true

echo "=== strings: power/blank/backlight keywords (first 60 hits) ==="
arm-buildroot-linux-musleabi-strings -a ./jive | grep -Ei 'blank|backlight|brightness|FBIO|ioctl|s3c|lcd|panel|power' | head -n 60 || true
