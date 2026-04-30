#!/bin/sh
set -eu

SB_A_HOST="${SB_A_HOST:-root@192.168.1.65}"
REMOTE_DIR="${REMOTE_DIR:-/mnt/storage/phase-a-lvgl}"
BIN_PATH="${BIN_PATH:-$REMOTE_DIR/ha-squeeze-remote-armv5}"
STOP_UI="${STOP_UI:-$REMOTE_DIR/stockui-stop-hard.sh}"

HA_HOST="${HA_HOST:-192.168.1.8}"

# SB_A offers only legacy algorithms; allow explicitly for this target.
SSH_OPTS="${SSH_OPTS:--oKexAlgorithms=+diffie-hellman-group1-sha1 -oHostKeyAlgorithms=+ssh-rsa -oPubkeyAcceptedAlgorithms=+ssh-rsa -oCiphers=aes128-cbc -oMACs=+hmac-sha1}"

ssh $SSH_OPTS -tt "$SB_A_HOST" "set -eu; cd '$REMOTE_DIR'; '$STOP_UI'; HA_HOST='$HA_HOST' '$BIN_PATH'"
