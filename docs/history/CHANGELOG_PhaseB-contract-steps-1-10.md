# Changelog

## ## v0.6.11-contract-enforcement-step9 - 2025-12-15

- Added ui_status_set(const char *s, int connected, int have_states) in ui.c/.h
- Backend ha_ws.c:set_status() now delegates to ui_status_set() for status updates
- Implemented ha_poll_timer_cb wrapper in ui.c to bridge LVGL timer to backend ha_poll_timer()
- Implemented ui_update_ha_status() as a no-op to suppress unwanted LCD output; console remains the sole channel for HA session state
- Backend scope narrowed further: ha_ws.c owns session lifecycle only
- UI subsystem owns timer glue and presentation hooks, but no longer displays HA status text on LCD
- Build and runtime verified on SB_A 

## ## v0.6.10-contract-enforcement-step8 - 2025-12-15

- Removed duplicate SHA-1 and Base64 code from ha_ws.c; backend now includes crypto.h and calls crypto.c
- Relocated tcp_connect_host() from ha_ws.c into ws_io.c/.h, renamed to ws_tcp_connect()
- Implemented ws_send_text() in ws_io.c to handle masked WebSocket text frames (7/16/64-bit lengths)
- ha_ws.c now calls ws_tcp_connect() and ws_send_text() via ws_io.h
- Backend scope narrowed further: ha_ws.c owns only HA session lifecycle logic
- Build verified: compiles cleanly and runs correctly on SB_A

## ## v0.6.9-contract-enforcement-step7 - 2025-12-13

- Extracted SHA-1 and Base64 primitives from ha_ws.c into new crypto.c/.h
- ha_ws.c now includes crypto.h and calls sha1_init/update/final and b64enc
- Removed duplicate crypto struct and functions from ha_ws.c
- Removed all LVGL types and wrappers from ha_ws.c; UI glue remains in ui.c
- Build script updated to compile crypto.c alongside other modules
- Verified runtime: WebSocket handshake succeeds using crypto module; backend remains LVGL-free

## ## v0.6.8-contract-enforcement-step6 - 2025-12-13

- Backend LVGL detachment completed:
  - Removed lv_timer_t reference from ha_ws.c
  - Backend now exposes ha_poll_timer(void) without LVGL types
  - LVGL wrapper ha_poll_timer_cb(lv_timer_t *t) moved into ui.c
- ha_ws_step6_lvgl_free_header.h generated:
  - Declares only LVGL-free backend API: ha_ws_seed, ha_session_start, ha_poll_timer, ha_session_close, ha_session_note_activity
  - No LVGL types exposed in backend header
- ui.c corrected:
  - Fixed ui_update_ha_status string literals (proper "\n" escapes)
  - Removed duplicated/garbled code fragments
  - Added ha_poll_timer_cb wrapper calling backend ha_poll_timer()
- ui.h updated to declare ha_poll_timer_cb so main.c can register it with lv_timer_create()
- Build script already includes ws_io.c; runtime verified: compiles cleanly and runs correctly on SB_A

## ## v0.6.7-contract-enforcement-step5 - 2025-12-13

- Extracted WebSocket text-frame sender from ha_ws.c into new ws_io.c/.h
- New API: ws_send_text(int fd, const char *s, const uint8_t mask[4])
- ha_ws.c now generates mask per send (rng_u32) and calls ws_send_text
- Removed legacy ws_send_frame_text implementation from ha_ws.c
- Build script updated to compile ws_io.c
- Runtime verified: connect → auth → get_states → snapshot → disconnect unchanged 

## ## v0.6.6-contract-enforcement-step4 - 2025-12-13

- Completed Step 4: removed LVGL dependency from HA session API
- ha_session_start signature simplified to (const char *host, const char *token)
- ha_session_connect_and_auth detached from LVGL types
- set_status signature simplified; no unused label parameter
- ha_refresh_ui delegates to ui_update_ha_status
- LVGL binding now performed in ui.c before calling ha_session_start
- Removed HAL attach/grab debug printf output from hal.c
- Runtime console now limited to LVGL and HA session status messages
- No runtime behavior changes; API contract clarified

## ##v0.6.5-contract-enforcement-step3 - 2025-12-12

- Housekeeping: simplified set_status() in ha_ws.c
- Removed unused lv_obj_t *label parameter from set_status()
- All call sites updated to set_status("...") without label argument
- Function now only updates g_ha_status, prints to console, and refreshes UI
- No runtime behavior changes; handshake/auth flows remain identical
- Modified file: ha_ws.c

