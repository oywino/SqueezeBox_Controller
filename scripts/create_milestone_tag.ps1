param(
    [Parameter(Mandatory = $true)][string]$Tag,
    [string]$Message,
    [switch]$Push
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

if (-not $Message) {
    $Message = $Tag
}

$status = git -C $repoRoot status --porcelain
if ($status) {
    throw 'Working tree is not clean. Commit changes before creating a milestone tag.'
}

$existing = git -C $repoRoot tag --list $Tag
if ($existing) {
    throw "Tag already exists: $Tag"
}

git -C $repoRoot tag -a $Tag -m $Message
Write-Host "Created annotated tag $Tag"

if ($Push) {
    git -C $repoRoot push origin $Tag
    Write-Host "Pushed tag $Tag"
}
