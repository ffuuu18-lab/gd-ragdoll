# gd-ragdoll

Make Grim Dawn's monsters ragdoll when they die, instead of playing a canned death animation.

Grim Dawn already has ragdoll physics, and it already uses them — **but only five corpses at a
time.** That limit is hard-coded. Clear a pack and the first few bodies tumble properly while
everything after them drops into the same scripted animation, which is why deaths look
inconsistent rather than simply bad. This mod lifts the limit.

Measured over a real session: **98% of deaths ragdolled** (the rest were monsters whose model has
no ragdoll rig at all), against a stock game that saturates its ceiling within seconds of a fight
starting.

## What you need

- Grim Dawn 1.3.0.8, 64-bit.
- [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader), the **x64** build.
  This mod is an ASI plugin: it does nothing until a loader loads it. The loader is not included
  here — download it once and it serves every ASI mod you use.
- Nothing else. No script extender, no launcher.

## Install

Everything goes in the game's `x64` folder — the one that holds `Grim Dawn.exe`.

1. Put the loader's `dinput8.dll` (x64 build) in `x64\`, beside `Grim Dawn.exe`. Its `winmm.dll`
   variant works too — use exactly one of them, or every plugin loads twice.
2. Copy `ragdoll.asi` and `ragdoll.ini` into that same `x64\` folder.

Then start the game and kill something.

If you built from source, `deploy.ps1` does steps 2 and onward for you and finds the game itself:

```
powershell -ExecutionPolicy Bypass -File deploy.ps1
```

## Settings

`x64\ragdoll.ini`, read once at launch. Every line is commented in the file itself.

| key | default | what it does |
| --- | --- | --- |
| `enabled` | 1 | master switch. `0` changes nothing but still counts, so it doubles as a control run |
| `liftCap` | 1 | lift the five-ragdoll ceiling |
| `maxActiveRagdolls` | 0 | `0` = no ceiling. `N` = allow a ragdoll while fewer than N are live |
| `patchBytes` | 0 | troubleshooting only — see *It does not modify the game* |
| `ignoreRagdollFlag` | 0 | ignore a record's deliberate `ragdollPhysics=0` opt-out (needs `patchBytes`) |
| `logLevel` | 1 | `0` off, `1` startup + a periodic tally, `2` one line per death |

If a huge fight ever stutters, set `maxActiveRagdolls=40` rather than turning `liftCap` off — that
keeps the behaviour and just bounds the worst case. A corpse here is a two-body physics proxy, not
an articulated ragdoll, so it is cheap; 28 concurrent was measured with no ill effect.

## What to expect, honestly

- **23% of Grim Dawn's creature models have no ragdoll rig** (299 of 1,301). Bladeswarms, ghosts,
  some chthonians and most equipment meshes have nothing to simulate and will keep playing their
  animation no matter what you set. No mod can change that without authoring new rigs.
- **Rigged models carry exactly two rigid bodies.** This is a two-part tumbling proxy, not
  articulated limbs. Expect corpses to flop, slide and roll — not to fold up like a modern
  ragdoll. This is Grim Dawn's own physics, just used more often.
- Bosses whose death is staged keep their scripted animation by default. That is deliberate;
  `ignoreRagdollFlag` overrides it if you would rather have no exceptions.

## It does not modify the game

`ragdoll.asi` leaves `Game.dll` byte-identical to what shipped. It hooks one function and, for the
duration of that call, shows the engine a ragdoll count that passes its own test — then puts the
true count back.

That is a deliberate constraint rather than an accident. An earlier version lifted the cap by
rewriting one instruction in the game's code; it worked, but other plugins locate their own
targets by scanning the same loaded image for byte patterns, and an edit in the middle of a
function makes those scans miss. Hooking a function's *prologue* is fine and universally
tolerated; editing bytes in the middle of one is not.

`patchBytes=1` restores that in-place edit. It exists only in case the runtime method ever stops
working, and anything else that scans the game's code may misbehave while it is on.

## The optional second half

The `.asi` is what makes monsters ragdoll. There is also an optional database overlay that makes
the ragdoll *look like the blow that caused it* — adding a direction, some lift and a force to the
1,596 attack skills that ship without any ragdoll effect, so bodies get thrown the way they were
hit instead of just collapsing.

It is a script rather than a file, because the overlay is built out of Grim Dawn's own records and
those are not ours to distribute (see `THIRD_PARTY.md`). You build it from your own installation:

```
python data\build_db.py --verify
powershell -ExecutionPolicy Bypass -File data\deploy_db.ps1
```

Then add `/basemods` to the game's launch options in Steam. Without that option the game ignores
the file completely, so installing it is not a commitment — and `deploy_db.ps1 -Undeploy` removes
it. Needs Python 3 and `pip install lz4`.

## When something goes wrong

The mod writes `ragdoll.log` next to the `.asi`. The startup block says what it found:

```
[site] cap site at ... | gGameEngine=... activeRagdolls=+0x37690 stockCap=5 (read-only)
[init] hooked DefaultDeathHandler::Execute
[init] ready | enabled=1 liftCap=1 maxActive=0 patchBytes=0 | Game.dll code untouched
```

and then tallies what actually happened:

```
[stats] periodic deaths=50 ragdolled=49/50 (98%) | refused: norig=1 optedOut=0 offScreen=0
        lateCrumple=0 overCap=0 | activeNow=6 cap=none codePatched=0
