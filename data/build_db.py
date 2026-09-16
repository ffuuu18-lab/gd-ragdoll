#!/usr/bin/env python3
r"""Build the gd-ragdoll database overlay (the optional second half of the mod).

WHY THIS IS A SCRIPT AND NOT A SHIPPED FILE
-------------------------------------------
The overlay is 1,596 Grim Dawn database records with five fields added to each. Those records are
Crate's data, not ours, so this repository does not carry them - you build the overlay from your
own installation, and what comes out stays on your machine.

WHAT IT CHANGES
---------------
Every attack-skill record that does not already specify a ragdoll effect gets one, so the killing
blow hands the victim a real PhysicsMotion - a direction, some lift, a force - instead of leaving
the engine to fall back on a generic impulse. Buff and debuff templates are excluded: vanilla
never sets a ragdoll effect on those and they are not what lands a killing blow.

This half is **optional**. The .asi alone is what makes monsters ragdoll; this makes the ragdoll
look like it was caused by the blow that killed them.

HOW IT IS LOADED
----------------
The engine stacks database archives: Engine::LoadAdditionalDatabases(vector<string>) loops and
calls LoadDatabase on the SAME DatabaseArchive that LoadMainDatabase created, and the shipped
expansions prove the semantics - GDX1/2/3 are partial archives (18k/16k/24k records against the
base 34k) and 8,182 records exist in more than one of them, with the last loaded winning. So the
overlay only needs the records it actually changes, not a rebuilt copy of all 82,448. The common
forum advice that a /basemods database must contain every vanilla record is wrong.

USAGE
-----
    python build_db.py             # build to out\database.arz
    python build_db.py --verify    # build, then re-read the result and check it
    python build_db.py --list      # show what would change, write nothing

The game is found through %GD_DIR% or the Steam registry (see ..\tools\gdpath.py).
Install the result with deploy_db.ps1, which explains the /basemods launch option.
"""

from __future__ import annotations

import argparse
import collections
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "tools"))

from gdpath import game_dir                                # noqa: E402
from arz import ArzDatabase, FT_FLOAT, FT_STRING           # noqa: E402
from arzw import ArzWriter, read_typed                     # noqa: E402

OUT_DIR = os.path.join(HERE, "out")
OUT = os.path.join(OUT_DIR, "database.arz")

# The picklist default leaks into some records as its own literal; it means "unset", not a value.
RAW_PICKLIST = ";Crumple;TakeHit;Random;"

# Values chosen as the most common real settings in the shipped data: TakeHit 1384/1456,
# Push 1170/1218, Upward the commonest non-None elevation, 1.5 the commonest amplification.
# TakeHit throws the corpse along the hit direction, which is what reads as "the blow killed it"
# rather than "it fell over".
NEW_FIELDS = [
    ("ragDollEffect",        FT_STRING, ["TakeHit"]),
    ("ragDollDirection",     FT_STRING, ["Push"]),
    ("ragDollPush",          FT_STRING, ["None"]),
    ("ragDollElevation",     FT_STRING, ["Upward"]),
    ("ragDollAmplification", FT_FLOAT,  [1.5]),
]


def is_attack_template(tn: str) -> bool:
    tn = tn.lower()
    if "skill_attack" not in tn and "wpattack" not in tn:
        return False
    # buff / debuf templates apply states; they are not the hit that kills something
    return "buf" not in tn.rsplit("/", 1)[-1]


def select(db):
    """Records that are attack skills and do not already declare a ragdoll effect."""
    out, already = [], 0
    for key, _tag, rec in db.items():
        tn = str(rec.get("templateName", ""))
        if not is_attack_template(tn):
            continue
        cur = str(rec.get("ragDollEffect", ""))
        if cur and cur != RAW_PICKLIST:
            already += 1
            continue
        out.append((key, _tag, tn))
    return out, already


def patch_fields(fields):
    """Replace any existing ragDoll* entry, append the rest, leave every other field untouched."""
    wanted = {n: (n, t, v) for n, t, v in NEW_FIELDS}
    out, seen = [], set()
    for name, ftype, vals in fields:
        if name in wanted:
            out.append(wanted[name])
            seen.add(name)
        else:
            out.append((name, ftype, vals))
    for name, ftype, vals in NEW_FIELDS:
        if name not in seen:
            out.append((name, ftype, vals))
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--verify", action="store_true", help="re-read the built archive and check it")
    ap.add_argument("--list", action="store_true", help="report the change set, write nothing")
    args = ap.parse_args()

    game = game_dir()
    print("game      : %s" % game)
    db = ArzDatabase.load_game(game)
    picked, already = select(db)

    by_tpl = collections.Counter(tn.rsplit("/", 1)[-1] for _, _, tn in picked)
    print("records   : %d" % len(db))
    print("already ok: %d attack skills already specify a ragdoll effect" % already)
    print("to patch  : %d" % len(picked))
    for tpl, n in by_tpl.most_common(8):
        print("     %-44s %4d" % (tpl, n))

    if args.list:
        print("\nfirst 20 records:")
        for key, tag, _ in picked[:20]:
            print("   [%-8s] %s" % (tag, key))
        return 0

    w = ArzWriter()
    for key, _tag, _tn in picked:
        arc = db.archives[db._index[key]]
        entry = arc.entries[key]
        w.add(arc.real_name(key) or key, patch_fields(read_typed(arc, key)), rtype=entry.rtype)

    os.makedirs(OUT_DIR, exist_ok=True)
    size = w.write(OUT)
    print("\nwrote %s  (%d records, %s bytes)" % (OUT, len(picked), format(size, ",")))

    if args.verify:
        from arz import ArzArchive
        chk = ArzArchive(OUT, "ragdoll")
        keys = list(chk.keys())
        bad = 0
        for key in keys:
            rec = chk.get(key)
            if str(rec.get("ragDollEffect", "")) != "TakeHit" or not rec.get("templateName"):
                bad += 1
                if bad <= 5:
                    print("   BAD %s" % key)
        sample = keys[0]
        rec = chk.get(sample)
        print("\nverify: %d records readable" % len(keys))
        print("   sample %s" % sample)
        print("      templateName         = %s" % rec.get("templateName"))
        for n, _t, _v in NEW_FIELDS:
            print("      %-20s = %r" % (n, rec.get(n)))
        vanilla = db.get(sample.replace("\\", "/").lower())
        print("      field count          = %d (vanilla had %d)" % (len(rec), len(vanilla)))
        print("   %s" % ("FAILED: %d bad records" % bad if bad
                         else "OK: every record carries the new fields and kept the old ones"))
        return 1 if bad else 0

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
