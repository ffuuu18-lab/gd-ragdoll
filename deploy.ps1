# deploy.ps1 - install ragdoll.asi into Grim Dawn's x64 folder.
#
#   powershell -ExecutionPolicy Bypass -File deploy.ps1
#   powershell -ExecutionPolicy Bypass -File deploy.ps1 -Undeploy
#   powershell -ExecutionPolicy Bypass -File deploy.ps1 -GameDir "D:\Games\Grim Dawn"
#
# Adds two files and nothing else: x64\ragdoll.asi and x64\ragdoll.ini. It does not patch, replace
# or back up anything belonging to the game, and -Undeploy removes exactly what it added.
# ragdoll.ini is never overwritten once it exists, so your settings survive an update.
#
# The .asi needs an ASI loader (Ultimate ASI Loader's x64 dinput8.dll or winmm.dll) already in
# that folder; this script says so if it cannot see one.

param(
    [string]$GameDir,
    [switch]$Undeploy
)

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\tools\GameDir.ps1"

$gd  = Get-GrimDawnDir -Hint $GameDir
$x64 = Join-Path $gd 'x64'
Write-Host "game: $gd`n"

$asiDst = Join-Path $x64 'ragdoll.asi'
$iniDst = Join-Path $x64 'ragdoll.ini'
$logDst = Join-Path $x64 'ragdoll.log'

if ($Undeploy) {
    foreach ($f in @($asiDst, $iniDst, $logDst, (Join-Path $x64 'ragdoll.pdb'))) {
        if (Test-Path $f) { Remove-Item $f -Force; Write-Host "removed  $f" }
    }
    Write-Host "`nUninstalled. Ragdolls are back to the stock limit of five on the next launch."
    exit 0
}

$proc = Get-Process -Name 'Grim Dawn' -ErrorAction SilentlyContinue
if ($proc) { throw "Grim Dawn is running (pid $($proc.Id)). Close it first." }

$asiSrc = Join-Path $PSScriptRoot 'bin\ragdoll.asi'
if (-not (Test-Path $asiSrc)) { throw "Not built: $asiSrc`nRun build.bat first." }

Copy-Item $asiSrc $asiDst -Force
Write-Host "installed $asiDst"

$pdbSrc = Join-Path $PSScriptRoot 'bin\ragdoll.pdb'
if (Test-Path $pdbSrc) { Copy-Item $pdbSrc (Join-Path $x64 'ragdoll.pdb') -Force }

if (Test-Path $iniDst) {
    Write-Host "kept      $iniDst  (your settings are preserved)"
} else {
    Copy-Item (Join-Path $PSScriptRoot 'ragdoll.ini') $iniDst -Force
    Write-Host "installed $iniDst"
}

$loader = @('dinput8.dll', 'winmm.dll', 'version.dll', 'dsound.dll') |
          Where-Object { Test-Path (Join-Path $x64 $_) }
Write-Host ""
if ($loader) {
    Write-Host "ASI loader present: $($loader -join ', ')"
} else {
    Write-Warning @"
No ASI loader found in $x64.

ragdoll.asi does nothing on its own. Download the x64 build of Ultimate ASI Loader
(https://github.com/ThirteenAG/Ultimate-ASI-Loader) and put its dinput8.dll in that folder.
"@
}
Write-Host "`nLog will appear next to the .asi as ragdoll.log."
