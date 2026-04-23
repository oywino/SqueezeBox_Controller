Here’s the complete contract as it stands, with all ten steps we carried out:

---

## 📑 Contract Enforcement Steps (1–10)

**Step 1 – Crypto separation**

- Extracted SHA1 and Base64 primitives into `crypto.c/.h`.
- Backend now calls crypto functions via explicit interface.
- No UI or HA logic mixed with crypto.

**Step 2 – WebSocket send separation**

- Moved generic WebSocket send (`ws_send_text`) into `ws_io.c/.h`.
- Backend uses `ws_send_text` with masking, no inline framing logic.

**Step 3 – TCP connect separation**

- Moved TCP connect helper (`ws_tcp_connect`) into `ws_io.c/.h`.
- Backend calls `ws_tcp_connect` for socket setup.

**Step 4 – Backend lifecycle isolation**

- `ha_ws.c` owns only HA session lifecycle (connect, auth, get_states, close).
- No LVGL or crypto code inside backend.

**Step 5 – UI subsystem creation**

- Established `ui.c/.h` for LVGL presentation.
- Buttons, power indicator, exit screen implemented here.
- Backend detached from LVGL.

**Step 6 – Timer callback relocation**

- Moved LVGL timer callback wrapper into `ui.c`.
- Backend now 100% LVGL‑free.
- Updated CHANGELOG and PROJECT_STATE accordingly.

**Step 7 – Exit screen restoration**

- Restored exit screen logic in `ui.c`.
- Verified runtime behavior with zero drift.
- Backend lifecycle unaffected.

**Step 8 – Poll timer bridge**

- Implemented `ha_poll_timer_cb` in `ui.c` to bridge LVGL timer to backend `ha_poll_timer()`.
- Backend lifecycle remains clean.

**Step 9 – Status update bridge**

- Added `ui_status_set` in `ui.c/.h`.
- Backend `set_status()` delegates to `ui_status_set`.
- `ui_update_ha_status` suppressed (no LCD output).
- Console remains sole channel for HA session state.

**Step 10 – Generic WS receive/I/O relocation**

- Moved `sockbuf_t`, `sb_init`, `sb_avail`, `sb_set`, `sock_wait`, `recv_all_sb`, `ws_recv_text_sb`, `ws_send_pong` into `ws_io.c/.h`.
- `ha_ws.c` includes `ws_io.h` and uses these helpers.
- No behavior changes; backend lifecycle only.

---

## 📓 Summary

- Monolithic codebase modularized into four subsystems:  
  • `crypto` → primitives  
  • `ws_io` → generic WebSocket/TCP I/O  
  • `ha_ws` → HA session lifecycle  
  • `ui` → LVGL presentation and glue
- Backend is LVGL‑free and crypto‑free.
- Each subsystem owns its state and teardown.
- Exported surfaces are minimal and explicit.
- Verified build and runtime in both attached and detached modes.
- Contract fully enforced and complete.


