# package.ps1 - build the release folder and zip from an already-built tree.
#
#   powershell -ExecutionPolicy Bypass -File tools\package.ps1 [-Version 1.0.0] [-Out out\]
#
# Stages what a player actually downloads and zips it. The version comes from src\gdr_version.h
# unless -Version overrides it, so the folder, the zip and the mod's own startup log line can
# never disagree about which build this is.
#
# Layout of the staged folder:
#
#   x64\                    the two files that go into the game's x64 folder, and nothing else
#   data\, tools\           the optional database overlay's builder, kept at the same relative
#                           depth as the repo so its "..\tools" lookups work unchanged
#   *.md, LICENSE           the docs
#
# The overlay itself is NOT built or shipped here: it is made out of the player's own game records
# (see THIRD_PARTY.md), so the builder travels and the output does not.

param(
    [string]$Version = "",
    [string]$Out = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

if (-not $Version) {
    $hdr = Get-Content (Join-Path $root "src\gdr_version.h") -Raw
    if ($hdr -match 'GDR_VERSION\s+"([^"]+)"') { $Version = $Matches[1] }
    else { throw "GDR_VERSION not found in src\gdr_version.h" }
}
if (-not $Out) { $Out = Join-Path $root "out" }

$asi = Join-Path $root "bin\ragdoll.asi"
if (-not (Test-Path $asi)) { throw "bin\ragdoll.asi is missing - run build.bat first" }

$name  = "GrimDawnRagdoll-$Version"
$stage = Join-Path $Out $name
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force "$stage\x64"   | Out-Null
New-Item -ItemType Directory -Force "$stage\data"  | Out-Null
New-Item -ItemType Directory -Force "$stage\tools" | Out-Null

# 1. what gets copied into the game
Copy-Item $asi                               "$stage\x64\ragdoll.asi"
Copy-Item (Join-Path $root "ragdoll.ini")    "$stage\x64\ragdoll.ini"

# 2. the optional overlay builder, plus only the helpers it and verify_patch.py need
Copy-Item (Join-Path $root "data\build_db.py")   "$stage\data\"
Copy-Item (Join-Path $root "data\deploy_db.ps1") "$stage\data\"
foreach ($t in "arz.py", "arzw.py", "gdpath.py", "GameDir.ps1", "verify_patch.py") {
    Copy-Item (Join-Path $root "tools\$t") "$stage\tools\"
}

# 3. docs
foreach ($doc in "README.md", "LICENSE", "THIRD_PARTY.md", "MECHANISM.md") {
    $p = Join-Path $root $doc
    if (Test-Path $p) { Copy-Item $p "$stage\$doc" } else { Write-Warning "$doc is missing from the tree" }
}

# Gate the zip on the leak audit. A published binary carrying the build machine's directory
# layout, or a script with a hardcoded game path, is the kind of thing nobody notices until it is
# already on the internet - so this refuses to produce an archive rather than warn about one.
$audit = Join-Path $PSScriptRoot "audit_release.py"
if (Test-Path $audit) {
    Write-Host "[package] running the leak audit..."
    & python $audit
    if ($LASTEXITCODE -ne 0) {
        throw "audit_release.py found machine-specific content - refusing to package. Fix it and re-run."
    }
} else {
    Write-Warning "audit_release.py is missing - packaging WITHOUT a leak check"
}

$zip = Join-Path $Out "$name.zip"
if (Test-Path $zip) { Remove-Item -Force $zip }
Compress-Archive -Path "$stage\*" -DestinationPath $zip

Write-Host ("[package] {0} ({1:N0} bytes)" -f $zip, (Get-Item $zip).Length)
Get-ChildItem $stage -Recurse -File | ForEach-Object {
    Write-Host ("[package]   {0,-40} {1,9:N0}" -f $_.FullName.Substring($stage.Length + 1), $_.Length)
}
