# deploy_db.ps1 - install the optional database overlay.
#
#   python build_db.py --verify                                  # build it first
#   powershell -ExecutionPolicy Bypass -File deploy_db.ps1
#   powershell -ExecutionPolicy Bypass -File deploy_db.ps1 -Undeploy
#
# Copies out\database.arz to <Grim Dawn>\mods\database.arz. That path is read ONLY when the game is
# launched with /basemods, so installing the file changes nothing by itself - the game behaves
# exactly as before until you add the launch option, and removing the option is enough to go back.
#
# To turn it on, in Steam: Grim Dawn -> Properties -> General -> Launch Options:
#
#     /basemods
#
# If the game fails to start with the option on, run -Undeploy (or just drop the option) and you
# are fully reverted. Database records are static data, so a bad overlay cannot damage a save.

param(
    [string]$GameDir,
    [switch]$Undeploy
)

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\..\tools\GameDir.ps1"

$gd      = Get-GrimDawnDir -Hint $GameDir
$modsDir = Join-Path $gd 'mods'
$dst     = Join-Path $modsDir 'database.arz'
$backup  = Join-Path $modsDir 'database.arz.pre-ragdoll'
Write-Host "game: $gd`n"

if ($Undeploy) {
    if (Test-Path $dst) {
        Remove-Item $dst -Force
        Write-Host "removed  $dst"
        if (Test-Path $backup) {
            Move-Item $backup $dst -Force
            Write-Host "restored the database.arz that was there before"
        }
    } else {
        Write-Host "absent   $dst"
    }
    Write-Host "`nUninstalled. You can also just drop /basemods from the launch options."
    exit 0
}

$proc = Get-Process -Name 'Grim Dawn' -ErrorAction SilentlyContinue
if ($proc) { throw "Grim Dawn is running (pid $($proc.Id)). Close it first." }

$src = Join-Path $PSScriptRoot 'out\database.arz'
if (-not (Test-Path $src)) { throw "Not built: $src`nRun: python build_db.py --verify" }
if (-not (Test-Path $modsDir)) { New-Item -ItemType Directory -Path $modsDir -Force | Out-Null }

# Never silently replace someone else's basemod database.
if ((Test-Path $dst) -and -not (Test-Path $backup)) {
    Copy-Item $dst $backup -Force
    Write-Host "backed up the existing database.arz -> $backup"
}

Copy-Item $src $dst -Force
Write-Host ("installed {0}  ({1:N0} bytes)" -f $dst, (Get-Item $dst).Length)

Write-Host @"

NOT ACTIVE YET. Add this to Steam -> Grim Dawn -> Properties -> Launch Options:

    /basemods

Without it the game ignores mods\database.arz entirely and runs stock.
The mod's log records whether /basemods was present, so you can check it worked.
"@
