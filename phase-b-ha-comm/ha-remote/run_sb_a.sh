#!/bin/sh
set -eu

SB_A_HOST="${SB_A_HOST:-root@192.168.1.94}"
REMOTE_DIR="${REMOTE_DIR:-/mnt/storage/phase-a-lvgl}"
BIN_PATH="${BIN_PATH:-$REMOTE_DIR/ha-remote-armv5}"
STOP_UI="${STOP_UI:-$REMOTE_DIR/stockui-stop-hard.sh}"

HA_HOST="${HA_HOST:-192.168.1.8}"

# SB_A offers only legacy algorithms; allow explicitly for this target.
SSH_OPTS="${SSH_OPTS:--oKexAlgorithms=+diffie-hellman-group1-sha1 -oHostKeyAlgorithms=+ssh-rsa -oCiphers=aes128-cbc}"

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
TOKEN_FILE="${TOKEN_FILE:-$SCRIPT_DIR/HA_LL_Token.txt}"

if [ -z "${HA_TOKEN:-}" ]; then
  if [ -f "$TOKEN_FILE" ]; then
    HA_TOKEN="$(tr -d '\r\n' < "$TOKEN_FILE")"
  else
    echo "ERROR: HA_TOKEN not set and token file missing: $TOKEN_FILE" >&2
    exit 1
  fi
fi

case "$HA_TOKEN" in
  ""|"<ha-ll-token>"|"<REDACTED>"|REDACTED*)
    echo "ERROR: HA_TOKEN looks placeholder/redacted." >&2
    exit 1
    ;;
esac

ssh $SSH_OPTS -tt "$SB_A_HOST" "set -eu; '$STOP_UI'; HA_HOST='$HA_HOST' HA_TOKEN='$HA_TOKEN' '$BIN_PATH'"
