$ErrorActionPreference = 'Stop'

$sourcePath = Join-Path $PSScriptRoot '..\src\Portal\PortalBspCommands.cpp'
$source = Get-Content -Raw $sourcePath
$start = $source.IndexOf('CON_COMMAND(portal_visual_crossleafhandoff')
$end = $source.IndexOf('CON_COMMAND(', $start + 1)

if ($start -lt 0 -or $end -le $start) {
    throw 'portal_visual_crossleafhandoff command registration was not found'
}

$commandBody = $source.Substring($start, $end - $start)
$requiredFragments = @(
    'portal_visual_crossleafhandoff <0|1>',
    'PortalTransitionDecision::GetPortalEyeCrossLeafHandoffEnabled()',
    'PortalTransitionDecision::SetPortalEyeCrossLeafHandoffEnabled(enabled)',
    'PortalBspPhase1::LogStatus('
)

foreach ($fragment in $requiredFragments) {
    if (-not $commandBody.Contains($fragment)) {
        throw "portal_visual_crossleafhandoff runtime surface is missing: $fragment"
    }
}

Write-Output 'Portal safe cross-leaf handoff command registration tests passed'
