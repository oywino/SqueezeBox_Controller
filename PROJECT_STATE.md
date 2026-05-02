# PROJECT_STATE.md

Purpose: single source of truth for **current** project status (what exists, what works, what’s next).  
Do not duplicate detailed procedures from PROJECT_RULES.md or long history from CHANGELOG.md.

---

## Trail control phrases

Use these exact markers in chat:

- **TRAIL OFF: <topic>** — temporarily leave the main track for a focused sidetrack.
- **TRAIL ON: main track** — return to the main track.

Rules:

- While TRAIL OFF, only work on that sidetrack goal.
- When TRAIL ON, stop discussing the sidetrack unless it blocks the main track.
- After a TRAIL ON that closes a milestone, update this file with the outcome.

---

## Hardware / environment

- Device: Logitech Squeezebox Controller (ARMv5, Linux + BusyBox).
- Tooling constraints: no `readlink`, `nl`, `chvt`, `strace`, `ftrace`.
- Stock UI process: `/usr/bin/jive` (started by `/etc/init.d/squeezeplay`).

---

## Contract defined

- `ha_ws.c` responsibility limited to one‑shot session orchestration (see PROJECT_RULES.md).

---

## Current status (artifact-synced 2026-04-27)

- LVGL apps run on `/dev/fb0` and read input events.
- Active binaries live under `/mnt/storage/phase-a-lvgl/`.
- Hardware-verified runtime closure is recorded at `v0.8.0-locked-audited-closed`.
- Step 8 document/adapter artifacts are recorded through `v0.8.3-roscard-decision-complete`.
- Phase B MVP target is approved: prove `Squeezebox UI -> HA live state -> user action -> HA service call -> visible result`.
- Phase B MVP card set is Light, Cover, Switch, and Media Player. Climate is deferred.
- `phase-b-ha-comm/ha-remote/microservices/config/config.example.json` now matches the current parser fields for the MVP card set.
- Canonical active changelog: `CHANGELOG.md`. `CHANGELOG_PhaseB - contract 10 steps.md` is retained as historical contract-enforcement context.
- Authoritative repository/worktree established at `\\NASF67175\Public\ubuntu\SqueezeBox_Controller`, backed by GitHub `oywino/SqueezeBox_Controller`.
- Ubuntu container path is `/workspace/SqueezeBox_Controller`.
- Legacy drive-letter workspace usage is retired for current operations.
- Runtime/deploy procedure for current Squeezebox `192.168.1.65` is documented in `phase-b-ha-comm/tools/README.md`.
- Current Phase B runtime binary is `ha-squeeze-remote-armv5`.
- The last verified test-phase binary `ha-remote-armv5` remains preserved and untouched.
- First Phase B UI shell is hardware-verified: screen section outline, AC/BAT cradle indicator, short Home menu, long Home emergency Exit with Jive restore.
- Reusable input-layer long-press binding is in place for pressable keypad keys.
- Sleep behavior is hardware-verified: BAT sleeps after about 30 seconds idle, AC stays awake past 30 seconds, and key/wheel/accelerometer activity wakes the screen.
- Status bar now uses extracted Jive WiFi and battery/AC PNG assets plus embedded Jive FreeSans/FreeSansBold font assets.
- Approved graphical details are version-bound: reverting to a tag/release must restore the exact approved graphics for that version unless explicitly changed.
- WiFi connected-state detection is owned by HAL via `hal_get_wifi()`; UI only renders the approved icon for the reported state.
- Power/WiFi polling is cached by `status_cache` so blocking shell calls do not run in the UI/input path.
- Home short action now fires on key-down; long Home still triggers emergency Exit. Current responsiveness is accepted for now, but still not Jive-level and should be revisited later.
- Menu responsiveness issue was measured and corrected at `v0.8.12-phase-b-responsive-menu-audio`: the forced full-screen redraw timer was removed, input polling now runs in a dedicated thread, and menu/button audio feedback is isolated in `audio_feedback`. User verified menu response as near perfect and sound as stable.
- Audio feedback now runs at 22.05 kHz stereo S16_LE and excess wheel input is coalesced to the latest direction at `v0.8.13-phase-b-audio-rate-wheel-coalesce`; user verified broken slow-scroll beeps are gone.
- REST state fetch for the configured MVP entities is hardware-verified at `v0.8.14-phase-b-rest-states-sleep`; startup logs show current state for `light.sov_2_tak`, `cover.screen_sov_2`, `switch.ikea_power_plug`, and `media_player.squeezebox_boom`.
- Sleep/backlight behavior is hardware-verified at `v0.8.14-phase-b-rest-states-sleep`: LCD/backlight turns fully off, key press wakes it, shake wakes it, and Jive does not appear mixed with the HA UI.
- First HA service-call action is hardware-verified at `v0.8.15-phase-b-switch-action`: rotary/select push toggles `switch.ikea_power_plug`, and the runtime log shows the service call plus refreshed switch state.
- Card focus/navigation is implemented at `v0.8.16-phase-b-card-focus-select-guard`: three cards are visible, wheel focus moves within the visible set before scrolling one card at the edge, no wrap is used, and rotary/select only toggles `switch.ikea_power_plug` when the Switch card is focused.
- Card focus scroll sensitivity was reduced at `v0.8.17-phase-b-card-scroll-guard` by increasing the card-only scroll guard to 250 ms.
- Switch card live-state rendering is hardware-verified at `v0.8.18-phase-b-switch-card-live-state`: the approved no-icon switch layout is used, and HA WebSocket `state_changed` events update the REST state cache and card toggle indicator.
- Light card visual/live-state/action behavior is hardware-verified at `v0.8.19-phase-b-light-card-action`: the approved toggle layout is used for `Sov 2 Tak`, HA pushed state changes update the card, and rotary/select calls `light.toggle` when focused.
- Cover card visual/action behavior is hardware-verified at `v0.8.20-phase-b-cover-card-action`: `<<` opens, `||` stops, `>>` closes, and the manually drawn triangle animation follows `current_position` until `100`/`0` or Stop.
- Cover animation visual tuning is hardware-approved at `v0.8.21-phase-b-cover-animation-visual-tune`: Stop is enlarged and active animation color is bright white.
- Media Player card artwork is hardware-approved at `v0.8.22-phase-b-media-card-artwork`: idle media shows `Squeezebox Boom` / `Nothing`, loaded media expands into the approved Jive-style now-playing layout when focused, and album art is fetched/decoded off the UI thread into a 240x204 RGB565 surface.
- `build_incremental.sh` is available for future ARMv5 builds so unchanged modules are not recompiled for small source changes.
- Verified Home Assistant base URL for current runtime/config is `http://192.168.1.8:8123`; startup REST state fetch logs `4/4 ok`.

