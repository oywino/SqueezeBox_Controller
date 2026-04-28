# Phase B (Part 1) — Home Assistant Communication (Project Plan)

## Baseline (Phase A complete)

- Target device: Logitech Squeezebox Controller (JIVE), Linux 2.6.22 armv5tejl
- Display: `/dev/fb0` 240x320 RGB565 (yres_virtual 640, line_len 480)
- Input: wheel + encoder push + matrix keys captured by our app
- We can stop Jive temporarily and take full control (watchdog considerations apply)
- Communication access: SSH only; power-cycle reverts to stock system
- Build env: QNAP → Ubuntu container; Buildroot toolchain at `/workspace/output/host/bin`, work at `/workspace/buildroot`

## Repository Control

- Authoritative repository: GitHub `oywino/SqueezeBox_Controller`
- Authoritative local worktree: `\\NASF67175\Public\ubuntu\SqueezeBox_Controller`
- Ubuntu container path: `/workspace/SqueezeBox_Controller`
- Legacy drive-letter workspace usage is retired for current operations
- External Buildroot roots (`/workspace/buildroot`, `/workspace/output`) remain environment dependencies and are not tracked in this repo

## Phase B objective

Prove a stable end-to-end control loop:  
**Device UI ↔ Home Assistant live states ↔ Home Assistant service calls**  
without watchdog-triggered reboots and with a clean restore path to stock UI.

## Phase B MVP design target

Prove one narrow remote-control loop:

```text
Squeezebox UI -> HA live state -> user action -> HA service call -> visible result
```

The Squeezebox should behave as a small dedicated HA remote, not a dashboard browser and not a full Astrion/Unfolded Circle clone.

MVP card set:

- Light
- Cover
- Switch
- Media Player

Climate is deferred. Emergency Exit remains available as a recovery path, not as normal navigation.

Verified Phase B UI/runtime checkpoint:

- Runtime binary renamed to `ha-squeeze-remote-armv5`.
- Test-phase `ha-remote-armv5` is preserved as the last verified communication-test binary.
- UI shell has screen sections, AC/BAT cradle indicator, Home menu placeholder, and long-Home emergency Exit.
- Input layer supports reusable short/long press bindings for keypad keys.
- Sleep behavior is active: BAT sleeps after about 30 seconds idle; AC stays awake past 30 seconds; key/wheel/accelerometer activity wakes the screen.
- Menu input/audio responsiveness is hardware-verified at `v0.8.12-phase-b-responsive-menu-audio`: input polling is threaded, menu wheel selection works without the forced redraw bottleneck, and feedback sound is stable.

## Exit criteria (Phase B done)

- Reliable start/stop that does not trigger the watchdog
- Connects to HA over LAN via WebSocket API using LLAT auth
- Receives state updates and maintains a local cache
- Can call services and reflect success/failure on-screen
- LCD shows live HA states for at least three cards; user actions invoke HA services with visible success/failure feedback; clean Exit restores Jive
- Logs allow troubleshooting without guesswork

---

## Phase B outline (planned steps)

- [x] **Step 1. Freeze Phase A baseline**  
  Tag `/mnt/storage/phase-a-lvgl` as known-good baseline. Snapshot + changelog entry.

- [x] **Step 2. Create the runtime app skeleton**  
  One binary owns main loop, LVGL task handler, input dispatch, timers. Build/deploy/run scripts created.

- [x] **Step 2b. Temporary UI cleanup + focus + cradle indicator**  
  Yellow focus outline; compact AC/BAT + charging indicator.  
  Snapshots: `v0.3.10-focus-border-2px-verified`, `v0.3.11-cradle-charging-indicator-verified`.

- [x] **Step 3. Launch/exit wrapper: stop stock UI + restore on Exit**  
  Exit clears LCD, shows termination message, restarts Jive via pidfile logic.  
  Snapshot: `v0.4.0-exit-restores-jive-verified`.

