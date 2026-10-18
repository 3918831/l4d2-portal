$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$hooksPath = Join-Path $repoRoot 'src\Hooks\Hooks.cpp'
$utf8 = New-Object System.Text.UTF8Encoding($false)
$sourceText = [System.IO.File]::ReadAllText($hooksPath, $utf8)
$sourceLines = $sourceText -split "`r?`n"
$activeRegistrations = @(
    foreach ($line in $sourceLines)
    {
        $code = $line -replace '//.*$', ''
        if ($code -match '\bInputAngles::Init\s*\(\s*\)\s*;')
        {
            $code.Trim()
        }
    }
)

if ($activeRegistrations.Count -ne 1)
{
    Write-Error "Expected exactly one active InputAngles::Init() registration in $hooksPath; found $($activeRegistrations.Count)."
    exit 1
}

$sourceBytes = [System.IO.File]::ReadAllBytes($hooksPath)
$registrationBytes = [System.Text.Encoding]::ASCII.GetBytes('InputAngles::Init();')
$registrationIndex = -1
for ($candidate = 0; $candidate -le $sourceBytes.Length - $registrationBytes.Length; ++$candidate)
{
    $matches = $true
    for ($offset = 0; $offset -lt $registrationBytes.Length; ++$offset)
    {
        if ($sourceBytes[$candidate + $offset] -ne $registrationBytes[$offset])
        {
            $matches = $false
            break
        }
    }

    if ($matches)
    {
        $registrationIndex = $candidate
        break
    }
}

if ($registrationIndex -lt 0)
{
    Write-Error 'Could not locate the InputAngles::Init() registration bytes.'
    exit 1
}

$precedingLf = $registrationIndex - 1
while ($precedingLf -ge 0 -and $sourceBytes[$precedingLf] -ne 0x0A)
{
    --$precedingLf
}

if ($precedingLf -le 0 -or $sourceBytes[$precedingLf - 1] -gt 0x7F)
{
    $precedingByte = if ($precedingLf -gt 0) { '0x{0:X2}' -f $sourceBytes[$precedingLf - 1] } else { 'missing' }
    Write-Error "The physical line before InputAngles::Init() ends with $precedingByte directly before LF; the CP936 MSVC frontend can consume that LF and comment out the registration."
    exit 1
}

$hooksObjectPath = Join-Path $repoRoot 'src\Debug\Hooks.obj'
if (-not (Test-Path -LiteralPath $hooksObjectPath))
{
    Write-Error "Missing compiled hook object: $hooksObjectPath"
    exit 1
}

$dumpbin = (Get-Command dumpbin.exe -ErrorAction Stop).Source
$disassembly = & $dumpbin /nologo /disasm /relocations $hooksObjectPath | Out-String
$clientModeSymbol = '?Init@ClientMode@Hooks@@YAXXZ'
$inputAnglesSymbol = '?Init@InputAngles@Hooks@@YAXXZ'

if (-not $disassembly.Contains($clientModeSymbol))
{
    Write-Error 'The compiled CGlobal_Hooks::Init artifact lost ClientMode::Init().'
    exit 1
}

if (-not $disassembly.Contains($inputAnglesSymbol))
{
    Write-Error 'The compiled CGlobal_Hooks::Init artifact does not call InputAngles::Init().'
    exit 1
}

Write-Output 'Portal Input AdjustAngles hook registration artifact test passed'
