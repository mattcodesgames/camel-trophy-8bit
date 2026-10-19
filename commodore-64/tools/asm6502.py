"""Minimal two-pass 6502 assembler (documented opcodes only).

Syntax: `label:`, `NAME = expr`, `.org expr`, `.byte expr, ...`, `.word expr, ...`,
`.scr "TEXT"` (C64 screen codes), comments with `;`. Expressions are Python
expressions over labels, with `$hex`, `%bin`, `<expr` (low byte) and `>expr` (high byte).
"""
import re

OPS = {
    'adc': dict(imm=0x69, zp=0x65, zpx=0x75, abs=0x6D, absx=0x7D, absy=0x79, indx=0x61, indy=0x71),
    'and': dict(imm=0x29, zp=0x25, zpx=0x35, abs=0x2D, absx=0x3D, absy=0x39, indx=0x21, indy=0x31),
    'asl': dict(acc=0x0A, zp=0x06, zpx=0x16, abs=0x0E, absx=0x1E),
    'bit': dict(zp=0x24, abs=0x2C),
    'cmp': dict(imm=0xC9, zp=0xC5, zpx=0xD5, abs=0xCD, absx=0xDD, absy=0xD9, indx=0xC1, indy=0xD1),
    'cpx': dict(imm=0xE0, zp=0xE4, abs=0xEC),
    'cpy': dict(imm=0xC0, zp=0xC4, abs=0xCC),
    'dec': dict(zp=0xC6, zpx=0xD6, abs=0xCE, absx=0xDE),
    'eor': dict(imm=0x49, zp=0x45, zpx=0x55, abs=0x4D, absx=0x5D, absy=0x59, indx=0x41, indy=0x51),
    'inc': dict(zp=0xE6, zpx=0xF6, abs=0xEE, absx=0xFE),
    'jmp': dict(abs=0x4C, ind=0x6C),
    'jsr': dict(abs=0x20),
    'lda': dict(imm=0xA9, zp=0xA5, zpx=0xB5, abs=0xAD, absx=0xBD, absy=0xB9, indx=0xA1, indy=0xB1),
    'ldx': dict(imm=0xA2, zp=0xA6, zpy=0xB6, abs=0xAE, absy=0xBE),
    'ldy': dict(imm=0xA0, zp=0xA4, zpx=0xB4, abs=0xAC, absx=0xBC),
    'lsr': dict(acc=0x4A, zp=0x46, zpx=0x56, abs=0x4E, absx=0x5E),
    'ora': dict(imm=0x09, zp=0x05, zpx=0x15, abs=0x0D, absx=0x1D, absy=0x19, indx=0x01, indy=0x11),
    'rol': dict(acc=0x2A, zp=0x26, zpx=0x36, abs=0x2E, absx=0x3E),
    'ror': dict(acc=0x6A, zp=0x66, zpx=0x76, abs=0x6E, absx=0x7E),
    'sbc': dict(imm=0xE9, zp=0xE5, zpx=0xF5, abs=0xED, absx=0xFD, absy=0xF9, indx=0xE1, indy=0xF1),
    'sta': dict(zp=0x85, zpx=0x95, abs=0x8D, absx=0x9D, absy=0x99, indx=0x81, indy=0x91),
    'stx': dict(zp=0x86, zpy=0x96, abs=0x8E),
    'sty': dict(zp=0x84, zpx=0x94, abs=0x8C),
}
IMPLIED = dict(brk=0x00, clc=0x18, cld=0xD8, cli=0x58, clv=0xB8, dex=0xCA, dey=0x88, inx=0xE8, iny=0xC8,
               nop=0xEA, pha=0x48, php=0x08, pla=0x68, plp=0x28, rti=0x40, rts=0x60, sec=0x38, sed=0xF8,
               sei=0x78, tax=0xAA, tay=0xA8, tsx=0xBA, txa=0x8A, txs=0x9A, tya=0x98)
BRANCH = dict(bcc=0x90, bcs=0xB0, beq=0xF0, bmi=0x30, bne=0xD0, bpl=0x10, bvc=0x50, bvs=0x70)
SIZE = dict(imm=2, zp=2, zpx=2, zpy=2, indx=2, indy=2, abs=3, absx=3, absy=3, ind=3, acc=1)


def screencode(ch):
    o = ord(ch)
    if 'A' <= ch <= 'Z':
        return o - 64
    if ch == '@':
        return 0
    if 32 <= o < 64:
        return o
    if ch in '[]':
        return {'[': 27, ']': 29}[ch]
    raise ValueError('no screen code for %r' % ch)


class AsmError(Exception):
    pass


