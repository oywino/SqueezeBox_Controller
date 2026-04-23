param(
    [Parameter(Mandatory = $true)][string]$BundlePath,
    [Parameter(Mandatory = $true)][string]$TargetDir,
    [string]$Ref
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path $BundlePath)) {
    throw "Bundle not found: $BundlePath"
}

if (Test-Path $TargetDir) {
    $existing = Get-ChildItem -Force $TargetDir -ErrorAction SilentlyContinue
    if ($existing) {
        throw "Target directory is not empty: $TargetDir"
    }
} else {
    New-Item -ItemType Directory -Path $TargetDir -Force | Out-Null
}

Remove-Item $TargetDir -Force -Recurse
git clone $BundlePath $TargetDir

if ($Ref) {
    git -C $TargetDir checkout $Ref
}

Write-Host "Repository restored to $TargetDir"
