param(
    [string]$LegacyRoot = 'U:\',
    [switch]$ShowMatches
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
. (Join-Path $PSScriptRoot '_sync_map.ps1')

function Compare-TrackedFile {
    param(
        [string]$RepoFile,
        [string]$LegacyFile,
        [string]$RelativePath
    )

    if (-not (Test-Path $LegacyFile)) {
        return [PSCustomObject]@{ Status = 'Missing'; Path = $RelativePath }
    }

    $repoHash = (Get-FileHash $RepoFile -Algorithm SHA256).Hash
    $legacyHash = (Get-FileHash $LegacyFile -Algorithm SHA256).Hash
    if ($repoHash -ne $legacyHash) {
        return [PSCustomObject]@{ Status = 'Changed'; Path = $RelativePath }
    }

    if ($ShowMatches) {
        return [PSCustomObject]@{ Status = 'Match'; Path = $RelativePath }
    }

    return $null
}

$results = New-Object System.Collections.Generic.List[object]
$map = Get-SqueezeBoxSyncMap -RepoRoot $repoRoot -LegacyRoot $LegacyRoot

foreach ($item in $map) {
    if ($item.Kind -eq 'File') {
        $result = Compare-TrackedFile -RepoFile $item.SourceAbs -LegacyFile $item.TargetAbs -RelativePath $item.TargetRel
        if ($null -ne $result) { $results.Add($result) }
        continue
    }

    Get-ChildItem $item.SourceAbs -Recurse -File | ForEach-Object {
        $relative = $_.FullName.Substring($item.SourceAbs.Length).TrimStart('\')
        $legacyFile = Join-Path $item.TargetAbs $relative
        $result = Compare-TrackedFile -RepoFile $_.FullName -LegacyFile $legacyFile -RelativePath (Join-Path $item.TargetRel $relative)
        if ($null -ne $result) { $results.Add($result) }
    }
}

if ($results.Count -eq 0) {
    Write-Host 'No drift detected between authoritative repo and exported legacy paths.'
    exit 0
}

$results | Sort-Object Status, Path | Format-Table -AutoSize

if (($results | Where-Object { $_.Status -ne 'Match' }).Count -gt 0) {
    exit 1
}

exit 0
