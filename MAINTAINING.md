# Maintaining gd-ragdoll

For whoever picks this up next — human or agent. `README.md` is for players; this is for changing
the thing. `MECHANISM.md` is the reverse-engineering account and is the prerequisite for any change
to the hook itself: read *The decision* and both *Wrong turn* sections before touching `dllmain.cpp`.

## The 60-second version

Grim Dawn only allows **five ragdolls at once**. That limit is compiled inline into
`DefaultDeathHandler::Execute`, so there is no function to hook and no data field to edit. The mod
hooks that one function and, for the duration of the original call, shows the engine a ragdoll
count that passes its own test, then restores the true count. **It does not modify `Game.dll`.**

Everything else — the `.ini`, the log, the audit, the optional database overlay — is support.

## What you need

- **MSVC 2022 C++ build tools.** That is all `build.bat` requires; `tools\find_vcvars.bat` locates
  them through `vswhere`, so no path is written down.
- **Python 3.8+ and `pip install -r requirements.txt`** for the tooling only. Not needed to build
  or to use the mod. `verify_patch.py` needs `pefile`, and the README tells players to run it after
  a game update — so install this before trusting that instruction.
- **A Grim Dawn install.** Every script finds it via `%GD_DIR%` or the Steam registry
  (`tools\gdpath.py`, `tools\GameDir.ps1`). Never hard-code it — see *The audit* below.

## File map

Read these to understand the mod:

| file | what it is |
| --- | --- |
| `src\dllmain.cpp` | the whole mod: config, the hook, the cap lift, logging, install |
| `src\gdr.h` | engine symbol names, the `PhysicsMotion` layout, character field offsets |
| `src\gdr_version.h` | the one place the version is written |
| `ragdoll.ini` | shipped defaults, with every option explained inline |

Build and ship:

| file | what it is |
| --- | --- |
| `build.bat` | MSVC build → `bin\ragdoll.asi`. Note `/PDBALTPATH` — see *Releases* |
| `deploy.ps1` | install/uninstall into the game's `x64\` |
| `tools\package.ps1` | stage + zip a release into `out\`, gated on the audit |
| `tools\audit_release.py` | leak scan; blocks packaging |

Investigation tools — not needed to build, used when the engine changes:

| file | what it is |
| --- | --- |
| `tools\verify_patch.py` | does the instruction pattern still match, and what does it decode to |
| `tools\exports.py` | dump a PE's exports (uncapped — `pefile` truncates at 8,192) |
| `tools\gddis.py` | disassemble an RVA range |
| `tools\gdpath.py`, `tools\GameDir.ps1` | find the game |

The optional database overlay — independent of the `.asi`, skip unless you are touching it:

| file | what it is |
| --- | --- |
| `data\build_db.py` | builds the overlay from the player's own game records |
| `data\deploy_db.ps1` | installs it; needs `/basemods` in the launch options |
| `tools\arz.py`, `tools\arzw.py` | read/write Grim Dawn `.arz` archives |

## The working loop

```
build.bat
powershell -ExecutionPolicy Bypass -File deploy.ps1
# launch the game, kill things, quit
type "<game>\x64\ragdoll.log"
```

`deploy.ps1` refuses to overwrite a running game, and never overwrites an existing `ragdoll.ini`,
so your test settings survive a redeploy.

## How to tell whether a change worked

Set `logLevel=2` in the installed `x64\ragdoll.ini` and read the tally:

```
[stats] periodic deaths=50 ragdolled=49/50 (98%) | refused: norig=1 optedOut=0 offScreen=0
        lateCrumple=0 overCap=0 | activeNow=6 cap=none codePatched=0
