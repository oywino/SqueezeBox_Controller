# Changelog — Phase B

---

## v0.8.16-phase-b-card-focus-select-guard — 2026-04-30

- Added the approved four-card MVP focus model: Light, Cover, Switch, Media.
- Wheel focus now moves through the three visible cards first, then scrolls the card list by one card at the top/bottom edge.
- Added card-scroll rate limiting to reduce jumpy focus movement while preserving no-wrap/no-skip behavior.
- Guarded rotary/select push so the existing switch toggle only fires when `switch.ikea_power_plug` is the focused card.
- Updated the config example HA base URL to the verified Home Assistant address `http://192.168.1.8:8123`.
- Built and deployed to Squeezebox controller `192.168.1.65`; startup REST state fetch verified `4/4 ok`.

---

## v0.8.15-phase-b-switch-action — 2026-04-30

- No visual design change.
- Added the first HA service-call action: rotary/select push calls `switch.toggle` for `switch.ikea_power_plug`.
- Reuses the REST HA connection path and refreshes/logs the switch state after the service call.
- Suppressed ALSA library underrun noise in the runtime log and increased audio stream buffer headroom.
- Built and deployed to Squeezebox controller `192.168.1.65`; user verified normal switch-toggle interaction works.

---

## v0.8.14-phase-b-rest-states-sleep — 2026-04-30

- No visual design change.
- Added `ha_rest` as a separate microservice for configured-entity REST state fetches.
- Kept normal MVP state fetch off WebSocket `get_states`; it now uses REST `GET /api/states/<entity_id>` for configured entities only.
- Configured the MVP entities as `light.sov_2_tak`, `cover.screen_sov_2`, `switch.ikea_power_plug`, and `media_player.squeezebox_boom`.
- Logged current state for each configured MVP entity at startup.
- Added config parsing for HA `base_url` and `access_token`, with runtime token fallback to `HA_TOKEN` or `HA_LL_Token.txt`.
- Fixed sleep behavior so the LCD/backlight is powered off via `/sys/class/lcd/ili9320/power=4` and restored on wake.
- Reduced sleeping runtime activity by skipping UI/timer handling while keeping input and accelerometer wake polling active.
- Built and deployed to Squeezebox controller `192.168.1.65`; user verified sleep backlight-off, key wake, shake wake, no Jive/UI framebuffer mixing, and four HA state log lines.

---

## v0.8.13-phase-b-audio-rate-wheel-coalesce — 2026-04-29

- No visual design change.
- Reduced audio feedback playback from 44.1 kHz stereo 16-bit to 22.05 kHz stereo 16-bit after hardware probing confirmed that `hw:0,0` supports 22.05 kHz stereo S16_LE but rejects 8-bit and mono modes.
- Coalesced excess wheel input to the latest direction so fast wheel movement cannot replay a backlog and skip visible menu rows.
- Built and deployed to Squeezebox controller `192.168.1.65`; user verified that slow menu scrolling no longer produces broken/choppy beeps.

---

## v0.8.12-phase-b-responsive-menu-audio — 2026-04-28

- No visual design change.
- Added the `audio_feedback` microservice with isolated worker-thread playback for menu/button feedback.
- Added a dedicated input polling thread so Linux input reads are no longer tied directly to LVGL's input callback.
- Added menu wheel selection handling: Home opens the approved pull-out menu, wheel moves the selected row, no wrap.
- Measured the actual responsiveness bottleneck with temporary instrumentation and removed it afterward.
- Removed the forced full-screen redraw timer that invalidated the display every 30 ms; this was the root cause of severe lag and choppy sound.
- Added architecture notes for the Jive-aligned and threaded runtime model.
- Hardware-verified on Squeezebox controller `192.168.1.65`: menu response is near perfect and sound is stable.

---

## v0.8.11-phase-b-input-response-cache — 2026-04-27

- No visual change.
- Added `status_cache` microservice so slow power/WiFi shell polling no longer runs in the UI/input path.
- Changed Home short action to fire on key-down while preserving long-press emergency Exit behavior.
- Recorded the rule that user input should respond as fast as possible, with progress indication for unavoidable delays above 3 seconds.
- Built and deployed to Squeezebox controller `192.168.1.65`; user accepted current responsiveness for now, with deeper Jive-level responsiveness deferred.

---

## v0.8.10-phase-b-hal-wifi-refactor — 2026-04-27

- No visual change.
- Moved WiFi connected-state detection out of `ui.c` and into HAL as `hal_get_wifi()`.
- Kept UI responsibility limited to rendering the already-approved Jive WiFi icon based on HAL state.
- Build verified.

