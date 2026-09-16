#!/usr/bin/env python3
"""Disassemble a RVA range of a PE."""
import sys, pefile
from capstone import *
from capstone.x86 import *
path, start, length = sys.argv[1], int(sys.argv[2],16), int(sys.argv[3],16)
pe = pefile.PE(path, fast_load=True); base = pe.OPTIONAL_HEADER.ImageBase
data = open(path,'rb').read()
secs=[(s.Name.rstrip(b'\x00').decode('latin-1'), s.VirtualAddress, s.Misc_VirtualSize, s.PointerToRawData, s.SizeOfRawData) for s in pe.sections]
def rva2off(rva):
    for n,va,vs,pr,sr in secs:
        if va <= rva < va+max(vs,sr): return pr+(rva-va)
def getstr(rva):
    o = rva2off(rva)
    if o is None: return None
    e = data.find(b'\x00', o, o+120)
    if e<0: return None
    s = data[o:e]
    try: t=s.decode('ascii')
    except: return None
    return t if t and all(32<=c<127 for c in s) else None
off = rva2off(start)
md = Cs(CS_ARCH_X86, CS_MODE_64); md.detail=True
for insn in md.disasm(data[off:off+length], base+start):
    ann=""
    for op in insn.operands:
        if op.type==X86_OP_MEM and op.mem.base==X86_REG_RIP:
            t = insn.address+insn.size+op.mem.disp-base
            s = getstr(t)
            ann = "   ; rva 0x%x%s" % (t, (' "%s"'%s) if s else '')
    if insn.mnemonic=='call':
        try: ann = "   ; -> rva 0x%x" % (int(insn.op_str,16)-base)
        except: pass
    print("  0x%08x  %-24s %-44s%s" % (insn.address-base, insn.bytes.hex(), insn.mnemonic+" "+insn.op_str, ann))