## ## v0.6.4-contract-enforcement-step2 - 2025-12-12

- Splitting up ha_ws.c continues (see PROJECT_PLAN.md)
- Ownership of HA status LVGL label moved from ha_ws.c to ui.c
- Added ui_bind_status_label(lv_obj_t *label) in ui.h/ui.c
- ha_ws.c now calls ui_bind_status_label() instead of assigning directly
- ui_update_ha_status() in ui.c now updates the bound label
- Verified on SB_A: UI lifecycle messages intact after relocation
- Modified files: ha_ws.c, ui.c, ui.h

## ##v0.6.3-contract-enforement-step1 - 2025-12-12

- Splitting up ha_ws.c - one step at a time according to contract (See PROJECT_PLAN.md)
- ha_ws.c: ha_refresh_ui() now delegates to ui_update_ha_status()
- ui.c: introduced ui_update_ha_status(), no longer depends on extern g_status_label
- Verified on SB_A: UI lifecycle messages intact
- Modified files: `ha_ws.c, ui.c, ui.h`

## ##v0.6.2‑one‑shot‑ha‑ws_session - 2025-12-12

### **Reason for change:**

We pivoted to a tighter alignment with the Astrion remote’s design philosophy. Instead of maintaining a continuous WebSocket subscription with idle timers and event floods, we now treat each Home Assistant session as a short‑lived, one‑shot transaction. This preserves battery, simplifies logic, and avoids noisy state streams.

### Added:

- A one‑shot HA WebSocket session model: connect → authenticate → request `get_states` → disconnect.

- Explicit UI feedback when a session closes (*“HA: disconnected”*).

### **Changed:**

- `Start HA Session` button now always initiates a fresh connection and snapshot, rather than guarding against “already running.”

- Status messages updated to reflect the new lifecycle (connect, auth, snapshot, disconnect).

### **Removed:**

- Continuous `subscribe_events` logic.

- Handling of `state_changed` events.

- Deadline/keepalive timers and idle wait loops.

- Unused struct fields related to subscriptions and event counts.

### **Improved:**

- Leaner session struct and message handling.

- Reduced stack buffer size for message parsing.

- Cleaner UI refresh logic, showing only relevant state information. 

## ## v0.6.1-ha-config-parser-verified — 2025-12-11

**Summary:** Added ha_config module for JSON‑based card configuration. Verified parsing of config.example.json and logging of cards + tracked entities.

### Changes

- Introduced `microservices/ha_config.h` and `microservices/ha_config.c`.
- Build script updated to compile ha_config.c into ha‑remote‑armv5.
- Added config file at `/workspace/phase-b-ha-comm/ha-remote-armv5/config.example.json`.

### Verification

- Built and deployed to SB_A.
- On startup, `[ha_config] Loaded 3 card(s)` logged with details:
  - Light, Media Player, Climate cards parsed with correct type, entity_id, title.
  - Primary and secondary services logged with data templates.
- Tracked entities list logged (3 entries).
- Wheel navigation moves focus across the three demo cards.
- Short push logs intended primary service; long push logs intended secondary service.
- Exit restores Jive cleanly; HAL detach logs show orderly ungrab/close of wheel, keys, accel.

### Snapshot

- `snapshots/v0.6.1-ha-config-parser-verified`

## v0.6.0-plan-approved

- “Inserted RosCard integration decision as Step 6.”

- “Renumbered subsequent steps.”

- “Expanded deliverables and exit criteria.”

## v0.5.4-hal-input-decoupled-verified — 2025-12-11

**Summary:** HAL now *owns* input lifecycle and polling; `input.c` is a HAL consumer only. Device mapping fixed for SB_A (`event1`=Wheel, `event2`=Matrix keys, `event3`=lis302dl accel). Power shim unchanged.

### Changes

- HAL lifecycle
  
  - Open `/dev/input/event1` (Wheel), `/dev/input/event2` (Keys), `/dev/input/event3` (Accel) with `O_NONBLOCK`.
  - Apply `EVIOCGRAB` on init; release on shutdown.
  - Provide `hal_poll_input()` returning `hal_input_event` `{source,type,code,value,ts_ms}`.
  - Log only attach/detach lines.

- Input module
  
  - `input_init()/input_deinit()` delegate to HAL.
  - `indev_encoder_read()` drains `hal_poll_input(..., 0)` and updates `g_enc_diff`/`g_btn_pressed`.
  - Calls `ha_session_note_activity()` (stub) on any user input event.
  - No direct reads from `/dev/input/*`.

- Unchanged
  
  - Power telemetry via `hal_get_power()`.