```

What good looks like:

- **`overCap=0`.** If it is not zero the cap is still in force and the mod is not working.
- **`ragdolled` around 95%+**, with the remainder `norig`. `norig` is the irreducible ~23% of
  creature meshes that ship without a ragdoll rig — see the census in `MECHANISM.md`. It varies by
  area, so a single session can read anywhere from ~5% to ~25%.
- **`activeNow` rising *and* falling** across a session. That is the evidence the delta-restore in
  `hk_Execute` keeps the engine's counter balanced. A number that only climbs, or goes negative,
  means the restore is wrong — that is the most fragile part of the mod.
- **`codePatched=0`** and `Game.dll code untouched` at startup.

**Control run:** set `enabled=0` and play. Every hook still runs and still counts, but nothing is
changed — so the log tells you what the *stock* game does. That is the honest before/after, and it
is why the counters are deliberately not gated behind `logLevel=2`.

## When Grim Dawn updates

This is the maintenance event. The mod is designed to fail safe: if the instruction pattern no
longer matches, `LocateSites` logs `cap site NOT FOUND`, changes nothing, and the game runs stock.
It never guesses.

**1. Check it offline, before launching anything:**

```
python tools\verify_patch.py
```

- *`PRE-FLIGHT OK`* — the pattern still matches and decodes to the same field offset and stock cap.
  Nothing to do; rebuild only if you changed something.
- *`CAP matches=0`* — the instruction sequence changed. Go to step 2.
- *`CAP matches=2+`* — the pattern is no longer unique. Do **not** patch; lengthen it, go to step 2.
- *`unexpected decode`* — it matched but `activeRagdolls` moved or the stock cap is no longer 5.
  The offset is decoded at runtime, so a moved field is fine; a changed cap value means updating
  the expectation in `verify_patch.py`.

**2. If the pattern broke, re-derive it:**

```
python tools\exports.py "<game>\x64\Game.dll" DeathHandler
python tools\gddis.py  "<game>\x64\Game.dll" <Execute rva> 400
```

Find the run of conditions described in `MECHANISM.md` → *The decision*. You are looking for the
last one: a `mov rax,[rip+disp32]` loading the `GameEngine` global, a `cmp dword [rax+off], 5`, and
a `jge` to the death-animation path. Update `PAT_CAP` in **both** `src\dllmain.cpp` and
`tools\verify_patch.py` — they are deliberately the same pattern in two places so the offline check
actually checks the shipped behaviour. Keep decoding the global and the offset from the matched
instructions rather than hard-coding them.

**3. Re-check the symbol names.** They are resolved by mangled name, so a signature change breaks
the lookup loudly (`symbol NOT FOUND` in the log):

```
python tools\exports.py "<game>\x64\Game.dll"   DefaultDeathHandler
python tools\exports.py "<game>\x64\Engine.dll" HasRigidBodyData InRenderPreLoadFrustum
```

**4. Re-run the census** if you want the README's percentages to stay true — the method is in
`MECHANISM.md` → *The census*. Expansions add meshes, so the 23% figure drifts.

**5. Bump `src\gdr_version.h`** and cut a release.

## Releases

```
build.bat
powershell -ExecutionPolicy Bypass -File tools\package.ps1
```

Stages `out\GrimDawnRagdoll-<version>\` and a zip. The version comes from `src\gdr_version.h`, so
the folder, the zip and the mod's own startup log line cannot disagree.

`package.ps1` **will not produce a zip unless `audit_release.py` passes.** Do not work around that
— see below.

## The audit, and why it is strict

`tools\audit_release.py` scans everything git would publish, plus the staged release, for absolute
paths, the current user's name, and the full build path MSVC stamps into a linked binary.

It exists because a hand-written `grep -E` for a drive-letter path *silently matched nothing* — a
backslash before a letter in a regex makes a character class — and reported a tree "clean" while a
script in it had a hardcoded game path, which then went public. Every test in the audit is a
literal substring comparison with no escaping to get wrong.

It is deliberately strict enough to flag drive-letter paths **in documentation**. That is why no
example path appears in these files. If it flags something, fix the content; do not loosen the
check.

`/PDBALTPATH:%_PDB%` in `build.bat` is not cosmetic. Without it the linker stamps the **full path**
of the `.pdb` into every binary, so a shipped DLL carries the build machine's directory layout. The
audit verifies this took effect on every build.

## Decisions not to undo

A newcomer will be tempted by each of these. They were all tried or considered:

- **Do not hook `Character::ShouldDoRagDoll` or `GameEngine::AllowRagdolls`.** They export cleanly
  and read exactly like the decision. They are inlined away and *never called* — measured, zero
  calls across 198 deaths. This cost a whole version.
- **Do not patch bytes in `Game.dll`.** It works, and it broke another plugin in the same process
  that locates its targets by scanning the same image. `patchBytes=1` still exists for emergencies
  and is documented as antisocial.
- **Do not bypass `HasRigidBodyData`.** A mesh with no rig has nothing to simulate; that check is
  the engine protecting itself, not an obstacle.
- **Do not bypass the frustum check.** Skipping unwatched deaths leaves budget for the ones on
  screen. It is a feature.
- **Do not commit the built overlay.** It is ~1,600 verbatim Grim Dawn records. The repo ships the
  *builder*; the output is generated on the player's machine. See `THIRD_PARTY.md`.
- **Do not gate the counters behind `logLevel=2`.** They were, once; it made a control run report
  all zeros and cost a session to notice.

## Verified vs assumed

- Everything here was established against **Grim Dawn 1.3.0.8 x64**. No other version has been
  tested.
- The mechanism, the field offsets and the census are **read out of the shipped binaries and data**,
  and every claim is re-checkable with the scripts in `tools\`.
- The behaviour numbers (98% ragdolled, `activeNow` peaking around 28) are from **real play
  sessions**, not simulation — but from one machine and a handful of areas.
- The reason the byte patch broke another plugin is **inferred** from that plugin's own strings
  (it scans for byte patterns and reports features unavailable when a scan misses). The exact
  colliding pattern was never confirmed. The fix does not depend on the explanation being right.
