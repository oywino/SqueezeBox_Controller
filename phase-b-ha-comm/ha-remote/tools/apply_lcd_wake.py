#!/usr/bin/env python3
import re
import sys
from pathlib import Path

p = Path(sys.argv[1] if len(sys.argv) > 1 else "main.c")
s = p.read_text(encoding="utf-8")

if "static void lcd_wake(void)" not in s:
    m = re.search(r"(static void keep_visible\(lv_timer_t \*t\)\s*\{.*?\n\}\n)\n", s, flags=re.S)
    if not m:
        raise SystemExit("ERROR: could not find keep_visible() block to insert lcd_wake() after")
    insert = (
        m.group(1)
        + "\n"
        + "static void lcd_wake(void) {\n"
        + "  /* Wake LCD panel even if stock UI (Jive) has put it to sleep. */\n"
        + "  (void)system(\"/usr/bin/jivectl 11 >/dev/null 2>&1\");\n"
        + "}\n"
        + "\n"
    )
    s = s[:m.start()] + insert + s[m.end():]

# Insert call once, right before lv_init()
if re.search(r"^\s*lcd_wake\(\);\s*$", s, flags=re.M) is None:
    if "  lv_init();" not in s:
        raise SystemExit("ERROR: could not find '  lv_init();' to insert lcd_wake() call before")
    s = s.replace("  lv_init();", "  lcd_wake();\n\n  lv_init();", 1)

p.write_text(s, encoding="utf-8")
print("OK: updated", p)