### Verification

- Built and deployed to SB_A.
- Wheel moves focus; press toggles; Exit restores stock UI.
- Accel activity lines visible when moving device.
- No accel spam; only attach/detach logs at start/exit.

### Snapshot

- `snapshots/v0.5.4-hal-input-decoupled-verified`

## v0.5.3‑hal‑input‑wired‑verified

**Summary:** Input subsystem modularized with HAL adapter, verified against original monolithic behavior.

### Changes

- Introduced `hal_poll_input()` adapter:
  
  - Unified event polling for wheel and key devices.
  
  - Returns structured `hal_input_event` with source, type, code, value, and monotonic timestamp.

- Added `mono_ms_now_input()` helper for reproducible millisecond timestamps.

- Preserved LVGL integration:
  
  - `indev_encoder_read()` continues to deliver encoder diff and push state.
  
  - Wheel rotation (`REL_WHEEL`) moves focus as before.
  
  - Encoder push (`EV_KEY code=106`) now reliably toggles press/release state.

- Lifecycle functions (`input_init`, `input_deinit`) unchanged in semantics, still manage EVIOCGRAB and device FDs.

- Verified parity:
  
  - “Restart HA Session” button activates correctly.
  
  - “Exit” button now activates correctly.
  
  - No behavioral drift compared to original monolithic `input.c`.

### Verification

- Built and deployed on SB_A.

