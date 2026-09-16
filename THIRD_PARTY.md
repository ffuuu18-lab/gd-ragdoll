# Third-party components

gd-ragdoll is MIT (see `LICENSE`). This file lists everything else it uses and what that means for
anyone who ships, forks or repackages it.

| component | licence | how it is used |
| --- | --- | --- |
| MinHook | BSD 2-Clause | vendored source under `third_party\minhook`, compiled into `ragdoll.asi` |
| Ultimate ASI Loader | MIT | required at run time, linked to, **not** redistributed here |
| Grim Dawn's own data | not ours | the optional database overlay is built on your machine from your own installation and is **not** distributed here — see below |

---

## MinHook

The detour library. Its sources are vendored under `third_party\minhook` and compiled directly
into `ragdoll.asi`, so the binary is a derived work and carries the notice below.
Upstream: <https://github.com/TsudaKageyu/minhook>.

The text is `third_party\minhook\LICENSE.txt`, reproduced in full:

```
MinHook - The Minimalistic API Hooking Library for x64/x86
Copyright (C) 2009-2017 Tsuda Kageyu.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

  1. Redistributions of source code must retain the above copyright
     notice, this list of conditions and the following disclaimer.
  2. Redistributions in binary form must reproduce the above copyright
     notice, this list of conditions and the following disclaimer in the
     documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

## Ultimate ASI Loader

`ragdoll.asi` does nothing until an ASI loader loads it. The loader is **not** included here —
download it once from <https://github.com/ThirteenAG/Ultimate-ASI-Loader> and it serves every ASI
mod you use. Nothing in this repository is derived from it; the mod only relies on the convention
that `*.asi` files beside the executable get `LoadLibrary`'d.

## Grim Dawn's own data

Two things touch Crate Entertainment's data, and neither is redistributed here.

**The .asi** reads nothing from disk. It resolves engine functions by their exported names at run
time and reads a handful of values out of the loaded `Game.dll` to learn where the game keeps its
ragdoll counter. No game file is copied, modified or shipped — and since v3 the mod does not write
to the game's memory image either.

**The database overlay** (`data\build_db.py`) is the reason that script exists instead of a file.
The overlay is 1,596 Grim Dawn database records with five fields added to each. Those records are
Crate's data, so this repository ships the *builder*, not the result: you run it against your own
installation and the output stays on your machine. `data/out/` is in `.gitignore` for that reason.
If you fork this, keep it that way.

The mod itself is a behaviour change to a game you already own. It adds files beside the game and
removes them again on request; it does not alter, crack or redistribute any part of Grim Dawn.
