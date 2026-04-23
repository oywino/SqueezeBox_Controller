#!/usr/bin/env python3
import sys
from pathlib import Path

def die(msg: str, code: int = 1):
    print(msg, file=sys.stderr)
    sys.exit(code)

def main():
    if len(sys.argv) != 2:
        die("usage: apply_gyro_extend_deadline.py <path/to/main.c>")

    p = Path(sys.argv[1])
    b = p.read_bytes()
    nl = b"\r\n" if b"\r\n" in b else b"\n"

    def ins_after(anchor: bytes, insert: bytes, label: str):
        nonlocal b
        c = b.count(anchor)
        if c != 1:
            die(f"ERROR: {label}: expected 1 match, got {c}")
        b = b.replace(anchor, anchor + insert, 1)

    # 1) Add gyro fd global right after g_fd_keys
    a1 = b"static int g_fd_keys  = -1;" + nl
    if b"g_fd_gyro" not in b:
        ins_after(a1, b"static int g_fd_gyro  = -1;" + nl, "insert g_fd_gyro")

    # 2) Open gyro device (event3 = lis302dl) after keys open
    a2 = b"  g_fd_keys  = open(\"/dev/input/event2\", O_RDONLY | O_NONBLOCK);" + nl
    if b"open(\"/dev/input/event3\"" not in b:
        ins_after(a2, b"  g_fd_gyro  = open(\"/dev/input/event3\", O_RDONLY | O_NONBLOCK);" + nl, "open event3")

    # 3) Drain gyro in encoder read loop (extends WS deadline via existing drain_input logic)
    a3 = b"  drain_input(g_fd_keys);" + nl
    if b"drain_input(g_fd_gyro)" not in b:
        ins_after(a3, b"  drain_input(g_fd_gyro);" + nl, "drain gyro")

    p.write_bytes(b)
    print("OK: added gyro (/dev/input/event3) as activity source to extend session deadline.")

if __name__ == "__main__":
    main()
