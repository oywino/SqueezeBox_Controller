# <u>Focus outline addition</u>

I think it's about time we split main.c into a few logical chunks without losing anything. it's important to make sure each chunk is self‑contained and clearly marked/named so that they can be maintained and built separately, then joined into a single deploy and then run.
What do you think? 

Yes.

Objectives:

- Improve maintainability by separating **framebuffer**, **input**, **UI**, **HA websocket/session**, and **stock UI restore**.

- Keep behavior identical (same binary `ha-remote-armv5`, same `build.sh`/deploy/run flow).

Step 1 (proposal to confirm before creating any new files):

- `fb.c/.h` — `/dev/fb0` open/mmap, `fb_flush_cb`, `lcd_wake`

- `input.c/.h` — open/close input fds, `drain_input`, `indev_encoder_read`, grabs

- `ha_ws.c/.h` — websocket + HA session (connect/auth/send/recv/poll)

- `ui.c/.h` — LVGL screen creation, buttons, focus border, AC/BAT indicator, exit screen text

- `stockui.c/.h` — `stockui_restart_via_pidfile` + pidfile helpers

- `main.c` — orchestration only (init order, loop, shutdown sequence)

Reply **yes/no**: proceed with this exact split and filenames.

Is this a useful logical breakdown for those files:

fb.c / fb.h

- Framebuffer setup, flush callback, cleanup.
  -Functions: fb_flush_cb, framebuffer init/teardown.

input.c / input.h
 -Input device handling (wheel, keys, gyro).
 -Functions: drain_input, indev_encoder_read.

ha_session.c / ha_session.h

- WebSocket client, SHA1/Base64 helpers, HA session connect/auth/subscribe, message processing.
- Functions: ha_session_start, ha_session_close, ha_poll_timer_cb.

ui.c / ui.h

- LVGL setup, buttons, labels, power indicator, event callbacks.
- Functions: btn_probe_cb, btn_exit_cb, pwr_indicator_update.

main.c
 -Entry point only: seeds RNG, opens devices, initializes subsystems, runs main loop, handles cleanup.

- Calls into the other modules.

How to join them:

- Each .c file includes its own .h for declarations.
- main.c includes all headers.
- Makefile or build system compiles each .c into an object file, then links them together.

An example build line could look like this:
gcc -o app main.c fb.c input.c ha_session.c ui.c -llvgl

OK, let's do it like this:
Review of the proposed split:

fb.c / fb.h

- Owns /dev/fb0 open/mmap/close.
- Provides fb_flush_cb for LVGL.
- Provides lcd_wake() helper.
- Keeps framebuffer state encapsulated.

input.c / input.h

- Owns input device file descriptors.
- Provides drain_input() and indev_encoder_read() callbacks.
- Handles EVIOCGRAB lifecycle.
- Encapsulates encoder/button state.

ha_ws.c / ha_ws.h

- Encapsulates WebSocket protocol, SHA1/Base64 helpers, and HA session logic.
- Provides ha_session_start(), ha_session_close(), ha_poll_timer_cb().
- Keeps networking separate from UI.

ui.c / ui.h

- Builds LVGL screen, buttons, focus border, AC/BAT indicator.
- Provides event callbacks (btn_probe_cb, btn_exit_cb).
- Encapsulates LVGL object creation and styling.

stockui.c / stockui.h

- Provides stockui_restart_via_pidfile() and pidfile helpers.
- Isolated teardown logic for stock UI handoff.

main.c

- Orchestrates init order: fb → input → LVGL/UI → HA session.
- Runs main loop.
- Calls cleanup in reverse order.
- No subsystem logic inside — just orchestration.

Before creating files, please confirm:

- Naming convention: ha_ws.c vs ha_session.c — consistent choices.
- Header contents: each .h should expose only the functions needed by main.c (and possibly other modules).
- Global state: move globals into their respective .c files, expose only what’s necessary.

If you agree, then draft the header skeletons (fb.h, input.h, ha_ws.h, ui.h, stockui.h) with function prototypes and minimal comments, so I can see exactly how the split will look before we start moving code.

## Relocation plan (from current monolithic `main.c`)