---

## v0.8.9-phase-b-jive-status-icons-fonts — 2026-04-27

- Replaced LVGL status glyphs with extracted Jive WiFi and battery/AC PNG assets.
- Embedded Jive `FreeSans` and `FreeSansBold` font assets for the approved visual shell.
- Updated WiFi connected-state detection for the controller's WiFi-as-`eth0` behavior.
- Recorded the rule that approved graphical details are version-bound and must restore exactly on tag/release revert unless explicitly changed.
- Hardware-approved on Squeezebox controller `192.168.1.65`; card placeholder icons remain as currently displayed.

---

## v0.8.8-phase-b-ui-shell-sleep — 2026-04-27

- Renamed the Phase B runtime binary to `ha-squeeze-remote-armv5`; preserved the last verified `ha-remote-armv5` test binary untouched.
- Added the first Phase B UI shell: screen section outline, AC/BAT cradle indicator, placeholder left menu on short Home, and emergency Exit on long Home.
- Reworked keypad long-press handling into a reusable input-layer key binding model with Jive-like hold timing.
- Added verified sleep behavior: BAT sleeps after about 30 seconds idle, AC stays awake past 30 seconds, and key/wheel/accelerometer activity wakes the screen.
- Hardware-verified on Squeezebox controller `192.168.1.65`.

---

## v0.8.7-phase-b-mvp-config — 2026-04-26

- Recorded the approved Phase B MVP direction: prove `Squeezebox UI -> HA live state -> user action -> HA service call -> visible result`.
- Updated `config.example.json` to use the current parser fields (`type`, `title`, `actions`) instead of the earlier RosCard skeleton-only fields.
- Dropped Climate from the Phase B MVP card list.
- Set the Phase B MVP example cards to Light, Cover, Switch, and Media Player.
- No runtime code changes and no Squeezebox deploy/run.

---

## v0.8.6-tools-deploy-docs-normalized — 2026-04-26

- Added canonical `phase-b-ha-comm/tools/README.md` for current build/deploy/test operations.
- Documented verified NAS access, Ubuntu container entry, Squeezebox SSH procedure, current controller `192.168.1.65`, and detached runtime test result.
- Removed obsolete current-operation references to legacy drive-letter workspace paths and old Squeezebox IP examples from active docs.
- Sanitized legacy run note by removing the embedded Home Assistant long-lived access token.
- Marked superseded tools notes as secondary to the canonical tools README.
- Updated Phase B deploy/run script defaults to the verified current Squeezebox controller `192.168.1.65` and expanded legacy SSH options.
- No firmware/runtime code changes; documentation and operational script normalization only.

---

## v0.8.5-authoritative-repo-bootstrap — 2026-04-23

