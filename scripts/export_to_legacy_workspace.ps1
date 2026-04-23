param(
    [string]$LegacyRoot = 'U:\'
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
. (Join-Path $PSScriptRoot '_sync_map.ps1')

$map = Get-SqueezeBoxSyncMap -RepoRoot $repoRoot -LegacyRoot $LegacyRoot

foreach ($item in $map) {
    if (-not (Test-Path $item.SourceAbs)) {
        throw "Missing repository source path: $($item.SourceAbs)"
    }

    if ($item.Kind -eq 'File') {
        $parent = Split-Path -Parent $item.TargetAbs
        if (-not (Test-Path $parent)) {
            New-Item -ItemType Directory -Path $parent -Force | Out-Null
        }
        Copy-Item $item.SourceAbs $item.TargetAbs -Force
        Write-Host "FILE  $($item.SourceRel) -> $($item.TargetAbs)"
        continue
    }

    if (-not (Test-Path $item.TargetAbs)) {
        New-Item -ItemType Directory -Path $item.TargetAbs -Force | Out-Null
    }

    Get-ChildItem $item.SourceAbs -Recurse -File | ForEach-Object {
        $relative = $_.FullName.Substring($item.SourceAbs.Length).TrimStart('\')
        $targetFile = Join-Path $item.TargetAbs $relative
        $targetParent = Split-Path -Parent $targetFile
        if (-not (Test-Path $targetParent)) {
            New-Item -ItemType Directory -Path $targetParent -Force | Out-Null
        }
        Copy-Item $_.FullName $targetFile -Force
    }

    Write-Host "DIR   $($item.SourceRel) -> $($item.TargetAbs)"
}

Write-Host "Export complete. Legacy workspace refreshed from authoritative repo."
