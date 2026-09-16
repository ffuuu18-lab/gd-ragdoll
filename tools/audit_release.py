#!/usr/bin/env python3
"""Scan the tree and the built binary for anything machine-specific before publishing.

    python tools\\audit_release.py            # scan what git would track, plus out\\ and bin\\
    python tools\\audit_release.py --all      # scan every file, including build artifacts

Checks, in order of how badly they bite:

  1. absolute paths in text files - a drive letter plus a backslash, e.g. C:\\ or T:\\
  2. user names / home directories
  3. the CodeView PDB path stamped into a built PE. MSVC records the FULL path of the .pdb in
     every binary it links; /PDBALTPATH:%%_PDB%% in build.bat reduces it to a bare filename, and
     this is what proves it worked.
  4. the same strings appearing raw anywhere in a shipped binary

Why this exists: a hand-written `grep -E "S:\\\\SteamLibrary"` silently matches nothing, because a
single backslash in an ERE turns S into the \\S character class. The check that was supposed to
catch a hardcoded path reported "clean" for a file that had one. Everything here is a literal
substring test - no regex, no escaping to get wrong.
"""

from __future__ import annotations

import argparse
import os
import string
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

BACKSLASH = chr(92)

# "<drive letter>:<backslash>" for every drive letter, built without writing an escape.
DRIVE_PREFIXES = [c + ":" + BACKSLASH for c in string.ascii_uppercase]
DRIVE_PREFIXES += [c.lower() + ":" + BACKSLASH for c in string.ascii_lowercase]

# Names that should never reach a public tree. The current user is picked up automatically so
# this keeps working for whoever forks it.
IDENTITY = [n for n in {os.environ.get("USERNAME", ""), os.environ.get("USER", "")} if n]

TEXT_EXT = {".py", ".ps1", ".bat", ".cpp", ".h", ".md", ".ini", ".txt", ".gitignore",
            ".gitattributes", ""}
BINARY_EXT = {".asi", ".dll", ".exe", ".pdb", ".obj", ".map", ".zip"}

# Paths that are examples in documentation rather than real machine paths.
ALLOWED = [
    "C:" + BACKSLASH + "Users" + BACKSLASH + "you",       # the ini's commented logPath example
    "D:" + BACKSLASH + "Games",                            # a README -GameDir example
]


def nested_repos():
    """Top-level directories that are checkouts in their own right."""
    out = []
    for name in os.listdir(ROOT):
        p = os.path.join(ROOT, name)
        if os.path.isdir(p) and os.path.isdir(os.path.join(p, ".git")):
            out.append(name)
    return out


NESTED_REPOS = nested_repos()

# Relative paths git would publish. Filled in main(); used to decide whether a .pdb matters.
TRACKED = set()


def tracked_files():
    """What git would publish, if this is a repo; otherwise every non-ignored file."""
    try:
        out = subprocess.run(["git", "-C", ROOT, "ls-files"],
                             capture_output=True, text=True, timeout=30)
        if out.returncode == 0 and out.stdout.strip():
            return [os.path.join(ROOT, p.replace("/", os.sep)) for p in out.stdout.split("\n") if p]
    except Exception:
        pass
    files = []
    # Mirrors .gitignore: without a repo to ask, build output must not be mistaken for content.
    skip = {".git", "build", "bin", "out", "__pycache__", "third_party"}
    for base, dirs, names in os.walk(ROOT):
        dirs[:] = [d for d in dirs if d not in skip]
        for n in names:
            files.append(os.path.join(base, n))
    return files


def hits_in_text(path):
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as fh:
            text = fh.read()
    except OSError:
        return []
    found = []
    for line_no, line in enumerate(text.splitlines(), 1):
        for pref in DRIVE_PREFIXES:
            i = line.find(pref)
            while i != -1:
                # A real path has its drive letter at a token boundary. This rules out registry
                # hives - HKCU:\ and HKLM:\ end in "U:\" and "M:\" and would otherwise look like
                # drives - and any other identifier that happens to end in a letter-colon-slash.
                preceded_by_word = i > 0 and (line[i - 1].isalnum() or line[i - 1] == "_")
                frag = line[i:i + 60]
                if not preceded_by_word and not any(frag.startswith(a) for a in ALLOWED):
                    found.append((line_no, "absolute path", line.strip()[:110]))
                    break
                i = line.find(pref, i + 1)
        for who in IDENTITY:
            if who and who in line:
                found.append((line_no, "user name", line.strip()[:110]))
    return found


