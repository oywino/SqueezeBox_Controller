param(
    [string]$OutputRoot,
    [string]$Name
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

if (-not $OutputRoot) {
    $OutputRoot = Join-Path $repoRoot 'artifacts\backups'
}

if (-not (Test-Path $OutputRoot)) {
    New-Item -ItemType Directory -Path $OutputRoot -Force | Out-Null
}

if (-not $Name) {
    $Name = 'SqueezeBox_Controller-' + (Get-Date -Format 'yyyyMMdd-HHmmss')
}

$bundlePath = Join-Path $OutputRoot ($Name + '.bundle')
git -C $repoRoot bundle create $bundlePath --all --tags

$metaPath = Join-Path $OutputRoot ($Name + '.txt')
@(
    "Bundle: $bundlePath"
    "Head: $(git -C $repoRoot rev-parse HEAD)"
    "CreatedUtc: $([DateTime]::UtcNow.ToString('s'))Z"
    "Remote: $(git -C $repoRoot remote get-url origin)"
) | Set-Content -Path $metaPath

Write-Host "Backup bundle created: $bundlePath"
