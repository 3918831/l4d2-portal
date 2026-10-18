$ErrorActionPreference = 'Stop'

$commands = Get-Content -LiteralPath 'src/Portal/PortalBspCommands.cpp' -Raw
$transition = Get-Content -LiteralPath 'src/Portal/PortalTransition.cpp' -Raw
$decision = Get-Content -LiteralPath 'src/Portal/PortalTransitionDecision.cpp' -Raw
$status = Get-Content -LiteralPath 'src/Portal/PortalBspPhase1.cpp' -Raw

function Require-Literal([string]$text, [string]$needle, [string]$message) {
    if (-not $text.Contains($needle)) {
        Write-Error $message
    }
}

function Reject-Literal([string]$text, [string]$needle, [string]$message) {
    if ($text.Contains($needle)) {
        Write-Error $message
    }
}

Require-Literal $commands 'CON_COMMAND(portal_visual_singleeyehistory,' 'single-owner A/B command is not registered'
Require-Literal $commands 'SetPortalEyeSingleOwnerEnabled(' 'single-owner command does not route through the atomic transition edge'
Require-Literal $transition '[PortalEyeSingleOwner]' 'single-owner runtime lifecycle evidence is missing'
Require-Literal $decision 'g_portalEyeSingleOwnerEnabled = false;' 'single-owner factor must default off'
Require-Literal $status 'singleEyeHistory=' 'phase status does not expose the active eye owner'

Reject-Literal $commands 'CON_COMMAND(portal_visual_convergenttail,' 'the disproved convergent-tail product command must be removed'
Reject-Literal $decision 'g_portalEyeConvergentTailEnabled' 'the disproved convergent-tail decision state must be removed'

Write-Output 'Portal single-eye-history command registration tests passed'
