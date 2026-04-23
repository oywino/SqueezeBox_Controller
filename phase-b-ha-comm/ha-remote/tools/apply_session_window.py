#!/usr/bin/env python3
import sys
from pathlib import Path

WINDOW_MS = 15000  # test-only: auto-close window after subscribe

def die(msg: str, code: int = 1):
    print(msg, file=sys.stderr)
    sys.exit(code)

def main():
    if len(sys.argv) != 2:
        die("usage: apply_session_window.py <path/to/main.c>")

    p = Path(sys.argv[1])
    b = p.read_bytes()

    nl = b"\r\n" if b"\r\n" in b else b"\n"

    def replace_once(old: bytes, new: bytes, label: str):
        nonlocal b
        c = b.count(old)
        if c != 1:
            die(f"ERROR: {label}: expected 1 match, got {c}")
        b = b.replace(old, new, 1)

    # 1) Deadline global (after g_ha)
    if b"g_ha_session_deadline_ms" not in b:
        a1 = b"static ha_session_t g_ha = { .fd = -1 };" + nl
        n1 = a1 + b"static uint64_t g_ha_session_deadline_ms = 0; /* auto-close window */" + nl
        replace_once(a1, n1, "insert deadline global")

    # 2) Reset deadline on close
    if b"g_ha_session_deadline_ms = 0;" not in b:
        a2 = b"  g_ha.last_entity[0] = 0;" + nl
        n2 = a2 + b"  g_ha_session_deadline_ms = 0;" + nl
        replace_once(a2, n2, "reset deadline in ha_session_close")

    # 3) Set deadline when subscribe succeeds
    if b"g_ha_session_deadline_ms = ms_now()" not in b:
        a3 = b"  g_ha.subscribed = 1;" + nl
        n3 = a3 + b"  g_ha_session_deadline_ms = ms_now() + %d;" % WINDOW_MS + nl
        replace_once(a3, n3, "set deadline on subscribe success")

    # 4) Auto-close check inside poll timer (after fd check)
    if b"HA: session ended" not in b:
        a4 = b"  if(g_ha.fd < 0) return;" + nl
        n4 = (
            a4 +
            nl +
            b"  if(g_ha_session_deadline_ms && ms_now() >= g_ha_session_deadline_ms) {" + nl +
            b"    set_status(g_status_label, \"HA: session ended\");" + nl +
            b"    ha_session_close();" + nl +
            b"    return;" + nl +
            b"  }" + nl
        )
        replace_once(a4, n4, "insert deadline check in ha_poll_timer_cb")

    p.write_bytes(b)
    print("OK: applied 15s session window.")

if __name__ == "__main__":
    main()
