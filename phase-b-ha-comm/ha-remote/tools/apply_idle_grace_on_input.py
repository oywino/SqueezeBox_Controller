#!/usr/bin/env python3
import sys
from pathlib import Path

GRACE_MS = 15000  # extend session while user is active (key/wheel). Gyro comes next.

def die(msg: str, code: int = 1):
    print(msg, file=sys.stderr)
    sys.exit(code)

OLD = (
    "static void drain_input(int fd) {\n"
    "  if(fd < 0) return;\n"
    "  for(;;) {\n"
    "    struct input_event ev;\n"
    "    ssize_t n = read(fd, &ev, sizeof(ev));\n"
    "    if(n < 0) {\n"
    "      if(errno == EAGAIN || errno == EWOULDBLOCK) break;\n"
    "      break;\n"
    "    }\n"
    "    if(n != sizeof(ev)) break;\n"
    "\n"
    "    if(ev.type == EV_REL && ev.code == REL_WHEEL) {\n"
    "      g_enc_diff += (int32_t)ev.value;\n"
    "    } else if(ev.type == EV_KEY) {\n"
    "      if(ev.value == 1) g_btn_pressed = 1;       /* press */\n"
    "      else if(ev.value == 0) g_btn_pressed = 0;  /* release */\n"
    "    }\n"
    "  }\n"
    "}\n"
)

NEW = (
    "static void drain_input(int fd) {\n"
    "  if(fd < 0) return;\n"
    "  int saw = 0;\n"
    "  for(;;) {\n"
    "    struct input_event ev;\n"
    "    ssize_t n = read(fd, &ev, sizeof(ev));\n"
    "    if(n < 0) {\n"
    "      if(errno == EAGAIN || errno == EWOULDBLOCK) break;\n"
    "      break;\n"
    "    }\n"
    "    if(n != sizeof(ev)) break;\n"
    "    saw = 1;\n"
    "\n"
    "    if(ev.type == EV_REL && ev.code == REL_WHEEL) {\n"
    "      g_enc_diff += (int32_t)ev.value;\n"
    "    } else if(ev.type == EV_KEY) {\n"
    "      if(ev.value == 1) g_btn_pressed = 1;       /* press */\n"
    "      else if(ev.value == 0) g_btn_pressed = 0;  /* release */\n"
    "    }\n"
    "  }\n"
    "\n"
    "  /* While a session is open, keep it alive during user activity; close after idle grace. */\n"
    "  if(saw && g_ha_session_deadline_ms) {\n"
    f"    g_ha_session_deadline_ms = ms_now() + {GRACE_MS};\n"
    "  }\n"
    "}\n"
)

def main():
    if len(sys.argv) != 2:
        die("usage: apply_idle_grace_on_input.py <path/to/main.c>")

    p = Path(sys.argv[1])
    b = p.read_bytes()

    old_lf = OLD.encode("utf-8")
    new_lf = NEW.encode("utf-8")
    old_crlf = old_lf.replace(b"\n", b"\r\n")
    new_crlf = new_lf.replace(b"\n", b"\r\n")

    if b.count(old_lf) == 1:
        p.write_bytes(b.replace(old_lf, new_lf, 1))
        print("OK: updated drain_input to extend session deadline on input (LF).")
        return
    if b.count(old_crlf) == 1:
        p.write_bytes(b.replace(old_crlf, new_crlf, 1))
        print("OK: updated drain_input to extend session deadline on input (CRLF).")
        return

    die(f"ERROR: expected drain_input block not found exactly once (LF count={b.count(old_lf)}, CRLF count={b.count(old_crlf)}).")

if __name__ == "__main__":
    main()
