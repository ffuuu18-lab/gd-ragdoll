# How Grim Dawn decides between a ragdoll and a death animation

Reverse-engineered against Grim Dawn 1.3.0.8 x64. Everything here is checkable with the scripts in
`tools\` — none of it needs a disassembler.

If you only read one thing: **an exported symbol is not evidence that the engine calls it.**
Two versions of this mod were built on that assumption and neither did anything.

## The binaries hand you a symbol table

`Game.dll` exports **25,100** decorated C++ symbols and `Engine.dll` 6,283. Class methods,
signatures and all. So locating a function is `GetProcAddress` with a mangled name — no byte
pattern for entry points, no build-specific offsets, and the name documents the signature.

```
python tools\exports.py "<game>\x64\Game.dll" RagDoll Ragdoll
```

Two traps:

- **`pefile` silently truncates at 8,192 exports** and the cap cannot be raised by setting the
  module attribute. `DIRECTORY_ENTRY_EXPORT` shows 8,192 of 25,100 and everything sorting after
  roughly `?S...` looks absent. `tools\exports.py` parses the export directory by hand.
- **`Grim Dawn.exe` is Steam-DRM packed** — `.text` entropy 8.00, entry point in a `.bind`
  section. It cannot be analysed statically. The DLLs are clean; do the work there.

## The decision

```c
// DefaultDeathHandler::Execute(bool)   -- Game.dll +0x1E2BB0, branch inlined at +0x268
   chr->ragdollPhysics                       // cmp byte [rdi+0x286C], 0     je  -> animation
&& Entity::InRenderPreLoadFrustum(chr)       // call                         je  -> animation
&& Actor::HasRigidBodyData(chr)              // call                         je  -> animation
&& chr->doLateCrumple == 0                   // cmp byte [rcx+0x286D], 0     jne -> animation
&& gGameEngine->activeRagdolls < 5           // cmp dword [rax+0x37690], 5   jge -> animation
       ? ragdoll
       : play a weighted-random <weaponclass>DieAnim<N> from the character's animation table
```

`DefaultDeathHandler` is one of a family — `DissolveActor`, `FadeActor`, `PlayEffect`,
`PlaySound`, `SpawnActor`, `SpawnMyBones`, `Telkine` — one per value of the `deleteBehavior` field
in the character record.

Ragdolls are otherwise **pushed onto the victim by the killing attack**:
`CombatManager::TakeAttack` +0x1845 calls `Character::SetRagdollData` with a `PhysicsMotion` built
from the attack animation's own `ragDoll*` fields. That is what the optional database overlay
feeds. Death animations themselves carry no ragdoll data at all (0 of 148 `.anm` files), so no
data-only edit can make a death animation become a ragdoll.

### Character fields

From the record loader at `Game.dll +0x56151`. Two of these are parsed by the engine but are
**not** in `character.tpl`, so no modding tool shows them:

| field | type | offset | default |
| --- | --- | --- | --- |
| `overrideRagdollBehavior` | string (`Crumple`/`TakeHit`/`Random`) | +0x2810 | "" |
| `overrideRagdollSpeed` | float | +0x2830 | 0 |
| `PhysicsMotion` | 28 bytes | +0x2850 | — |
| `ragdollPhysics` | bool | +0x286C | **1** |
| **`doLateCrumple`** | bool — *undocumented* | +0x286D | 0 |
| **`physicsTimeLimit`** | int — *undocumented* | +0x2870 | — |

`PhysicsMotion` is `{ int type; float dir[3]; float speed; float; int }`. The engine's own default
is `{0, (0,1,0), 1.0, 0, 0}`, from the constant at rva `0x777340`.

## Wrong turn 1 — hooking functions nothing calls

`Character::ShouldDoRagDoll` and `GameEngine::AllowRagdolls` export cleanly and read exactly like
the decision:

```c
bool Character::ShouldDoRagDoll() const   // +0x6E110
    { return ragdollPhysics && InRenderPreLoadFrustum() && HasRigidBodyData(); }
bool GameEngine::AllowRagdolls() const    // +0x2D9B80
    { return activeRagdolls < 5; }        // kMaxActiveRagdolls = 5
