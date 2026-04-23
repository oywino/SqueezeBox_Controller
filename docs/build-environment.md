# Build Environment

## Canonical Local Paths

- Repository worktree: `U:\SqueezeBox_Controller`
- Legacy workspace root: `U:\`
- External Buildroot checkout: `U:\buildroot`
- External Buildroot output/toolchain: `U:\output`

## Container Model

`docker-compose.yaml` mounts `/share/Public/ubuntu` to `/workspace` inside the Ubuntu container. Existing build scripts assume that path convention.

## Phase B Build Assumptions

`phase-b-ha-comm/ha-remote/build_modules.sh` expects:

- `/workspace/output/host/bin` on `PATH`
- the vendored `lvgl` tree under `phase-b-ha-comm/ha-remote/lvgl`
- project sources under `phase-b-ha-comm/ha-remote/microservices`

## What Stays External

The following stay outside the repo and are treated as environment prerequisites:

- full Buildroot checkout
- generated toolchain output
- device-local files and deployed binaries
- milestone snapshots and archive tarballs

## Recommended Workflow

1. Work in `U:\SqueezeBox_Controller`.
2. Build from the repository worktree.
3. Export repository files to the legacy workspace only when operational scripts still depend on `U:\`.
4. Keep milestone binaries and snapshot archives as release assets or NAS archives, not as tracked repository files.