def assemble(source, defines=None):
    """Returns (origin, bytes, symbols)."""
    lines = source.splitlines()
    syms = dict(defines or {})
    for pas in (1, 2):
        pc = org = None
        out = bytearray()

        def ev(expr, need=True):
            e = expr.strip()
            e = re.sub(r'\$([0-9a-fA-F]+)', r'0x\1', e)
            e = re.sub(r'%([01]+)', r'0b\1', e)
            lo_hi = None
            if e.startswith('<'):
                lo_hi, e = 'lo', e[1:]
            elif e.startswith('>'):
                lo_hi, e = 'hi', e[1:]
            try:
                v = eval(e, {'__builtins__': {}}, syms)
            except NameError:
                if pas == 2 or need:
                    if pas == 2:
                        raise AsmError('undefined symbol in %r' % expr)
                return None
            v = int(v)
            if lo_hi == 'lo':
                v &= 0xFF
            elif lo_hi == 'hi':
                v = (v >> 8) & 0xFF
            return v

        def emit(*bs):
            nonlocal pc
            for b in bs:
                out.append(b & 0xFF)
            pc += len(bs)

        for ln, raw in enumerate(lines, 1):
            line = raw.split(';', 1)[0] if '.scr' not in raw else raw
            if '.scr' in line:
                # keep semicolons inside strings
                m = re.match(r'(.*?\.scr\s+"[^"]*")', line)
                line = m.group(1) if m else line
            line = line.strip()
            if not line:
                continue
            try:
                m = re.match(r'^([A-Za-z_]\w*)\s*=\s*(.+)$', line)
                if m:
                    v = ev(m.group(2), need=False)
                    if v is not None:
                        syms[m.group(1)] = v
                    continue
                m = re.match(r'^([A-Za-z_]\w*):\s*(.*)$', line)
                if m:
                    if pc is None:
                        raise AsmError('label before .org')
                    if pas == 1 and m.group(1) in syms:
                        raise AsmError('duplicate label ' + m.group(1))
                    syms[m.group(1)] = pc
                    line = m.group(2).strip()
                    if not line:
                        continue
                parts = line.split(None, 1)
                op = parts[0].lower()
                arg = parts[1].strip() if len(parts) > 1 else ''
                if op == '.org':
                    v = ev(arg)
                    if org is None:
                        org = pc = v
                    else:
                        if v < pc:
                            raise AsmError('.org goes backwards')
                        emit(*([0] * (v - pc)))
                    continue
                if op == '.byte':
                    for a in split_args(arg):
                        v = ev(a)
                        emit(0 if v is None else v)
                    continue
                if op == '.word':
                    for a in split_args(arg):
                        v = ev(a) or 0
                        emit(v & 0xFF, v >> 8)
                    continue
                if op == '.scr':
                    s = re.match(r'"([^"]*)"', arg).group(1)
                    emit(*[screencode(c) for c in s])
                    continue
                if op == '.align':
                    n = ev(arg)
                    while pc % n:
                        emit(0)
                    continue
                if op in IMPLIED:
                    if arg and arg.lower() != 'a':
                        raise AsmError('unexpected operand')
                    emit(IMPLIED[op])
                    continue
                if op in BRANCH:
                    t = ev(arg, need=False)
                    off = 0 if t is None else t - (pc + 2)
                    if pas == 2 and not -128 <= off <= 127:
                        raise AsmError('branch out of range')
                    emit(BRANCH[op], off & 0xFF)
                    continue
                if op not in OPS:
                    raise AsmError('unknown mnemonic ' + op)
                modes = OPS[op]
                a = arg.replace(' ', '')
                al = a.lower()
                if al in ('', 'a'):
                    mode, v = 'acc', None
                elif a.startswith('#'):
                    mode, v = 'imm', ev(a[1:], need=False)
                elif al.endswith('),y'):
                    mode, v = 'indy', ev(a[1:-3], need=False)
                elif al.endswith(',x)'):
                    mode, v = 'indx', ev(a[1:-3], need=False)
                elif a.startswith('(') and a.endswith(')') and op == 'jmp':
                    mode, v = 'ind', ev(a[1:-1], need=False)
                else:
                    idx = ''
                    if al.endswith(',x') or al.endswith(',y'):
                        idx, a = al[-1], a[:-2]
                    v = ev(a, need=False)
                    zp_ok = v is not None and v < 256 and syms.get('__force_abs__') is None
                    zmode, amode = 'zp' + idx, 'abs' + idx
                    if zp_ok and zmode in modes and not (pas == 2 and _was_abs(syms, ln)):
                        mode = zmode
                    else:
                        mode = amode
                        if pas == 1 and v is None:
                            syms.setdefault('__abs_lines__', set()).add(ln)
                if mode not in modes:
                    raise AsmError('bad addressing mode %s for %s' % (mode, op))
                v = 0 if v is None else v
                if SIZE[mode] == 1:
                    emit(modes[mode])
                elif SIZE[mode] == 2:
                    if not 0 <= v <= 255:
                        if pas == 2:
                            raise AsmError('operand out of range')
                    emit(modes[mode], v)
                else:
                    emit(modes[mode], v & 0xFF, (v >> 8) & 0xFF)
            except AsmError as e:
                raise AsmError('line %d: %s\n  %s' % (ln, e, raw))
    syms.pop('__abs_lines__', None)
    return org, bytes(out), syms


def _was_abs(syms, ln):
    return ln in syms.get('__abs_lines__', ())


def split_args(s):
    return [a for a in (x.strip() for x in s.split(',')) if a]