```

v1 hooked both. Over **198 real deaths it logged zero calls to either.** The compiler inlined them
into `Execute` and left the out-of-line copies unreferenced. `ShouldDoRagDoll` has exactly one
static caller in the entire binary — `DefaultDeathHandler::AnimationCallback`, the separate
"late crumple" path, which is gated on `doLateCrumple` and is 0 on every shipped record.

A byte-exact caller scan finds this in seconds — but note that a *linear* capstone sweep desyncs
on data in `.text` and misses call sites. `tools\gddis.py` disassembles a range; scanning for
`E8`/`E9` rel32 by hand is what actually finds callers.

The general lesson: confirm a target is live — a caller scan, or better, hook it and count calls
in a real session — before building anything on it.

## Wrong turn 2 — patching bytes in a shared DLL

v2 found the real site and lifted the ceiling by rewriting its `jge` to two `NOP`s. It worked:
94% of deaths ragdolled, peak 28 concurrent against a stock ceiling of 5.

It also broke another plugin loaded in the same process — one that locates its own targets by
scanning `Game.dll` for byte patterns. The anchor v2 picked,
`48 8B 05 <gGameEngine> / 83 B8 <off> 05 / 7D`, is precisely the kind of distinctive sequence
something else would anchor on to find the `GameEngine` global, and two `NOP`s in the middle of it
make that scan miss.

**Do not write into a shared game DLL.** Everything loaded in the process reads the same image,
and a pattern-scanning plugin has no way to tell a patched byte from a different game version — it
just stops finding what it was looking for, usually silently. Hooking a function *prologue* is
fine and was tolerated throughout; editing bytes in the middle of one is not.

## What v3 does instead

The test *reads* `gGameEngine->activeRagdolls`, so there is no need to touch code at all. Hook
`Execute`, present a passing value for the duration of the original call, restore afterwards:

```c
saved = *pActive;  *pActive = 0;
o_Execute(self, finishing);
*pActive = saved + *pActive;     // restore the DELTA, not the old value
```

Restoring the delta keeps the engine's counter honest whether it registers the new ragdoll inside
that call or after it, so nothing drifts and `DecActiveRagdolls` stays balanced. It also nests
correctly if a death ever fires inside a death handler. Observing the live counter rise *and* fall
across a session (15, then 6) is the evidence that it works.

The site is still located by an instruction-pattern scan, but **read-only**: it exists purely to
decode the address of the `gGameEngine` global and the offset of `activeRagdolls` straight out of
the matched instructions, so neither is hard-coded to a build.

```
python tools\verify_patch.py      # confirms the pattern matches exactly once, and decodes both
```

## The census, and why 23% can never ragdoll

`Actor::HasRigidBodyData` is `!mesh->rigidBodyDescriptions.empty()` — purely a mesh property. Rigs
are authored as bones named `rigidBody0`, `rigidBody1` in the `.msh` files, so they can be counted
directly:

| | |
| --- | --- |
| creature meshes (base + gdx1/2/3) | 1,301 |
| with a rig | **1,002 (77.0%)** — always exactly 2 bodies |
| with none, can never ragdoll | 299 (23.0%) |
| creature `.anm` files with a `RagDollData` chunk | 515 of 3,461 |
| **death animations with ragdoll data** | **0 of 148** |
| attack-skill records specifying a ragdoll effect | 1,392 of 3,230 |
| records with `ragdollPhysics = 0` (opt-out) | 196 |

Two rigid bodies means the "ragdoll" is a two-part tumbling proxy, not articulated limbs. Worth
knowing before expecting Havok.

## Database overlays stack

Why the optional overlay is 1,596 records and not 82,448: a `/basemods` overlay needs **only the
records it changes**, contradicting the widespread advice that it must contain every vanilla one.
`Engine::LoadAdditionalDatabases(vector<string>)` loops and calls `LoadDatabase` on the *same*
`DatabaseArchive` that `LoadMainDatabase` created. The shipped expansions prove the semantics by
construction: GDX1/2/3 are partial archives (18k/16k/24k records against the base 34k) and 8,182
records exist in more than one of them, with the last loaded winning.

## Tools

All take a PE path and are not specific to this mod.

| script | what |
| --- | --- |
| `exports.py` | uncapped export dump with substring filter |
| `gddis.py` | disassemble an RVA range, annotating string refs and call targets |
| `verify_patch.py` | offline pre-flight: do the mod's patterns still match, and what do they decode to |
| `gdpath.py` / `GameDir.ps1` | find the installation via `%GD_DIR%` or the Steam registry |
| `arz.py` / `arzw.py` | read and write Grim Dawn `.arz` database archives |