- [x] **Step 4. Refactor monolithic main.c into microservices**  
  Mechanical split into `microservices/` with no behavior change.  
  Snapshot: `v0.5.0-refactoring-into-modules`.

- [x] **Step 5. Device HAL (hardware abstraction)**  
  HAL owns input lifecycle and polling; power shim via `jivectl`.  
  Snapshot: `v0.5.4-hal-input-decoupled-verified`.

- [x] **Step 6. Start HA session (one-shot model)**  
  Connect → authenticate → fetch states → disconnect; short-lived sessions on demand.  
  Snapshot: `v0.6.2-one-shot-ha-ws_session`.

---

### Step 7. Contract enforcement continuation (relocation substeps 1–20)

**Purpose:** Close all pending items from the relocation plan, one at a time, with snapshots and hardlink-aware workflow.  
**Status:** Substeps 1–10 completed at `v0.7.0`; visible workspace artifacts for later Step 7 closure are `v0.7.1-fb-split-step-11`, `v0.7.2-status-bridge-final-step-15`, `v0.7.3-ui-hygiene-step-19`, with closure/audit checkpoint at `v0.8.0-locked-audited-closed`.

- Substeps 1–10 (Contract enforcement): crypto separation, ws_io split, backend lifecycle isolation, UI subsystem creation, timer relocation, exit screen restoration, poll timer bridge, status update bridge, WS receive/I/O relocation.
- Substeps 11–20 (Split and hygiene): framebuffer split, input split (no-op), stock UI utilities split (no-op), main orchestration cleanup (no-op), status path consolidation, WS I/O hygiene (no-op), crypto hygiene (no-op), docs sync (no-op), UI hygiene pass, hardware validation.

---

- [x] **Step 8. RosCard integration decision (HACS)**  
  
  - Decision criteria defined (Step 8.1).
  - Mapping table skeleton created (Step 8.2).
  - Minimal config example drafted (Step 8.3).
  - Decision recorded: Adopt RosCard upstream via thin adapter, fallback to fork if criteria fail (Step 8.4).
  - Artifact trail: `v0.8.1-roscard-decision-criteria-verified`, `v0.8.2-roscard-mapping-skeleton-verified`, `v0.8.3-roscard-decision-complete`.
  - Outputs: `phase-b-ha-comm/docs/roscard_decision.md`, `phase-b-ha-comm/docs/roscard_scoring.md`, `phase-b-ha-comm/docs/roscard_mapping_table.md`, `phase-b-ha-comm/ha-remote/microservices/config/config.example.json`, `phase-b-ha-comm/include/roscard_adapter.h`, evidence dir placeholder.

- [ ] **Step 9. HA connection layer (WebSocket + LLAT)**  
  LLAT connect with banner; auth failure handling; fetch initial states for configured entities; activity-fed keepalive.

- [ ] **Step 10. State cache + rate limiting**  
  Cache only configured entities; apply event patches; redraw only changed regions; enforce per-domain rate caps.

- [ ] **Step 11. Service call pipeline**  
  Wire demo card actions to concrete HA services; success/error feedback; basic retries.

- [ ] **Step 12. Minimal demo UI (“one screen per card type” PoC)**  
  Start with Light, Cover, Switch, and Media Player; wheel navigation; push/long-push actions; verify end-to-end loop.
  First shell verified at `v0.8.8-phase-b-ui-shell-sleep`; responsive menu/audio behavior verified at `v0.8.12-phase-b-responsive-menu-audio`; card population remains pending.

- [ ] **Step 13. Configuration loading**  
  Load small config for entity IDs + layout; allow reload on startup.

- [ ] **Step 14. Metrics + debug**  
  Optional overlay toggle (FPS, WS state, last activity); log buffer accessible via SSH.

- [ ] **Step 15. Package + deploy documentation**  
  Exact build/deploy/run steps; known failure modes and recovery.

- [ ] **Step 16. Phase B deliverable**  
  Versioned release demonstrating: device UI ↔ HA live states ↔ HA service calls with stable watchdog behavior.
