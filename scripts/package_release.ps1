param(
    [Parameter(Mandatory = $true)][string]$Tag,
    [string]$OutputRoot,
    [string]$BinaryRoot
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

if (-not $OutputRoot) {
    $OutputRoot = Join-Path $repoRoot 'artifacts\releases'
}

if (-not $BinaryRoot) {
    $BinaryRoot = $repoRoot
}

$tagRef = git -C $repoRoot tag --list $Tag
if (-not $tagRef) {
    throw "Tag not found: $Tag"
}

$releaseDir = Join-Path $OutputRoot $Tag
if (-not (Test-Path $releaseDir)) {
    New-Item -ItemType Directory -Path $releaseDir -Force | Out-Null
}

$sourceZip = Join-Path $releaseDir "$Tag-source.zip"
git -C $repoRoot archive --format=zip --output=$sourceZip $Tag

$metaPath = Join-Path $releaseDir 'release-metadata.txt'
$commit = git -C $repoRoot rev-list -n 1 $Tag
@(
    "Tag: $Tag"
    "Commit: $commit"
    "CreatedUtc: $([DateTime]::UtcNow.ToString('s'))Z"
    "RepoRoot: $repoRoot"
    "BinaryRoot: $BinaryRoot"
) | Set-Content -Path $metaPath

$optionalFiles = @(
    'phase-b-ha-comm\ha-remote\ha-remote-armv5'
    'phase-b-ha-comm\stockui-stop-hard.sh'
    'phase-a-lvgl-build\lvgl-hello\lvgl-hello-armv5'
)

$binDir = Join-Path $releaseDir 'binaries'
New-Item -ItemType Directory -Path $binDir -Force | Out-Null

foreach ($relative in $optionalFiles) {
    $candidate = Join-Path $BinaryRoot $relative
    if (Test-Path $candidate) {
        $target = Join-Path $binDir ([IO.Path]::GetFileName($candidate))
        Copy-Item $candidate $target -Force
    }
}

$hashFile = Join-Path $releaseDir 'SHA256SUMS.txt'
$lines = Get-ChildItem $releaseDir -Recurse -File | Where-Object { $_.FullName -ne $hashFile } | Sort-Object FullName | ForEach-Object {
    $hash = (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    $relative = $_.FullName.Substring($releaseDir.Length).TrimStart('\')
    "$hash  $relative"
}
$lines | Set-Content -Path $hashFile

Write-Host "Release package staged at $releaseDir"
