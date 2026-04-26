# Repository Layout

## Top Level

- `CHANGELOG.md`: active project changelog
- `PROJECT_PLAN_phaseB.md`: authoritative Phase B roadmap
- `PROJECT_STATE.md`: current project state
- `PROJECT_RULES.md`: working governance
- `docker-compose.yaml`: local Ubuntu container entrypoint
- `scripts/`: repository operations, sync, backup, and release tooling
- `docs/`: repository and historical documentation

## Source Trees

- `phase-a-lvgl-build/`
  - curated Phase A sources and helper scripts
  - excludes old snapshots and generated binaries
- `phase-b-ha-comm/`
  - current active development tree for HA communication work
  - includes `ha-remote/`, `include/`, `docs/`, and text-based tooling

## Repository-Only Documentation

- `docs/repo-layout.md`: this file
- `docs/build-environment.md`: external dependencies and build assumptions
- `docs/release-process.md`: tag, package, backup, and restore workflow
- `docs/history/`: retained historical contract and changelog context
- `docs/legacy-workspace-inventory.md`: imported inventory snapshot of the pre-repo workspace

## Not Versioned Here

- `buildroot/`
- `output/`
- `snapshots/` and release archives
- runtime tokens and logs
- device-extracted proprietary binaries

## Sync Boundary

The repository is authoritative at `\\NASF67175\Public\ubuntu\SqueezeBox_Controller`.

Legacy drive-letter workspace usage is retired for current operations. Legacy sync scripts are retained for historical recovery only and should not be part of the normal build/deploy/test workflow.
