# SqueezeBox Controller

Authoritative source repository for the HA Squeezebox Controller project based on Logitech Jive hardware.

Current release: `v0.9.0` — UX responsiveness milestone for the Phase B HA remote runtime.

## Working Model

- GitHub repository: [oywino/SqueezeBox_Controller](https://github.com/oywino/SqueezeBox_Controller)
- Authoritative local working tree: `\\NASF67175\Public\ubuntu\SqueezeBox_Controller`
- Ubuntu container path: `/workspace/SqueezeBox_Controller`
- Legacy drive-letter workspace usage is retired for current operations

The repository contains source, scripts, and control documents required to continue development. Toolchain outputs, snapshots, milestone archives, and secrets are kept out of normal Git history.

## Included

- Phase B control documents: `PROJECT_PLAN_phaseB.md`, `PROJECT_STATE.md`, `PROJECT_RULES.md`, `CHANGELOG.md`
- Curated Phase A and Phase B source trees
- Build, deploy, and run scripts
- RosCard decision artifacts
- Repository operations scripts under `scripts/`
- Release control documents: `CHANGELOG.md`, `PROJECT_PLAN_phaseB.md`, `PROJECT_RULES.md`, `PROJECT_STATE.md`, and this `README.md`

## Excluded From Git History

- `buildroot/` and `output/`
- `snapshots/` trees
- generated binaries and logs
- device tokens and other secrets
- proprietary/device-extracted binaries from exploratory analysis

## Common Operations

```powershell
# Create an annotated milestone tag
powershell -File .\scripts\create_milestone_tag.ps1 -Tag v0.8.5-authoritative-repo-bootstrap

# Package a release bundle
powershell -File .\scripts\package_release.ps1 -Tag v0.8.5-authoritative-repo-bootstrap

# Create an offline git bundle backup
powershell -File .\scripts\create_backup_bundle.ps1
```

## Build Notes

- Container entrypoint is defined in `docker-compose.yaml`.
- Current Phase B build expects `/workspace/output/host/bin` to provide the Buildroot toolchain.
- `/workspace/buildroot` and `/workspace/output` remain external dependencies and are not mirrored into this repository.

See `docs/repo-layout.md`, `docs/build-environment.md`, and `docs/release-process.md` for the operating model.
