# Changelog

## 1.0.0

First release. Lifts Grim Dawn's five-ragdoll limit so monsters ragdoll on death instead of
falling back to a canned animation. Measured at 98% of deaths ragdolling, the remainder being
meshes that ship without a ragdoll rig.

- Hooks `DefaultDeathHandler::Execute` and lifts the cap at runtime. **`Game.dll` is not
  modified** — see `MAINTAINING.md` → *Decisions not to undo*.
- `maxActiveRagdolls` to re-impose a ceiling, `ignoreRagdollFlag` for the per-record opt-out,
  `enabled=0` as a measuring control run.
- Fails safe: if the instruction pattern stops matching after a game update, the mod logs it,
  changes nothing, and the game runs stock.
- Optional database overlay (`data\build_db.py`) adds a ragdoll effect to the ~1,600 attack skills
  that ship without one, so corpses are thrown by the blow rather than collapsing. Built from the
  player's own installation; not distributed.
- `tools\audit_release.py` blocks packaging if anything machine-specific would be published.

### Two earlier attempts, kept in `MECHANISM.md` because they are the interesting part

- Hooking the exported `Character::ShouldDoRagDoll` and `GameEngine::AllowRagdolls` did nothing at
  all: both are inlined into `DefaultDeathHandler::Execute` and never called.
- Byte-patching the cap worked, but modified `Game.dll` in place and broke another plugin that
  locates its own targets by scanning the same loaded image.
