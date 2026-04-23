#!/bin/sh
set -eu

TAG="${1:-state}"

echo "== TAG=$TAG =="; date || true
echo "== uptime =="; cat /proc/uptime || true
echo

echo "== /proc/fb =="; cat /proc/fb 2>/dev/null || true
echo

echo "== fb devices =="; ls -l /dev/fb* 2>/dev/null || true
echo

echo "== fb0 sysfs =="
for f in /sys/class/graphics/fb0/blank /sys/class/graphics/fb0/modes /sys/class/graphics/fb0/mode /sys/class/graphics/fb0/name; do
  if [ -e "$f" ]; then
    echo "-- $f --"
    cat "$f" 2>/dev/null || true
  fi
done
echo

echo "== backlight sysfs =="
if [ -d /sys/class/backlight ]; then
  for d in /sys/class/backlight/*; do
    [ -d "$d" ] || continue
    echo "-- $d --"
    for f in brightness actual_brightness max_brightness bl_power; do
      if [ -e "$d/$f" ]; then
        v="$(cat "$d/$f" 2>/dev/null || true)"
        echo "$f=$v"
      fi
    done
  done
else
  echo "no /sys/class/backlight"
fi
echo

echo "== commands present =="
for c in fbset setterm vtswitch; do
  if command -v "$c" >/dev/null 2>&1; then
    echo "have $c=$(command -v "$c")"
  else
    echo "missing $c"
  fi
done
