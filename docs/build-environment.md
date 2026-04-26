# Build Environment

## Canonical Local Paths

- Repository worktree: `\\NASF67175\Public\ubuntu\SqueezeBox_Controller`
- Ubuntu container repository path: `/workspace/SqueezeBox_Controller`
- External Buildroot checkout: `/workspace/buildroot`
- External Buildroot output/toolchain: `/workspace/output`
- Legacy drive-letter workspace usage is retired for current operations

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

1. Work in `\\NASF67175\Public\ubuntu\SqueezeBox_Controller`.
2. Build from the repository worktree.
3. Use `/workspace/SqueezeBox_Controller` inside the Ubuntu container.
4. Keep milestone binaries and snapshot archives as release assets or NAS archives, not as tracked repository files.
