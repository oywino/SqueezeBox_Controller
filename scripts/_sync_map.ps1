function Get-SqueezeBoxSyncMap {
    param(
        [Parameter(Mandatory = $true)][string]$RepoRoot,
        [Parameter(Mandatory = $true)][string]$LegacyRoot
    )

    $items = @(
        @{ Kind = 'File'; Source = 'CHANGELOG.md'; Target = 'CHANGELOG.md' }
        @{ Kind = 'File'; Source = 'PROJECT_PLAN_phaseB.md'; Target = 'PROJECT_PLAN_phaseB.md' }
        @{ Kind = 'File'; Source = 'PROJECT_STATE.md'; Target = 'PROJECT_STATE.md' }
        @{ Kind = 'File'; Source = 'PROJECT_RULES.md'; Target = 'PROJECT_RULES.md' }
        @{ Kind = 'File'; Source = 'docker-compose.yaml'; Target = 'docker-compose.yaml' }
        @{ Kind = 'File'; Source = 'phase-a-lvgl-build\fbtest.c'; Target = 'phase-a-lvgl-build\fbtest.c' }
        @{ Kind = 'File'; Source = 'phase-a-lvgl-build\analysis\v0.2.x-analyze-jive-mgmt.sh'; Target = 'phase-a-lvgl-build\analysis\v0.2.x-analyze-jive-mgmt.sh' }
        @{ Kind = 'Directory'; Source = 'phase-a-lvgl-build\fbblank'; Target = 'phase-a-lvgl-build\fbblank' }
        @{ Kind = 'Directory'; Source = 'phase-a-lvgl-build\fbpan'; Target = 'phase-a-lvgl-build\fbpan' }
        @{ Kind = 'Directory'; Source = 'phase-a-lvgl-build\jive-mgmt-ping'; Target = 'phase-a-lvgl-build\jive-mgmt-ping' }
        @{ Kind = 'Directory'; Source = 'phase-a-lvgl-build\lvgl-hello'; Target = 'phase-a-lvgl-build\lvgl-hello' }
        @{ Kind = 'File'; Source = 'phase-b-ha-comm\Contract to split ha_ws.c.md'; Target = 'phase-b-ha-comm\Contract to split ha_ws.c.md' }
        @{ Kind = 'File'; Source = 'phase-b-ha-comm\stockui-stop-hard.sh'; Target = 'phase-b-ha-comm\stockui-stop-hard.sh' }
        @{ Kind = 'Directory'; Source = 'phase-b-ha-comm\docs'; Target = 'phase-b-ha-comm\docs' }
        @{ Kind = 'Directory'; Source = 'phase-b-ha-comm\include'; Target = 'phase-b-ha-comm\include' }
        @{ Kind = 'Directory'; Source = 'phase-b-ha-comm\tools'; Target = 'phase-b-ha-comm\tools' }
        @{ Kind = 'Directory'; Source = 'phase-b-ha-comm\ha-remote'; Target = 'phase-b-ha-comm\ha-remote' }
    )

    foreach ($item in $items) {
        [PSCustomObject]@{
            Kind      = $item.Kind
            SourceRel = $item.Source
            TargetRel = $item.Target
            SourceAbs = Join-Path $RepoRoot $item.Source
            TargetAbs = Join-Path $LegacyRoot $item.Target
        }
    }
}
