#!/usr/bin/env python3
"""Pre-flight for gd-ragdoll: confirm the instruction patterns the mod looks for match exactly
once inside the exported DefaultDeathHandler::Execute, and decode what the C++ decodes at runtime
(the gGameEngine global and the activeRagdolls field offset).

The mod does NOT write to these bytes - it only reads them to learn those two values - but if the
patterns stop matching after a game update, the cap cannot be lifted, and this says so offline
without launching anything.

    python verify_patch.py
"""
import os, struct, sys
import pefile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gdpath import game_dir            # noqa: E402  - %GD_DIR%, else the Steam registry

P = os.path.join(game_dir(), "x64", "Game.dll")
print("Game.dll   : %s" % P)
pe = pefile.PE(P, fast_load=True); base = pe.OPTIONAL_HEADER.ImageBase
data = open(P, 'rb').read()
secs = [(s.VirtualAddress, s.Misc_VirtualSize, s.PointerToRawData, s.SizeOfRawData) for s in pe.sections]
def r2o(r):
    for va, vs, pr, sr in secs:
        if va <= r < va + max(vs, sr): return pr + (r - va)

d = pe.OPTIONAL_HEADER.DATA_DIRECTORY[0]; o = r2o(d.VirtualAddress)
(_a,_b,_c,_e,_f,ordBase,nFunc,nNames,aF,aN,aO) = struct.unpack_from('<IIHHIIIIIII', data, o)
of, on, oo = r2o(aF), r2o(aN), r2o(aO)
exp = {}
for i in range(nNames):
    nr = struct.unpack_from('<I', data, on+4*i)[0]; no = r2o(nr)
    nm = data[no:data.find(b'\x00', no)].decode('latin-1')
    ordi = struct.unpack_from('<H', data, oo+2*i)[0]
    exp[nm] = struct.unpack_from('<I', data, of+4*ordi)[0]

EXEC = "?Execute@DefaultDeathHandler@GAME@@UEAAX_N@Z"
rva = exp[EXEC]
print("Execute rva = 0x%x" % rva)
body = data[r2o(rva):r2o(rva)+0x600]

def find(pat, name):
    n = len(pat); hits = []
    for i in range(len(body)-n+1):
        if all(p == 0x100 or body[i+j] == p for j, p in enumerate(pat)): hits.append(i)
    print("%-10s matches=%d %s" % (name, len(hits), ["+0x%x" % h for h in hits]))
    return hits

PAT_CAP = [0x48,0x8B,0x05,0x100,0x100,0x100,0x100, 0x83,0xB8,0x100,0x100,0x100,0x100,0x100, 0x7D,0x100]
PAT_FLAG= [0x80,0xBF,0x6C,0x28,0x00,0x00,0x00,0x74,0x100]
cap = find(PAT_CAP, "CAP"); flag = find(PAT_FLAG, "FLAG")
ok = True
if len(cap) != 1: print("  !! CAP must match exactly once"); ok = False
if len(flag) != 1: print("  !! FLAG must match exactly once"); ok = False
if cap:
    i = cap[0]
    disp = struct.unpack_from('<i', body, i+3)[0]
    gg   = rva + i + 7 + disp
    off  = struct.unpack_from('<i', body, i+9)[0]
    print("\ndecoded from the CAP instructions (what the mod computes at runtime):")
    print("   &gGameEngine       rva 0x%x" % gg)
    print("   activeRagdolls off +0x%x   (expect 0x37690)" % off)
    print("   stock cap imm8     %d          (expect 5)" % body[i+13])
    print("   jge at             +0x%x -> rel8 %d" % (i+14, struct.unpack_from('<b', body, i+15)[0]))
    print("   bytes              %s" % body[i:i+16].hex())
    if off != 0x37690 or body[i+13] != 5: print("  !! unexpected decode"); ok = False
    # sanity: the global should land in .data
    for nm, va, vs, pr, sr in [(s.Name.rstrip(b'\x00').decode(), s.VirtualAddress, s.Misc_VirtualSize, s.PointerToRawData, s.SizeOfRawData) for s in pe.sections]:
        if va <= gg < va+max(vs,sr): print("   global lives in    %s" % nm)
if flag:
    i = flag[0]
    print("\nFLAG site bytes      %s  (cmp byte [rdi+0x286C],0 ; je)" % body[i:i+9].hex())
print("\n%s" % ("PRE-FLIGHT OK" if ok else "PRE-FLIGHT FAILED"))
sys.exit(0 if ok else 1)
