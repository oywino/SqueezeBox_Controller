# Release, Backup, And Restore

## Milestone Tagging

Use annotated tags that match the project snapshot/version language, for example:

- `v0.8.5-authoritative-repo-bootstrap`
- `v0.9.0-step9-llat-verified`

Create tags with:

```powershell
powershell -File .\scripts\create_milestone_tag.ps1 -Tag v0.9.0-step9-llat-verified
```

## Release Packaging

Package a release staging folder with:

```powershell
powershell -File .\scripts\package_release.ps1 -Tag v0.9.0-step9-llat-verified
```

The script creates:

- a source archive from git
- a metadata file
- optional copied binaries if present
- `SHA256SUMS.txt`

## Backup

Create an offline backup bundle with:

```powershell
powershell -File .\scripts\create_backup_bundle.ps1
```

This produces a `git bundle` file that can recreate the repository with tags and history.

## Restore

Restore from a bundle with:

```powershell
powershell -File .\scripts\restore_from_bundle.ps1 -BundlePath .\artifacts\backups\repo.bundle -TargetDir .\restore-test
```

Optionally check out a milestone tag:

```powershell
powershell -File .\scripts\restore_from_bundle.ps1 -BundlePath .\artifacts\backups\repo.bundle -TargetDir .\restore-test -Ref v0.9.0-step9-llat-verified
```

## GitHub Release Flow

1. Verify code and hardware acceptance.
2. Update `PROJECT_PLAN_phaseB.md`, `PROJECT_STATE.md`, and `CHANGELOG.md`.
3. Commit to `main`.
4. Create annotated milestone tag.
5. Package release assets.
6. Push commit and tag.
7. Publish a GitHub Release and attach milestone binaries, hashes, and snapshot/archive assets.