- Interactive test confirmed:
  
  - Focus outline moves with wheel.
  
  - Both upper and lower buttons respond to encoder push.
  
  - Console debug prints show correct EV_KEY press/release cycles.
  
  Snapshot: `snapshots/v0.5.3-hal-input-wired-verified

## v0.5.2-hal-power-ui-wired-verified — 2025-12-10

### Added

- Introduced minimal HAL shim for power telemetry:
  - `microservices/hal.h` (stable API contract).
  - `microservices/hal.c` (C99 implementation using `/usr/bin/jivectl 23/25`).
- Refactored `microservices/ui.c` power indicator to consume `hal_get_power()` (no direct `jivectl` calls in UI).

### Verified

- Built, deployed, and ran on SB_A: on-screen `AC`/`BAT` and charging indicator behavior unchanged vs. pre-HAL.
- No regressions; app start/exit flows unchanged.

### Snapshot

- `snapshots/v0.5.2-hal-power-ui-wired-verified`

## v0.5.1-hal-skeleton-compiled-verified — 2025-12-10

### Added

- Introduced `hal.c` skeleton implementation under `microservices/`:
  
  - Provides stubs for `hal_init()`, `hal_shutdown()`, `hal_poll_input()`, and `hal_get_power()`.
  - All functions return neutral values; no device access yet.
  - Output structs are zero‑initialized with sentinel values (`-1` for unknown fields).

- Updated build script (`build_modules.sh`) to compile `microservices/hal.c`.

Verified

- Build completed successfully; `ha-remote-armv5` produced with hal.c included.
- Deploy/run on SB_A via `deploy_sb_a.sh` and `run_sb_a_detached.sh` showed **identical behavior** to pre‑HAL binaries.

Snapshot

- Snapshot created using hardlink method:
  - `snapshots/v0.5.1-hal-skeleton-compiled-verified`

## v0.5.0-refactoring-into modules — 2025-12-09

Summary:

- Mechanical refactoring of the monolithic ha-remote main.c into modules, with no functional or logical changes.

- Exit screen behavior preserved exactly (moved from inline show_exit_screen() to ui_show_exit_screen()).

- Build verified using existing module build script; deploy/run unchanged in behavior.

Module layout (under /workspace/phase-b-ha-comm/ha-remote/microservices/):

- fb.c, fb.h

- input.c, input.h

- ui.c, ui.h

- ha_ws.c, ha_ws.h

- stockui.c, stockui.h

- main.c

Notes:

- Snapshot created as hardlink-chained copy (cp -al + rsync delta) in snapshots/v0.5.0-refactoring-into modules.

### v0.4.0 - 2025-12-07

### Exit → restore stock UI (Jive)

- Exit now clears the LCD and shows:
  - `Terminating HA-Remote`
  - `Restarting Jive......... (please wait)`
- Exit restores Jive reliably by killing the PID from `/var/run/squeezeplay.pid` and removing the pidfile (then exiting).
- `run_sb_a_detached.sh` now stops stock UI on the SB before launching `ha-remote` detached.
- Verified: AC/BAT indicator + yellow focus outline still work, and Exit returns to responsive Jive.

#### #### v0.3.11 - 2025-12-06

### UI — Power indicator (cradle/charging)

- Added bottom-right indicator showing `AC`/`BAT` from `jivectl 23`.
- Added `+` when charging from `jivectl 25`.
- Fixed jivectl parsing/capture by reading combined output (`2>&1`) so the value line is not lost.
- Verified indicator toggles correctly when docking/undocking on SB_A.

#### v0.3.10 - 2025-12-06

### UI (2b-2) — Focus indicator

- Added a clear yellow focus indicator on the focused button.
- Tuned focus border thickness to 2px (visible, not overpowering).

### Verified

- On SB_A, focus moves with wheel and the focused button is clearly indicated with a 2px yellow border.
- No new tools installed on the device.

#### v0.3.9 - 2025-12-05

### Observed issue

- When starting `ha-remote-armv5` from the Ubuntu container using the **foreground** runner (`run_sb_a.sh`), the SB_A SSH session would drop on:
  - UI **Exit**, or
  - **Ctrl-C** in the Ubuntu terminal.
    In some cases, this was perceived as a “reboot”.

### Debug findings

- The 15s HA idle timeout cycle was stable and repeatable (no reboot).
- Running `ha-remote-armv5` directly on SB_A (both with Jive running and with Jive stopped) did not cause reboots or SSH loss.
- In at least one Ctrl-C test, `btime` stayed unchanged across the event, indicating it was **not** a kernel reboot in that case (session/process behavior, not system restart).

### Resolution

- Added `run_sb_a_detached.sh` (preferred “run from Ubuntu” method):
  - starts `ha-remote-armv5` on SB_A via `nohup` (survives SSH close),
  - stores PID/log on SB_A: `/tmp/ha-remote.pid` and `/tmp/ha-remote.log`,
  - passes `HA_TOKEN` via stdin (keeps token out of argv/`ps`).
- Fixed detached runner reliability:
  - avoid EOF/`read` pitfalls by using `cat` for token ingestion,
  - fix false failure `rc=1` by making EXIT trap cleanup always return success,
  - verify started PID is alive before reporting success.

### UI (2b-1)

- Disabled the on-screen HA status/debug overlay at top of LCD UI (stdout logging remains).
- Normalized `main.c` line endings from CRLF to LF to allow patching cleanly.
- Fixed build break caused by removing `label` while `set_status(label, ...)` callsites still existed (set `label=NULL` for compile-only).

### UI (2b-1.1) — Wake LCD when stock UI has blanked it

- Symptom: LVGL UI was invisible if the stock UI had already put the LCD to sleep/blank; LVGL was only visible if the LCD was already awake.
- Framebuffer probing (`fbinfo-armv5`): `/dev/fb0` is double-buffered (`yres_virtual=640`); the displayed buffer flips via `yoffset` (`0` vs `320`) depending on state.
- Attempts that did **not** wake the LCD: `FBIOPAN_DISPLAY` (`fbpanset-armv5` yoffset 0/320), `FBIOBLANK` (`fbblank-armv5 0`), and writing `0` to `/sys/class/graphics/fb0/blank`.
- Discovery: running a `jivectl` scan (`jivectl 0..30`) while the LCD was asleep consistently woke the screen; minimal wake action identified as `jivectl 11`.
  - Note: with Jive killed, `jivectl 11` returns `-1`, but the wake side-effect still applies, so the code ignores the exit status/output.
- Implementation: added `lcd_wake()` in `main.c` (calls `/usr/bin/jivectl 11` early) so `ha-remote-armv5` can bring the display back even when the screen was blanked by stock UI.

### Verified

- Detached starts are stable with Jive running or stopped.
- Multiple detached starts in a row work; no reboots observed in detached mode.
- Top 4-row HA status/debug text is gone from the LCD UI.
- LCD wake from stock UI blank works: starting `ha-remote-armv5` now wakes an asleep/blanked screen via `jivectl 11`.

#### v0.3.8 - 2025-12-05

### Added

- Snapshot `snapshots/v0.3.8-stockui-stop-hard-synced`:
  - Syncs `/workspace/phase-b-ha-comm/stockui-stop-hard.sh` to match the verified SB_A variant (watchdog-safe infinite sleep loop).

### Verified

- `/workspace/phase-b-ha-comm/stockui-stop-hard.sh` and SB_A `/mnt/storage/phase-a-lvgl/stockui-stop-hard.sh` confirmed identical after sync (diff clean).
- Snapshot contains the watchdog-safe line:
  - `( while :; do sleep 3600; done ) & WDPID=$!`

### Notes

- Purpose: prevent drift between the `/workspace` copy and the device copy of `stockui-stop-hard.sh`, since both are used operationally.

#### v0.3.7 - 2025-12-05

### Added

- Standardized SB_A deploy/run scripts in `phase-b-ha-comm/ha-remote/`:
  
  - `deploy_sb_a.sh`: deploys `ha-remote-armv5` to SB_A at `/mnt/storage/phase-a-lvgl/` via `scp`, then enforces executable permissions.
  - `run_sb_a.sh`: stops stock UI (`stockui-stop-hard.sh`), then runs `ha-remote-armv5` on SB_A with `HA_HOST` and `HA_TOKEN` (token read from `HA_LL_Token.txt` when not provided via env).

- SB_A legacy SSH requirements applied in the scripts (needed for OpenSSH negotiation from the Ubuntu container):
  
  - `-oKexAlgorithms=+diffie-hellman-group1-sha1`
  - `-oHostKeyAlgorithms=+ssh-rsa`
  - `-oCiphers=aes128-cbc`

- Archived experiment:
  
  - `run_sb_a.sh.stdin-token-attempt` (token passed via stdin to avoid token appearing in `ps` output).

### Verified

- `deploy_sb_a.sh` successfully copied the binary to:
  - `/mnt/storage/phase-a-lvgl/ha-remote-armv5` on SB_A and set correct permissions.
- Restored `run_sb_a.sh` successfully:
  - hard-stops `jive`
  - starts `ha-remote-armv5` and keeps SB_A up (verified by uptime + `pidof`/`ps`).

### Notes

- The stdin-token experiment correlated with an SB_A reboot (uptime reset observed). Kept as an archived variant for later postmortem.
- HA Long-Lived Token was rotated after accidental exposure in a `ps` output.

#### v0.3.6 - 2025-12-04

### Added

- Battery / cradle / charging status reads via `/dev/misc/jive_mgmt` ioctl:
  
  - cmd 17: battery raw
  - cmd 23: cradle/AC present (0=AC/on-cradle, 1=battery/off-cradle)
  - cmd 25: charging sense (0=charging, 1=not charging)
  
  Practical integer `bat_raw` → percent mapping for UI:

- clamp to 0–100%

- `MIN=807` (0%), `MAX=875` (100%)
  -

### Verified

- Values change correctly on/off-cradle with stock UI running.
- Battery level reading + percent mapping matches real batteries on the device (including a ~48% battery).
- The same reads work with:
  - Stock UI (jive) running
  - Stock UI stopped (via `stockui-stop-hard.sh`) while LVGL is running

### Added

- Documented power telemetry available via `/dev/misc/jive_mgmt` ioctls (through `/usr/bin/jivectl`):
  
  - `17`: battery raw value (`bat_raw`)
  - `23`: AC/on-cradle indicator (`0` = AC/on-cradle, `1` = battery/handheld)
  - `25`: charge signal (`0`/`1` as reported by hardware)

- Documented a practical `bat_raw` → percent mapping for UI display:
  
  - clamp to 0–100%
  - `MIN=807` (0%), `MAX=875` (100%)

### Verified

- Battery level reading + percent mapping matches real batteries on the device (including a ~48% battery).
- The same `jivectl` reads work with:
  - Stock UI (jive) running
  - Stock UI stopped (via `stockui-stop-hard.sh`) while LVGL is running

## [0.3.5] - 2025-12-04

### Added

- Activity source: add gyro (/dev/input/event3, lis302dl) to extend WS idle-grace deadline.

### Verified

- moving/picking up device keeps session alive for the grace period
- no new dependencies installed on device

## [0.3.4] - 2025-12-04

### Added

- Activity source: add key-press (/dev/input/event1) to extend WS idle-grace deadline.

### Verified

- button press keeps session alive for the grace period
- no new dependencies installed on device

## [0.3.3] - 2025-12-04

### Added

- Idling watchdog: disconnect WebSocket on inactivity; reconnect on fresh activity.

### Verified

- no longer holds WS open when idle (reduces battery drain)
- still reconnects + resumes UI updates after new activity

## [0.3.2] - 2025-12-04

...

- Updated `send_state()` payload format:
  - `activity_source`: idle / key / wheel / gyro
  - `activity_ts_ms`: monotonic timestamp

### Verified

- HA still updates normally
- idle disconnect/reconnect works

## [0.3.1] - 2025-12-04

### Added

- /dev/input handlers: mouse wheel (event0) and key-press (event1) activity tracking.

### Verified

- wheel scroll increments activity timestamp
- key-press increments activity timestamp