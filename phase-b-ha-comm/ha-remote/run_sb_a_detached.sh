#!/bin/sh
set -eu

SB_A_HOST="${SB_A_HOST:-root@192.168.1.65}"
REMOTE_DIR="${REMOTE_DIR:-/mnt/storage/phase-a-lvgl}"
BIN_NAME="${BIN_NAME:-ha-remote-armv5}"
HA_HOST="${HA_HOST:-192.168.1.8}"

# Legacy SSH requirements for SB_A
SSH_OPTS="${SSH_OPTS:--oKexAlgorithms=+diffie-hellman-group1-sha1 -oHostKeyAlgorithms=+ssh-rsa -oPubkeyAcceptedAlgorithms=+ssh-rsa -oCiphers=aes128-cbc -oMACs=+hmac-sha1}"

ssh $SSH_OPTS "$SB_A_HOST" "set -eu
REMOTE_DIR='$REMOTE_DIR'
BIN=\"\$REMOTE_DIR/$BIN_NAME\"
STOPUI=\"\$REMOTE_DIR/stockui-stop-hard.sh\"
TOKEN_FILE=\"\$REMOTE_DIR/HA_LL_Token.txt\"

[ -x \"\$STOPUI\" ] || { echo \"ERROR: missing or not executable: \$STOPUI\" >&2; exit 10; }
[ -x \"\$BIN\" ]    || { echo \"ERROR: missing or not executable: \$BIN\" >&2; exit 11; }
[ -f \"\$TOKEN_FILE\" ] || { echo \"ERROR: missing: \$TOKEN_FILE\" >&2; exit 12; }

# Stop stock UI the canonical way
\"\$STOPUI\"

export HA_HOST='$HA_HOST'
export HA_TOKEN=\$(cat \"\$TOKEN_FILE\" 2>/dev/null || true)
[ -n \"\$HA_TOKEN\" ] || { echo \"ERROR: empty HA_TOKEN (file: \$TOKEN_FILE)\" >&2; exit 13; }

nohup \"\$BIN\" </dev/null >/tmp/ha-remote.log 2>&1 &
PID=\$!
echo \$PID >/tmp/ha-remote.pid
echo \"started pid=\$PID\"
"

echo "rc=$?"