- Bootstrapped authoritative repository worktree at `U:\SqueezeBox_Controller` for GitHub `oywino/SqueezeBox_Controller`.
- Imported curated Phase A and Phase B sources, control documents, RosCard artifacts, and operational scripts from the legacy `U:\` workspace.
- Excluded toolchain roots, snapshots, generated binaries, secrets, and proprietary/device-extracted binaries from normal Git history.
- Added repository operations for one-way sync, drift checks, milestone tagging, release packaging, backup bundles, and bundle-based restore.
- Recorded the operating model: repository is authoritative; legacy `U:\` is export-only.
- No firmware/runtime behavior changes; repository/bootstrap infrastructure only.

## v0.8.4-doc-sync-artifact-backed — 2026-04-23

- Synchronized PLAN, STATE, and CHANGELOG against visible workspace artifacts.
- Corrected Step 7/framebuffer wording so it no longer collides with main Step 11 (`Service call pipeline`).
- Standardized the active Step 8 criteria document path to `phase-b-ha-comm/docs/roscard_decision.md`.
- Recorded the visible Step 8 artifact trail consistently: `v0.8.1-roscard-decision-criteria-verified`, `v0.8.2-roscard-mapping-skeleton-verified`, `v0.8.3-roscard-decision-complete`.
- Declared root `CHANGELOG.md` the active changelog; `CHANGELOG_PhaseB - contract 10 steps.md` remains historical contract-enforcement context.
- No code changes; docs-only normalization.

## v0.8.3-roscard-decision-complete

- Recorded Step 8 closure artifacts in the workspace:
  - `phase-b-ha-comm/include/roscard_adapter.h`
  - `phase-b-ha-comm/ha-remote/microservices/config/config.example.json`
  - `phase-b-ha-comm/docs/roscard_mapping_table.md`
  - `phase-b-ha-comm/docs/roscard_scoring.md`
  - `phase-b-ha-comm/docs/roscard_decision.md`
- Decision recorded: adopt RosCard upstream via thin adapter, fallback to fork if criteria fail.
- Snapshot/artifact folder present: `phase-b-ha-comm/snapshots/v0.8.3-roscard-decision-complete`.

## v0.8.2-roscard-mapping-skeleton-verified

- Created skeleton mapping table for RosCard integration.
- Columns defined: Card type → RosCard element(s) → HA domain/service(s) → Required entity fields → UI actions.
- No semantics or bindings added at this step.
- Outputs: docs/roscard_mapping_table.md.
- PLAN and STATE updated to mark Step 8.2 complete.

## v0.8.1-roscard-decision-criteria-verified

- Defined audit-ready, measurable criteria for the RosCard integration decision.
- Added scoring stub and evidence placeholder for later Step 8 work.
- Outputs:
  - `phase-b-ha-comm/docs/roscard_decision.md`
  - `phase-b-ha-comm/docs/roscard_scoring.md`
  - `phase-b-ha-comm/docs/roscard_evidence/`
- No code changes; baseline remains `v0.8.0-locked-audited-closed`.

## v0.8.0-locked-audited-closed — 2025-12-18

- Formal closure snapshot: all completed Phase B work through Step 7 relocation substeps 1–20 is now locked, audited, and closed.
- Note: framebuffer split belongs to Step 7 substep 11; main Step 11 remains `Service call pipeline`.
- No new functional changes; this release serves as a checkpoint before beginning Step 8 (RosCard integration decision).
- Audit trail restated:
  - `fb.c/.h` own framebuffer lifecycle (`fb_init`, `lcd_wake`, `fb_deinit`, `fb_flush_cb`).
  - `main.c` reduced to orchestration calls only.
  - `ui.c/.h` encapsulated helpers; exported surface reduced to six orchestration functions.
  - `ha_ws.c` scope enforced: one-shot session orchestrator only.
- Verification:
  - Identical rendering, wake, input, and exit behavior compared to baseline.
  - Watchdog stable; teardown orderly; exit restores Jive cleanly.
  - Hardware validation confirmed on SB_A.
- Documentation updated:
  - PLAN: Step 7 marked complete; next pending Step 8.
  - STATE: Closure recorded at `v0.8.0-locked-audited-closed`.
  - CHANGELOG: Entry added for closure snapshot.

## v0.7.3-ui-hygiene-step-19 — 2025-12-16

- Applied UI hygiene pass:
  - Reduced header surface to six orchestration functions.
  - Internal helpers made static and encapsulated in `ui.c`.
  - Backend bridge preserved (`ui_status_set` delegates internally).
  - Verified parity: power indicator, Start/Exit buttons, exit screen, timers.
  - No visible UI changes.
- Steps 16–18 logged as no-ops (crypto, HAL, WS I/O already satisfied).

---

## v0.7.2-status-bridge-final-step-15 — 2025-12-15

- Consolidated backend status path:
  - All updates flow through `ui_status_set(...)`.
  - Backend no longer calls `ui_update_ha_status(...)` directly.
- Acceptance: no LVGL calls remain in backend; UI owns formatting.
- Verified parity on SB_A.
- Steps 12–14 logged as no-ops (input split, stock UI split, main orchestration cleanup already satisfied).

---

## v0.7.1-fb-split-step-11 — 2025-12-15

- Framebuffer split finalized:
  - `fb.c/.h` own all framebuffer state and lifecycle.
  - `main.c` reduced to orchestration calls (`fb_init`, `lcd_wake`, `fb_deinit`).
- Verified identical rendering and wake behavior on SB_A.
- Note: `fb_flush_cb` assumes RGB565 pixel format and mirrors writes to both physical buffers.

---

## v0.7.0-contract-enforcement-step-1-10 — 2025-12-15

- Completed full 10‑step contract enforcement process:
  - Crypto primitives isolated in `crypto.c/.h`.
  - Generic WS/TCP I/O in `ws_io.c/.h`.
  - `ha_ws.c` owns session lifecycle only.
  - `ui.c/.h` owns LVGL presentation, power indicator, buttons, exit screen, timer glue.
- Backend is LVGL‑free and crypto‑free; UI owns presentation glue.
- Each subsystem owns its state and teardown; exported surfaces minimal and explicit.
- Verified build/runtime on SB_A in attached and detached modes.
- Audit trail maintained: every relocation traceable, reversible, confirmed by hardware output.

---

## v0.6.2-one-shot-ha-ws_session — 2025-12-12

- Pivoted to one‑shot HA WebSocket sessions:
  - Connect → authenticate → request `get_states` → disconnect.
  - UI feedback: *connecting → auth_ok → snapshot ok → disconnected*.
- Removed continuous subscription logic and idle timers.
- Reduced battery impact; simplified logic.
- Snapshot: `v0.6.2-one-shot-ha-ws_session`.

---

## v0.6.1-ha-config-parser-verified — 2025-12-11

- Added `ha_config` module (`ha_config.h/.c`).
- Verified parsing of `config.example.json` (Light, Media Player, Climate cards).
- Wheel navigation moves focus; push/long-push log intended services.
- Exit restores Jive cleanly; HAL detach orderly.
- Snapshot: `v0.6.1-ha-config-parser-verified`.

---

## v0.6.0-plan-approved — 2025-12-10

- Inserted RosCard integration decision as Step 6.
- Renumbered subsequent steps.
- Expanded deliverables and exit criteria.
- Snapshot: `v0.6.0-plan-approved`.

---

## v0.5.4-hal-input-decoupled-verified — 2025-12-11

- HAL owns input lifecycle and polling; `input.c` consumes HAL events only.
- Device mapping fixed for SB_A (event1=Wheel, event2=Keys, event3=Accel).
- Verified parity: wheel focus, encoder push, exit restores stock UI.
- Snapshot: `v0.5.4-hal-input-decoupled-verified`.

---

## v0.5.3-hal-input-wired-verified — 2025-12-10

- Input subsystem modularized with HAL adapter.
- Verified parity with original monolithic behavior (wheel focus + encoder push).
- Snapshot: `v0.5.3-hal-input-wired-verified`.

---

## v0.5.2-hal-power-ui-wired-verified — 2025-12-10

- Introduced HAL shim for power telemetry (`hal_get_power()`).
- UI power indicator consumes HAL instead of direct `jivectl`.
- Verified parity on SB_A.
- Snapshot: `v0.5.2-hal-power-ui-wired-verified`.

---

## v0.5.1-hal-skeleton-compiled-verified — 2025-12-10

- Added `hal.c` skeleton with stubs for init/shutdown/poll/power.
- Build script updated; verified identical runtime behavior.
- Snapshot: `v0.5.1-hal-skeleton-compiled-verified`.

---

## v0.5.0-refactoring-into-modules — 2025-12-09

- Mechanical refactoring of monolithic `main.c` into modules.
- Exit screen logic transplanted to `ui_show_exit_screen()`.
- Verified build/deploy/run unchanged.
- Snapshot: `v0.5.0-refactoring-into-modules`.

---

## v0.4.0-exit-restores-jive-verified — 2025-12-07

- Exit clears LCD, shows termination message, restores Jive reliably.
- Verified AC/BAT indicator + focus outline still work.
- Snapshot: `v0.4.0-exit-restores-jive-verified`.

---

## v0.3.11-cradle-charging-indicator-verified — 2025-12-06

- Added bottom-right indicator showing AC/BAT and charging status via `jivectl`.
- Verified toggling on SB_A.
- Snapshot: `v0.3.11-cradle-charging-indicator-verified`.

---

## v0.3.10-focus-border-2px-verified — 2025-12-06

- Added clear yellow focus indicator (2px border).
- Verified on SB_A.
- Snapshot: `v0.3.10-focus-border-2px-verified`.

---

## v0.3.9-detached-runner-fix — 2025-12-05

- Added `run_sb_a_detached.sh` for stable detached runs.
- Fixed token ingestion and cleanup reliability.
- Verified detached starts stable with Jive running or stopped.
- Snapshot: `v0.3.9-detached-runner-fix`.

---

## v0.3.8-stockui-stop-hard-synced — 2025-12-05

- Synced `stockui-stop-hard.sh` to watchdog-safe variant.
- Verified identical behavior on SB_A.
- Snapshot: `v0.3.8-stockui-stop-hard-synced`.

---

## v0.3.7-deploy-run-scripts-standardized — 2025-12-05

- Standardized SB_A deploy/run scripts (`deploy_sb_a.sh`, `run_sb_a.sh`).
- Applied legacy SSH options for compatibility.
- Verified deploy/run success.
- Snapshot: `v0.3.7-deploy-run-scripts-standardized`.

---

## v0.3.6-battery-telemetry-verified — 2025-12-04

- Added battery/cradle/charging telemetry via `/dev/misc/jive_mgmt` ioctls.
- Implemented integer mapping to percent (807=0%, 875=100%).
- Verified readings on SB_A with and without stock UI.
- Snapshot: `v0.3.6-battery-telemetry-verified`.

---

## v0.3.5-gyro-idle-grace-verified — 2025-12-04

- Added gyro activity source to extend WS idle-grace deadline.
- Verified movement keeps session alive.
- Snapshot: `v0.3.5-gyro-idle-grace-verified`.

---

## v0.3.4-keypress-idle-grace-verified — 2025-12-04

- Added key-press activity source to extend WS idle-grace deadline.
- Verified button press keeps session alive.
- Snapshot: `v0.3.4-keypress-idle-grace-verified`.

---

## v0.3.3-idle-watchdog-disconnect-verified — 2025-12-04

- Implemented idle watchdog: disconnect WS on inactivity, reconnect on new activity.
- Verified reduced battery drain; reconnect resumes updates.
- Snapshot: `v0.3.3-idle-watchdog-disconnect-verified`.

---

## v0.3.2-session-window-verified — 2025-12-04

- Updated `send_state()` payload format with activity source and timestamp.
- Verified HA updates and idle reconnect.
- Snapshot: `v0.3.2-session-window-verified`.

---

## v0.3.1-activity-tracking-verified — 2025-12-04

- Added wheel and key-press activity tracking.
- Verified activity increments timestamp correctly.
- Snapshot: `v0.3.1-activity-tracking-verified`.

## <u>Begin Phase B</u>

---

## <u>End of Phase A</u>

## 

## v0.3.0

### Changes

- Added `stockui-stop-hard.sh`: keep watchdog satisfied (pidfile -> sleep) and hard-stop `jive`/`jive_alsa` to prevent LCD blanking.
- Confirmed: with stock UI hard-stopped, LVGL runs stable and responsive on fb0.
- Added on-device versioning and quickstart notes.

### Files (kept on device after cleanup)

- `lvgl-hello-armv5` — Minimal LVGL UI (buttons) on `/dev/fb0`; responsive with wheel + click when stock UI is hard-stopped.
- `stockui-stop-hard.sh` — Writes a `sleep 3600` PID into `/var/run/squeezeplay.pid` (watchdog stays happy), then SIGKILLs `jive` and `jive_alsa` to avoid LCD blanking/teardown side-effects.
- `fbpan-armv5` — Framebuffer page select (yoffset control).
- `fbtest-armv5` — Framebuffer RGB bars test.
- `README.txt` — On-device quickstart and baseline description.
- `VERSION` — Current on-device version marker.

### Removed in v0.3.0 (space cleanup; no longer needed)

- `jive-mgmt-ping-armv5` — Experimental tool to probe `/dev/misc/jive_mgmt` (write() was rejected; not part of baseline).
- `jive_mgmt_ping.log` — Output from the above probe.
- `stockui-stop-safe.sh` — Earlier “safe stop” helper (replaced by `stockui-stop-hard.sh` baseline).
- `fbblank-armv5` — Framebuffer blank/unblank test tool (not needed for baseline).
- `lvgl.log` — Runtime log file (recreated ad-hoc when needed; not kept by default).

---

## v0.2.0

### Changes

- Safe termination of the stock UI (`/usr/bin/jive`) without watchdog reboot by swapping `/var/run/squeezeplay.pid` to a long-running `sleep` before killing jive.
  - Procedure (device): save old PID from pidfile, start `sleep 3600` and write its PID to `/var/run/squeezeplay.pid`, then kill old jive PID.

### Files

- `lvgl-hello-armv5` — First LVGL “hello UI” on `/dev/fb0` with wheel/button input; required juggling the two framebuffer pages.
- `fbpan-armv5` — Utility to set fb0 yoffset (page select) via `FBIOPAN_DISPLAY`.
- `fbtest-armv5` — Utility to draw RGB test bars into `/dev/fb0` to confirm framebuffer writing works.

---

## v0.1.0

### Changes

- LVGL v8.3.11 running on `/dev/fb0` (RGB565 240x320).
- Stable with jive running: draws to both framebuffer pages (yoffset 0 and 320).
- Input: wheel + buttons via `/dev/input/event1` (Wheel) and `/dev/input/event2` (Matrix).
- EVIOCGRAB enabled to prevent jive from consuming wheel/buttons.

### Files

- `hello-musl-armv5` — Minimal static “hello world” proving we can execute an ARMv5 musl binary on-device over SSH.

---