def hits_in_binary(path):
    try:
        data = open(path, "rb").read()
    except OSError:
        return []
    found = []
    for pref in DRIVE_PREFIXES:
        needle = pref.encode("latin-1")
        i = data.find(needle)
        if i != -1:
            # pull the surrounding printable run so the report shows what leaked
            s = i
            while s > 0 and 0x20 <= data[s - 1] < 0x7f:
                s -= 1
            e = i
            while e < len(data) and 0x20 <= data[e] < 0x7f:
                e += 1
            found.append(("absolute path", data[s:e].decode("latin-1")[:110]))
    for who in IDENTITY:
        if who and who.encode("latin-1") in data:
            found.append(("user name", who))
    return found


def pdb_path(path):
    """The CodeView PDB path MSVC stamps into a PE, or None."""
    try:
        import pefile
    except ImportError:
        return "?? (pefile not installed)"
    try:
        pe = pefile.PE(path, fast_load=True)
        pe.parse_data_directories(
            directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_DEBUG"]])
        for dbg in getattr(pe, "DIRECTORY_ENTRY_DEBUG", []) or []:
            entry = dbg.entry
            if hasattr(entry, "PdbFileName"):
                return entry.PdbFileName.rstrip(b"\x00").decode("latin-1")
    except Exception as exc:
        return "?? (%s)" % exc
    return None


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--all", action="store_true",
                    help="scan every file, including gitignored build artifacts")
    args = ap.parse_args()

    if args.all:
        targets = []
        for base, dirs, names in os.walk(ROOT):
            dirs[:] = [d for d in dirs if d != ".git"]
            targets += [os.path.join(base, n) for n in names]
    else:
        targets = tracked_files()
        TRACKED.update(os.path.relpath(p, ROOT) for p in targets)
        # the release staging area is not tracked but IS what people download
        for extra in ("out", "bin"):
            d = os.path.join(ROOT, extra)
            for base, _dirs, names in os.walk(d):
                targets += [os.path.join(base, n) for n in names]

    print("root      : %s" % ROOT)
    print("identity  : %s" % (", ".join(IDENTITY) or "(none in environment)"))
    if NESTED_REPOS:
        print("skipping  : nested checkout(s) %s" % ", ".join(NESTED_REPOS))
    print("scanning  : %d files%s\n" % (len(targets), " (--all)" if args.all else ""))

    problems = 0
    for path in sorted(set(targets)):
        if not os.path.isfile(path):
            continue
        rel = os.path.relpath(path, ROOT)
        if rel.startswith("third_party"):
            continue
        # A nested checkout is a different repository with its own history and its own release
        # artifacts. Scanning it reports that repo's problems as if they were this tree's.
        if any(nested and rel.startswith(nested + os.sep) for nested in NESTED_REPOS):
            continue
        # This script necessarily contains the very patterns it searches for, in its own
        # documentation. Scanning it would report itself forever.
        if os.path.basename(path) == os.path.basename(__file__):
            continue
        ext = os.path.splitext(path)[1].lower()
        if ext == ".pdb":
            # A .pdb records the full build path of every object it covers - that is its job, and
            # it cannot be scrubbed. What matters is that it is never published. Flag it only if
            # it is tracked by git or staged in a release, never for merely existing in bin\.
            staged = rel.startswith("out" + os.sep)
            if staged or rel in TRACKED:
                print("  LEAK %-46s %-14s %s" % (rel, "pdb published",
                                                 "a .pdb always contains build paths"))
                problems += 1
            else:
                print("  ok   %-46s pdb           local only, not tracked or staged" % rel)
            continue
        if ext in BINARY_EXT:
            for kind, frag in hits_in_binary(path):
                print("  LEAK %-46s %-14s %s" % (rel, kind, frag))
                problems += 1
            if ext in {".asi", ".dll", ".exe"}:
                pdb = pdb_path(path)
                if pdb and (BACKSLASH in pdb or "/" in pdb):
                    print("  LEAK %-46s %-14s %s" % (rel, "pdb path", pdb))
                    problems += 1
                elif pdb:
                    print("  ok   %-46s pdb path      %s" % (rel, pdb))
        elif ext in TEXT_EXT:
            for line_no, kind, frag in hits_in_text(path):
                print("  LEAK %s:%d  %-14s %s" % (rel, line_no, kind, frag))
                problems += 1

    print()
    if problems:
        print("FAILED: %d thing(s) would leak. Fix before publishing." % problems)
        return 1
    print("CLEAN: nothing machine-specific in what would be published.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
