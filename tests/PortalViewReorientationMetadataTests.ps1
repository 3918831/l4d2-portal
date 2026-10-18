$ErrorActionPreference = 'Stop'

$transitionPath = Join-Path $PSScriptRoot '..\src\Portal\PortalTransition.cpp'
$commandsPath = Join-Path $PSScriptRoot '..\src\Portal\PortalBspCommands.cpp'
$transition = Get-Content -Raw -LiteralPath $transitionPath
$commands = Get-Content -Raw -LiteralPath $commandsPath

$beginLines = @($transition -split "`r?`n" | Where-Object { $_ -match '\[PortalViewReorientationBegin\]' })
if ($beginLines.Count -lt 2 -or @($beginLines | Where-Object { $_ -notmatch 'curve=%s timeScale=%.3f' }).Count -ne 0) {
    throw 'Every PortalViewReorientationBegin lifecycle diagnostic must carry curve/time-scale metadata.'
}

$endLines = @($transition -split "`r?`n" | Where-Object { $_ -match '\[PortalViewReorientationEnd\]' })
if ($endLines.Count -ne 1 -or $endLines[0] -notmatch 'curve=%s timeScale=%.3f') {
    throw 'PortalViewReorientationEnd must carry curve/time-scale metadata.'
}

$frameLines = @($transition -split "`r?`n" | Where-Object { $_ -match '\[PortalViewReorientationFrame\]' })
if ($frameLines.Count -ne 1 -or $frameLines[0] -match 'curve=|timeScale=') {
    throw 'Per-frame reorientation diagnostics must not repeat lifecycle curve metadata.'
}

$commandLines = @($commands -split "`r?`n" | Where-Object {
    $_ -match '\[PortalBsp\]\[Command\].*command=portal_visual_reorientation'
})
if ($commandLines.Count -ne 2 -or @($commandLines | Where-Object { $_ -notmatch 'curve=%s timeScale=%.3f' }).Count -ne 0) {
    throw 'Both reorientation command status diagnostics must carry curve/time-scale metadata.'
}

Write-Output 'PortalViewReorientation metadata tests passed'
