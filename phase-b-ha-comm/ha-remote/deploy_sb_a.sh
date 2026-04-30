#!/bin/sh
set -eu

SB_A_HOST="${SB_A_HOST:-root@192.168.1.65}"
REMOTE_DIR="${REMOTE_DIR:-/mnt/storage/phase-a-lvgl}"
BIN_NAME="${BIN_NAME:-ha-squeeze-remote-armv5}"
CONFIG_NAME="${CONFIG_NAME:-config.example.json}"

# SB_A offers only legacy algorithms; allow explicitly for this target.
SSH_OPTS="${SSH_OPTS:--oKexAlgorithms=+diffie-hellman-group1-sha1 -oHostKeyAlgorithms=+ssh-rsa -oPubkeyAcceptedAlgorithms=+ssh-rsa -oCiphers=aes128-cbc -oMACs=+hmac-sha1}"

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
LOCAL_BIN="$SCRIPT_DIR/$BIN_NAME"
LOCAL_CONFIG="$SCRIPT_DIR/microservices/config/$CONFIG_NAME"

if [ ! -f "$LOCAL_BIN" ]; then
  echo "ERROR: Missing local binary: $LOCAL_BIN" >&2
  echo "Build it first (e.g. ./build.sh)." >&2
  exit 1
fi

scp $SSH_OPTS "$LOCAL_BIN" "$SB_A_HOST:$REMOTE_DIR/$BIN_NAME"
scp $SSH_OPTS "$LOCAL_CONFIG" "$SB_A_HOST:$REMOTE_DIR/$CONFIG_NAME"
ssh $SSH_OPTS "$SB_A_HOST" "chmod 755 '$REMOTE_DIR/$BIN_NAME' && ls -lh '$REMOTE_DIR/$BIN_NAME'"
