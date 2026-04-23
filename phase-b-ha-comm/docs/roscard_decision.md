# RosCard Integration Decision — Criteria (Step 8.1)

## Purpose

Define audit-ready, measurable criteria to decide whether to **adopt** RosCard upstream with a thin adapter or to **fork** a device-oriented subset. No code changes in Step 8.1.

## Scope

- Target device: Logitech Squeezebox Controller (ARM9/S3C2412, 200 MHz, 64 MB RAM, 240×320 RGB565).
- Affects: adapter boundary only; no UI, `ha_ws.c`, or config schema changes at this step.
- Baseline locked at `v0.8.0-locked-audited-closed`.

## Decision Options

- **Adopt** RosCard via a thin adapter (preferred path to evaluate first).
- **Fork** RosCard into a minimal, device-oriented subset (fallback if criteria fail).

## Evaluation Method

- Score each criterion 0–5 (0 = fails materially; 3 = acceptable; 5 = ideal).
- Record raw evidence (artifacts, diffs, measurements) per criterion.
- A decision is **Adopt** if all Mandatory criteria ≥3 and Weighted total ≥75/100.
- Otherwise, **Fork**.

### Weights

- Mandatory criteria: must meet threshold (weights still applied).
- Optional criteria: contribute to total only.

| Criterion Group                    | Weight  | Mandatory |
| ---------------------------------- | -------:|:---------:|
| A. Runtime Footprint & Performance | 25      | Yes       |
| B. Schema & Config Alignment       | 15      | Yes       |
| C. Input Model Fit (wheel/buttons) | 10      | Yes       |
| D. Offline/Degraded Behavior       | 10      | Yes       |
| E. Maintenance & Update Cadence    | 10      | Yes       |
| F. Licensing & Attribution         | 5       | Yes       |
| G. Testability & Reproducibility   | 10      | No        |
| H. Failure Modes & Error Handling  | 10      | No        |
| **Total**                          | **100** |           |

---

## Criteria Details

### A. Runtime Footprint & Performance (25, Mandatory)

- **A1. Binary/code size impact:** Adapter + minimal RosCard representation fits within existing partitioning without evicting required modules. Evidence: size diffs.
- **A2. RAM usage:** Peak additional RAM ≤ 4 MB at render/update spikes; steady-state ≤ 2 MB. Evidence: `/proc/meminfo`, allocator stats.
- **A3. CPU load:** Mean additional CPU ≤ 10% at 200 MHz during burst updates; frame flush stays ≤ 33 ms (≤ 30 FPS worst-case), idle returns to baseline. Evidence: perf samples.
- **A4. Latency budget:** Entity state→UI visible ≤ 250 ms median, ≤ 500 ms p95 under typical HA event rates. Evidence: timestamped event→render traces.

### B. Schema & Config Alignment (15, Mandatory)

- **B1. No schema drift:** RosCard JSON maps 1:1 to device card structs via adapter; no mutations to upstream field semantics.
- **B2. Compatibility envelope:** Supports minimum card set (Light, Media, Climate, Fan, Scene, Cover, Switch, TV/media_source, Weather, Host) without upstream changes.
- **B3. Back-compat strategy:** Future upstream additions do not break adapter contract; unknown fields ignored safely.

### C. Input Model Fit (10, Mandatory)

- **C1. Control mapping:** Wheel/press/long-press actions map deterministically to RosCard interactions for target cards.
- **C2. Accessibility:** Focus movement and action activation require ≤ 3 user steps for common tasks (toggle, set volume/brightness/temp).
- **C3. No LVGL contract breach:** Adapter does not require changes to LVGL ownership or `fb.c` lifecycle.

### D. Offline/Degraded Behavior (10, Mandatory)

- **D1. Cache tolerance:** UI remains stable with last-known states when HA is unreachable; no hard-fail UI paths.
- **D2. Rate limiting:** Adapter can cooperate with Step 10 limits; no burst amplification on reconnect.
- **D3. Partial payloads:** Missing/slow entities do not block rendering of others.

### E. Maintenance & Update Cadence (10, Mandatory)

- **E1. Upstream velocity:** Update cadence manageable; adapter insulation keeps local changes ≤ 200 LoC per upstream minor rev.
- **E2. Surface area:** Public adapter API remains ≤ 10 symbols; no cross-module bleed.

### F. Licensing & Attribution (5, Mandatory)

- **F1. License compatibility:** Upstream license compatible with our distribution.
- **F2. Attribution sufficiency:** Single source attribution in NOTICE/README is sufficient; no additional obligations that impact device UX.

### G. Testability & Reproducibility (10, Optional)

- **G1. Determinism:** Same inputs → same rendered state; timestamp/nonce handling isolated.
- **G2. Fixture coverage:** JSON fixtures for each supported card type render on device simulator and hardware identically.

### H. Failure Modes & Error Handling (10, Optional)

- **H1. Bounded errors:** Malformed/unknown fields downgrade gracefully; adapter returns structured errors without side effects.
- **H2. Recovery paths:** Clear recovery from HA disconnects, stale tokens, and schema extensions.

---

## Measurement Protocol

1. **Fixtures:** Curate one RosCard JSON fixture per minimum card type; include edge cases (missing optional fields, unknown fields).
2. **Replay:** Feed fixtures through adapter (simulated first, then hardware) capturing:
   - memory peak/steady, CPU %, frame time, event→render latency.
3. **Stress:** 60 s burst of mixed updates at 10 Hz per active card; measure p95/p99 latencies and RAM peaks.
4. **Offline:** Cut HA link; verify cached rendering and input handling; reconnect and verify no thundering herd.
5. **Audit Pack:** Store raw logs, diffs, and measurements with timestamps; link from CHANGELOG entry.

---

## Evidence Recording Template

```
Criterion: A2 RAM usage
Score: 4/5
Evidence: memstat_2025-12-18T12-10Z.txt (peak +1.6MB, steady +0.9MB)
Notes: Within budget. No allocator warnings.
```

---

## Decision Gate

- **Adopt** if all Mandatory criteria ≥3 and total ≥75/100.
- **Fork** if any Mandatory <3 or total <75/100.
- Record decision, rationale, and evidence references; update PLAN/STATE/CHANGELOG per governance.

## Non-Goals (Step 8.1)

- No adapter header or implementation.
- No mapping table or config schema edits.
- No UI or `ha_ws.c` changes.

## Outputs (Step 8.1)

- This criteria document committed under `docs/roscard_decision.md`.
- Empty evidence directory placeholder: `docs/roscard_evidence/`. Empty directory created in repo; populated in Step 8.2+.
- Scoring sheet stub: `docs/roscard_scoring.md` (table only, filled later).
- PLAN and STATE updated to mark Step 8.1 criteria defined.

## Acceptance Checklist (Step 8.1)

- [x] Document exists with criteria, weights, thresholds, and protocol.
- [x] No code changes introduced.
- [x] PLAN/STATE note “Step 8.1 criteria defined.”

---

CHANGELOG: `v0.8.1-roscard-decision-criteria-verified`

- Defined audit-ready criteria for RosCard integration.
- No code changes; baseline remains `v0.8.0`.
- Outputs: `docs/roscard_decision.md`, evidence dir placeholder, scoring sheet stub.
