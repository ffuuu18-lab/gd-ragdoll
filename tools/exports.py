#!/usr/bin/env python3
"""Parse a PE export table manually (no pefile symbol cap). Usage: exports.py <dll> [substr...]"""
import sys, struct, pefile
path=sys.argv[1]; pats=[p.encode() for p in sys.argv[2:]]
pe=pefile.PE(path, fast_load=True)
data=open(path,'rb').read()
secs=[(s.VirtualAddress, s.Misc_VirtualSize, s.PointerToRawData, s.SizeOfRawData) for s in pe.sections]
def r2o(rva):
    for va,vs,pr,sr in secs:
        if va <= rva < va+max(vs,sr): return pr+(rva-va)
d=pe.OPTIONAL_HEADER.DATA_DIRECTORY[0]
o=r2o(d.VirtualAddress)
(chars,ts,mj,mn,nameRva,ordBase,nFunc,nNames,addrFuncs,addrNames,addrOrds)=struct.unpack_from('<IIHHIIIIIII',data,o)
print('# %s  functions=%d names=%d ordBase=%d' % (__import__('os').path.basename(path), nFunc, nNames, ordBase))
of=r2o(addrFuncs); on=r2o(addrNames); oo=r2o(addrOrds)
out=[]
for i in range(nNames):
    nr=struct.unpack_from('<I',data,on+4*i)[0]
    no=r2o(nr)
    e=data.find(b'\x00',no)
    nm=data[no:e]
    ordi=struct.unpack_from('<H',data,oo+2*i)[0]
    frva=struct.unpack_from('<I',data,of+4*ordi)[0]
    out.append((nm,ordi+ordBase,frva))
print('# parsed %d named exports' % len(out))
for nm,ordi,frva in out:
    if not pats or any(p in nm for p in pats):
        print('0x%08x  ord=%-6d %s' % (frva, ordi, nm.decode('latin-1')))
