"""Dump each game part as a 64K memory image plus a BASIC listing, for translation work."""
import os, sys
sys.path.insert(0, os.path.dirname(__file__))
from zxtape import read_tzx, w16, parse_basic, detok

SYS = {'VARS': 0x5C4B, 'PROG': 0x5C53, 'NXTLIN': 0x5C55, 'DATADD': 0x5C57, 'E_LINE': 0x5C59, 'K_CUR': 0x5C5B,
       'CH_ADD': 0x5C5D, 'X_PTR': 0x5C5F, 'WORKSP': 0x5C61, 'STKBOT': 0x5C63, 'STKEND': 0x5C65, 'RAMTOP': 0x5CB2,
       'ERR_SP': 0x5C3D, 'CHARS': 0x5C36, 'UDG': 0x5C7B, 'PPC': 0x5C45, 'NEWPPC': 0x5C42}

d, blocks = read_tzx(sys.argv[1])
out = sys.argv[2]
os.makedirs(out, exist_ok=True)
turbo = [b for b in blocks if b['id'] == 0x11]
for k, b in enumerate(turbo, 1):
    data = d[b['data_off']:b['data_off'] + b['data_len']]
    m = bytearray(65536)
    m[0x4000:0x4000 + len(data) - 2] = data[1:-1]
    open(f'{out}/part{k}.bin', 'wb').write(m)
    print(f'part{k}: flag {data[0]:02x} len {len(data)} loads 4000-{0x4000 + len(data) - 3:04x}')
    print('   ', ' '.join(f'{n}={w16(m, a):04x}' for n, a in SYS.items()))
    prog, vars_ = w16(m, 0x5C53), w16(m, 0x5C4B)
    lines, end = parse_basic(m, prog, vars_)
    with open(f'{out}/part{k}.bas', 'w') as f:
        for no, body in lines:
            f.write(f'{no} {detok(body)}\n')
    print(f'    {len(lines)} lines, program {prog:04x}-{end:04x}')
