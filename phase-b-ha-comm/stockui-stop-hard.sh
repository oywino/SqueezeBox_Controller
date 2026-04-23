#!/bin/sh
set -eu

# Keep watchdog happy: point pidfile at a harmless long sleep
( while :; do sleep 3600; done ) & WDPID=$!
echo "$WDPID" > /var/run/squeezeplay.pid

# Hard-kill stock UI to avoid it blanking the LCD during shutdown
for p in /proc/[0-9]*; do
  pid="${p##*/}"
  cmd="$(tr '\0' ' ' <"$p/cmdline" 2>/dev/null || true)"

  case "$cmd" in
    */usr/bin/jive*|/usr/bin/jive*)
      kill -9 "$pid" 2>/dev/null || true
      ;;
    *jive_alsa*)
      kill -9 "$pid" 2>/dev/null || true
      ;;
  esac
done

echo "watchdog pidfile -> $WDPID"