```

`norig` is the irreducible 23%. `overCap` should be zero — if it is not, the ceiling is still in
force. `logLevel=2` adds one line per death naming the condition that refused it.

**After a game update**, the mod may report `cap site NOT FOUND`. It then changes nothing and the
game runs stock — it does not guess. `python tools\verify_patch.py` checks the same thing offline
without launching the game.

## How it works

Short version: the decision is compiled *inline* into `DefaultDeathHandler::Execute`, so the
obvious-looking exported functions (`Character::ShouldDoRagDoll`, `GameEngine::AllowRagdolls`) are
unreferenced leftovers — hooking them does nothing at all. The live branch is:

```
ragdollPhysics && InFrustum && HasRigidBodyData && !doLateCrumple && activeRagdolls < 5
```

`MECHANISM.md` has the full reverse-engineering account, including the two wrong turns and how
each was caught.

## Build from source

Needs the MSVC 2022 C++ build tools (the free Build Tools package is enough). Nothing else — the
toolchain is located through `vswhere`, and MinHook is vendored.

```
build.bat
powershell -ExecutionPolicy Bypass -File deploy.ps1
```

To make a release instead of installing it, `tools\package.ps1` stages what a player downloads
into `out\` and zips it. The version comes from `src\gdr_version.h`, so the folder name, the zip
name and the mod's own startup log line can never disagree about which build something is.

```
powershell -ExecutionPolicy Bypass -File tools\package.ps1
```

Packaging will not produce a zip unless `tools\audit_release.py` passes. That scans everything
git would publish, plus the staged release, for anything machine-specific: absolute paths, the
current user's name, and the full build path MSVC stamps into a linked binary. Run it on its own
at any time:

```
python tools\audit_release.py          # what would be published
python tools\audit_release.py --all    # plus local build artifacts
```

It exists because the check it replaces was a hand-written `grep -E` for a drive-letter path, and
an escaping mistake made the pattern match nothing at all: a single backslash before a letter in a
regex turns it into a character class rather than a literal. The check reported "clean" for a file
that had a hardcoded path sitting in plain sight. Every test in the audit is a literal substring
comparison with no escaping to get wrong — and it deliberately flags drive-letter paths even in
documentation, which is why none appear in these files.

## Uninstall

```
powershell -ExecutionPolicy Bypass -File deploy.ps1 -Undeploy
```

Or just delete `x64\ragdoll.asi` and `x64\ragdoll.ini`. Nothing else was touched: the mod adds
files beside the game, never modifies the game's own, and writes nothing into your saves. If you
installed the overlay, remove `/basemods` from the launch options as well.

## Licence

MIT — see `LICENSE`. Third-party components and the reasoning about Grim Dawn's own data are in
`THIRD_PARTY.md`.
