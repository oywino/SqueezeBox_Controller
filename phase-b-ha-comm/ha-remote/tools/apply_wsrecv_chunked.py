#!/usr/bin/env python3
import sys
from pathlib import Path

def die(msg: str, code: int = 1):
    print(msg, file=sys.stderr)
    sys.exit(code)

OLD_BLOCK = (
    "  size_t keep = outlen - 1;\n"
    "  size_t kept = 0;\n"
    "\n"
    "  for(uint64_t i = 0; i < len; i++) {\n"
    "    uint8_t ch;\n"
    "    if(recv_all_sb(sb, fd, &ch, 1, timeout_ms) != 0) return -1;\n"
    "    if(masked) ch ^= mkey[i & 3];\n"
    "    if(kept < keep) out[kept++] = (char)ch;\n"
    "  }\n"
    "  out[kept] = 0;\n"
)

NEW_BLOCK = (
    "  size_t keep = outlen - 1;\n"
    "  size_t kept = 0;\n"
    "\n"
    "  uint64_t off = 0;\n"
    "  uint64_t remaining = len;\n"
    "  uint8_t tmp[512];\n"
    "  while(remaining) {\n"
    "    size_t chunk = (remaining > (uint64_t)sizeof(tmp)) ? sizeof(tmp) : (size_t)remaining;\n"
    "    if(recv_all_sb(sb, fd, tmp, chunk, timeout_ms) != 0) return -1;\n"
    "    if(masked) {\n"
    "      for(size_t i = 0; i < chunk; i++) tmp[i] ^= mkey[(off + (uint64_t)i) & 3];\n"
    "    }\n"
    "    size_t can = chunk;\n"
    "    if(kept + can > keep) can = (kept < keep) ? (keep - kept) : 0;\n"
    "    if(can) {\n"
    "      memcpy(out + kept, tmp, can);\n"
    "      kept += can;\n"
    "    }\n"
    "    off += (uint64_t)chunk;\n"
    "    remaining -= (uint64_t)chunk;\n"
    "  }\n"
    "  out[kept] = 0;\n"
)

def main():
    if len(sys.argv) != 2:
        die("usage: apply_wsrecv_chunked.py <path/to/main.c>")

    p = Path(sys.argv[1])
    b = p.read_bytes()

    old_lf = OLD_BLOCK.encode("utf-8")
    new_lf = NEW_BLOCK.encode("utf-8")

    old_crlf = old_lf.replace(b"\n", b"\r\n")
    new_crlf = new_lf.replace(b"\n", b"\r\n")

    if b.count(old_lf) == 1:
        b2 = b.replace(old_lf, new_lf, 1)
        p.write_bytes(b2)
        print("OK: replaced ws_recv_text_sb payload loop (LF).")
        return
    if b.count(old_crlf) == 1:
        b2 = b.replace(old_crlf, new_crlf, 1)
        p.write_bytes(b2)
        print("OK: replaced ws_recv_text_sb payload loop (CRLF).")
        return

    # Helpful diagnostics
    die(f"ERROR: expected block not found exactly once (LF count={b.count(old_lf)}, CRLF count={b.count(old_crlf)}).")

if __name__ == "__main__":
    main()
