# GameDir.ps1 - find the Grim Dawn installation without writing a path down.
#
# Dot-source it:  . "$PSScriptRoot\..\tools\GameDir.ps1"
# then:           $gd = Get-GrimDawnDir              # throws with advice if not found
#                 $gd = Get-GrimDawnDir -Hint $Path  # an explicit path always wins
#
# Order: the caller's hint, then %GD_DIR%, then Steam's own registry key plus every library
# folder Steam lists in libraryfolders.vdf. The mirror of tools\gdpath.py, for the same reason:
# the game is found, never hard-coded.

function Get-GrimDawnDir {
    param([string]$Hint)

    $marker = 'x64\Grim Dawn.exe'

    $candidates = @()
    if ($Hint)        { $candidates += $Hint }
    if ($env:GD_DIR)  { $candidates += $env:GD_DIR }

    foreach ($c in $candidates) {
        $c = $c.TrimEnd('\', '/')
        if (Test-Path (Join-Path $c $marker)) { return $c }
        throw "Not a Grim Dawn folder (no $marker): $c"
    }

    $roots = @()
    foreach ($k in @('HKCU:\Software\Valve\Steam', 'HKLM:\SOFTWARE\WOW6432Node\Valve\Steam',
                     'HKLM:\SOFTWARE\Valve\Steam')) {
        foreach ($v in @('SteamPath', 'InstallPath')) {
            try {
                $p = (Get-ItemProperty -Path $k -Name $v -ErrorAction Stop).$v
                if ($p) { $roots += ($p -replace '/', '\') }
            } catch {}
        }
    }
    # Steam keeps other drives' libraries here; the "path" lines are all we need.
    foreach ($r in @($roots)) {
        $vdf = Join-Path $r 'steamapps\libraryfolders.vdf'
        if (Test-Path $vdf) {
            foreach ($m in [regex]::Matches((Get-Content $vdf -Raw), '"path"\s*"([^"]+)"')) {
                $roots += ($m.Groups[1].Value -replace '\\\\', '\')
            }
        }
    }

    foreach ($r in ($roots | Select-Object -Unique)) {
        $cand = Join-Path $r 'steamapps\common\Grim Dawn'
        if (Test-Path (Join-Path $cand $marker)) { return $cand }
    }

    throw @"
Grim Dawn was not found.

Pass -GameDir, or set GD_DIR to the folder that holds x64\Grim Dawn.exe:
    `$env:GD_DIR = '<your Steam library>\steamapps\common\Grim Dawn'
"@
}