---

## Phase B progress (main steps)

- [x] **Step 1. Freeze Phase A baseline** — snapshot frozen.
- [x] **Step 2. Runtime app skeleton** — build/deploy/run scripts verified.
- [x] **Step 2b. UI cleanup (focus + cradle indicator)** — snapshots `v0.3.10`, `v0.3.11`.
- [x] **Step 3. Launch/exit wrapper** — exit restores Jive (`v0.4.0`).
- [x] **Step 4. Refactor into microservices** — exit screen logic transplanted (`v0.5.0`).
- [x] **Step 5. Device HAL abstraction** — input decoupled (`v0.5.4`).
- [x] **Step 6. One‑shot HA session** — lifecycle verified (`v0.6.2`).

### Step 7. Contract enforcement continuation (relocation substeps 1–20)

- Substeps 1–10 completed at `v0.7.0`.

- Later Step 7 closure is represented in visible workspace artifacts `v0.7.1-fb-split-step-11`, `v0.7.2-status-bridge-final-step-15`, `v0.7.3-ui-hygiene-step-19`, with closure checkpoint `v0.8.0-locked-audited-closed`.

- Includes framebuffer split, input split (no‑op), stock UI split (no‑op), main orchestration cleanup (no‑op), status bridge, hygiene passes, UI audit, hardware validation.

- [x] **Step 8. RosCard integration decision (HACS)**
  
  - Criteria defined and weighted (docs/roscard_decision.md).
  - Scoring stub prepared (docs/roscard_scoring.md).
  - Evidence dir placeholder created (docs/roscard_evidence/).
  - Adapter header defined (include/roscard_adapter.h).
  - Mapping table skeleton created (docs/roscard_mapping_table.md).
  - Config skeleton drafted (ha-remote/microservices/config/config.example.json).
  - Decision recorded: Adopt RosCard upstream via thin adapter, fallback to fork if criteria fail.
  - Artifact trail present: `v0.8.1-roscard-decision-criteria-verified`, `v0.8.2-roscard-mapping-skeleton-verified`, `v0.8.3-roscard-decision-complete`.

- [ ] **Step 9. HA connection layer (REST state fetch + WebSocket service calls)** — REST state fetch is hardware-verified for the configured MVP entities. WebSocket service calls are still pending. Normal MVP state fetch should use REST `GET /api/states/<entity_id>` for configured entities only, not WebSocket `get_states`.

  MVP entity IDs: `light.sov_2_tak`, `cover.screen_sov_2`, `switch.ikea_power_plug`, `media_player.squeezebox_boom`.

- [ ] **Step 10. State cache + rate limiting** — pending.

- [ ] **Step 11. Service call pipeline** — pending.
  
  First action verified: `switch.toggle` for `switch.ikea_power_plug` on rotary/select push. Remaining MVP actions are pending.

- [ ] **Step 12. Minimal demo UI (PoC)** — pending.

- [ ] **Step 13. Configuration loading** — pending.

- [ ] **Step 14. Metrics + debug** — pending.

- [ ] **Step 15. Package + deploy documentation** — pending.

- [ ] **Step 16. Phase B deliverable** — pending.

- [x] 

---

## Milestones (snapshots)

- v0.3.10 — Focus border (2px yellow).
- v0.3.11 — Cradle/charging indicator.
- v0.4.0 — Exit restores Jive.
- v0.5.0 — Refactoring into modules.
- v0.5.4 — HAL input decoupled.
- v0.6.1 — ha_config parser verified.
- v0.6.2 — One‑shot HA WS session.
- v0.7.0 — Contract enforcement complete.
- v0.7.1 — Framebuffer split.
- v0.7.2 — Status bridge final.
- v0.7.3 — UI hygiene pass.
- v0.8.0 — Locked, audited, closed Step 7 checkpoint.
- v0.8.1 — RosCard decision criteria defined.
- v0.8.2 — RosCard mapping skeleton created.
- v0.8.3 — RosCard decision artifacts completed.
- v0.8.7 — Phase B MVP config aligned to Light, Cover, Switch, Media Player.
- v0.8.8 — Phase B UI shell, reusable long-press, and sleep behavior verified.
- v0.8.12 — Responsive menu/audio correction verified.
- v0.8.13 — Audio rate reduction and wheel coalescing verified.

---

## STATE entry
