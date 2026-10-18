$ErrorActionPreference = 'Stop'

$sourcePath = Join-Path $PSScriptRoot '..\src\Portal\PortalBspCommands.cpp'
$source = Get-Content -Raw $sourcePath
$start = $source.IndexOf('CON_COMMAND(portal_visual_commitseed')
$end = $source.IndexOf('CON_COMMAND(portal_visual_shadowtail', $start)

if ($start -lt 0 -or $end -le $start) {
    throw 'portal_visual_commitseed command registration was not found'
}

$commandBody = $source.Substring($start, $end - $start)
$requiredRuntimeCall = 'm_PortalTransition.SetPortalVisualCommitSeedEnabled('
if (-not $commandBody.Contains($requiredRuntimeCall)) {
    throw 'portal_visual_commitseed must use the atomic transition runtime decision API'
}

if ($commandBody.Contains('PortalTransitionDecision::SetPortalVisualCommitSeedEnabled(')) {
    throw 'portal_visual_commitseed must not flip the global decision separately from runtime synchronization'
}

if ($commandBody.Contains('ResetPortalVisualCommitSeed(')) {
    throw 'portal_visual_commitseed must not reset before the runtime decision edge is synchronized'
}

Write-Output 'PortalVisualCommitSeed command registration tests passed'