### `fb.c / fb.h`

Move **globals**:

- `g_fb0`, `g_fb1`

- `g_fb_map`, `g_fb_map_len`

- `g_fb_fd`, `g_line_len`

- `g_w`, `g_h`

Move **functions**:

- `fb_flush_cb(...)`

- `lcd_wake(void)`

Move **main() code blocks** (mechanically lifted into `fb_init()` / `fb_deinit()`):

- `/dev/fb0` open + `FBIOGET_VSCREENINFO/FBIOGET_FSCREENINFO` + `mmap` + `g_fb0/g_fb1` pointer setup (currently around lines ~931–976)

- `munmap` + `close` cleanup (currently after the main loop, around lines ~1073–1081)

---

### `input.c / input.h`

Move **globals**:

- `g_fd_wheel`, `g_fd_keys`, `g_fd_gyro`

- `g_enc_diff`, `g_btn_pressed`

Move **functions**:

- `drain_input(int fd)`

- `indev_encoder_read(...)`

Move **main() code blocks** into `input_init()` / `input_deinit()`:

- open `/dev/input/event1`, `/dev/input/event2`, `/dev/input/event3` (with `O_NONBLOCK`)

- `EVIOCGRAB` lifecycle (grab in init, release in deinit)

- current close/ungrab block near exit (currently around lines ~1067–1070)

---

### `ha_ws.c / ha_ws.h`

Move **globals**:

- `rng_state`

- `g_ha_status[128]`

- `g_ha` (`ha_session_t` instance)

- `g_ha_session_deadline_ms`

- (keep here as well) `g_status_label` (currently used by `ha_refresh_ui()` / `set_status()`, even though it’s NULL today)

Move **types + helpers**:

- `sha1_ctx_t`, `sockbuf_t`, `ha_session_t`

- `rol32`, SHA1 functions: `sha1_init`, `sha1_block`, `sha1_update`, `sha1_final`

- Base64: `b64enc`

Move **functions**:

- `ms_now(void)` (used heavily by HA/WS timing; main currently also uses it for ticks)

- `ha_refresh_ui`, `set_status`

- WebSocket + socket helpers: `sock_wait`, `recv_all_sb`, `tcp_connect_host`, `ws_send_frame_text`, `ws_send_pong`, `ws_recv_text_sb`, `msg_has_id`

- HA session: `ha_session_connect_and_auth`, `ha_session_start`, `ha_session_close`, `ha_session_process_msg`, `ha_poll_timer_cb`

- RNG: `rng_u32`

---

### `ui.c / ui.h`

Move **globals**:

- `g_should_exit`

- `g_power_label`

Move **functions**:

- `btn_probe_cb(...)`

- `btn_exit_cb(...)`

- `keep_visible(...)`

- power indicator: `read_jivectl_value`, `pwr_indicator_update`, `pwr_indicator_timer_cb`

Move **main() UI construction code block** into `ui_init(...)`:

- screen bg color set

- create `g_power_label`, its styling + alignment, start its timer

- create the two buttons, labels, focus styling, group add, and initial focus (currently around lines ~1001–1042)

- `lv_timer_create(keep_visible, ...)` (currently line ~1052)

(Leave “start HA session at startup from env” in `main.c` orchestration, not inside `ui.c`.)

---

### `stockui.c / stockui.h`

Move **functions**:

- `read_pidfile_int(...)`

- `stockui_restart_via_pidfile(void)`

(No globals needed.)

---

### `main.c` (after split)

Keep only orchestration:

- env read of `HA_HOST` / `HA_TOKEN`

- LVGL driver registration (`lv_disp_drv_*`, `lv_indev_drv_*`) using exported `fb_flush_cb` + exported `indev_encoder_read`

- creating LVGL `group`, attaching it to indev, calling `ui_init(group)`

- creating `lv_timer_create(ha_poll_timer_cb, ...)`

- main loop (tick + handler + sleep) until UI signals exit

- cleanup order: `ha_session_close()` → `input_deinit()` → `fb_deinit()` → `stockui_restart_via_pidfile()`

---

Confirm this relocation plan.

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
