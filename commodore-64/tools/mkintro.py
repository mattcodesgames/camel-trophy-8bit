#!/usr/bin/env python3
"""Replace the CFB-Soft cracker intro with a new title/credits screen.

Reads assets/CamelTrophy-original.d64 and writes web/camel.d64 containing one PRG:

  $0801-$AE0B  the original crunched program, byte-for-byte, except:
                 - the BASIC line now says SYS 49152 (our intro) instead of SYS 2088
                 - the depacker's final JMP $4000 (CFB intro) now jumps to $033C
  $C000-       the new intro (6502, assembled by tools/asm6502.py)

Flow: intro runs first (bobbing pixel-art Camel Trophy badge from assets/logo.txt, shown at full
C64 resolution by multiplexing 8 sprites over 4 rows; credits, music, raster bars). On fire/space it
shows a loading message, installs the "move game + RUN" routine at $033C (the same job the CFB
intro's exit code did) and starts the original depacker at $0828.
"""
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..')
sys.path.insert(0, HERE)
from asm6502 import assemble  # noqa: E402

ORIG_D64 = os.path.join(ROOT, 'assets', 'CamelTrophy-original.d64')
OUT_D64 = os.path.join(ROOT, 'web', 'camel.d64')

# ------------------------------------------------------------------ D64 helpers
SPT = [0] + [21] * 17 + [19] * 7 + [18] * 6 + [17] * 5


def d64_off(t, s):
    return (sum(SPT[1:t]) + s) * 256


def d64_read_first_prg(d):
    t, s = 18, 1
    while t:
        sec = d[d64_off(t, s):d64_off(t, s) + 256]
        for e in range(8):
            ent = sec[e * 32:e * 32 + 32]
            if ent[2] & 7 == 2:
                data, ft, fs = b'', ent[3], ent[4]
                while ft:
                    b = d[d64_off(ft, fs):d64_off(ft, fs) + 256]
                    data += b[2:b[1] + 1] if b[0] == 0 else b[2:]
                    ft, fs = b[0], b[1]
                return ent[5:21].rstrip(b'\xa0'), data
        t, s = sec[0], sec[1]
    raise SystemExit('no PRG on disk')


def d64_write(prg, filename, diskname, diskid):
    img = bytearray(174848)
    free = {t: set(range(SPT[t])) for t in range(1, 36)}
    free[18] -= {0, 1}
    chunks = [prg[i:i + 254] for i in range(0, len(prg), 254)]
    secs = []
    for t in list(range(17, 0, -1)) + list(range(19, 36)):
        for s in range(SPT[t]):
            if len(secs) < len(chunks):
                secs.append((t, s))
                free[t].discard(s)
    for i, c in enumerate(chunks):
        o = d64_off(*secs[i])
        if i + 1 < len(chunks):
            img[o], img[o + 1] = secs[i + 1]
        else:
            img[o], img[o + 1] = 0, len(c) + 1
        img[o + 2:o + 2 + len(c)] = c
    pad = lambda b, n: b[:n] + b'\xa0' * (n - len(b[:n]))
    bam = d64_off(18, 0)
    img[bam:bam + 4] = bytes([18, 1, 0x41, 0])
    for t in range(1, 36):
        bits = sum(1 << s for s in free[t])
        img[bam + 4 * t:bam + 4 * t + 4] = bytes([len(free[t]), bits & 255, (bits >> 8) & 255, bits >> 16])
    img[bam + 0x90:bam + 0xA0] = pad(diskname, 16)
    img[bam + 0xA0:bam + 0xAB] = b'\xa0\xa0' + diskid + b'\xa02A\xa0\xa0\xa0\xa0'
    d = d64_off(18, 1)
    img[d:d + 2] = bytes([0, 0xFF])
    img[d + 2] = 0x82
    img[d + 3], img[d + 4] = secs[0]
    img[d + 5:d + 21] = pad(filename, 16)
    img[d + 30], img[d + 31] = len(chunks) & 255, len(chunks) >> 8
    return bytes(img)


# ------------------------------------------------------------------ logo pixel art
LOGO_TXT = os.path.join(ROOT, 'assets', 'logo.txt')   # 192x84, made by tools/logo_from_image.py
LOGO_COLS, LOGO_ROWS = 8, 4                            # 8 sprites wide, 4 multiplexed sprite rows
SPRITE_SRC = 0xB000                                    # stored here in the PRG, copied to $E800 at start


def build_logo():
    pix = [l.rstrip('\n') for l in open(LOGO_TXT) if l.strip()]
    assert len(pix) == 21 * LOGO_ROWS and all(len(r) == 24 * LOGO_COLS for r in pix), 'logo.txt must be 192x84'
    data = bytearray()
    for r in range(LOGO_ROWS):
        for c in range(LOGO_COLS):
            for y in range(21):
                row = pix[r * 21 + y][c * 24:c * 24 + 24]
                for k in range(3):
                    data.append(int(row[k * 8:k * 8 + 8].replace('.', '0').replace('#', '1'), 2))
            data.append(0)
    return bytes(data)


# ------------------------------------------------------------------ music
def note_freq(name):
    names = {'C': 0, 'C#': 1, 'D': 2, 'D#': 3, 'E': 4, 'F': 5, 'F#': 6, 'G': 7, 'G#': 8, 'A': 9, 'A#': 10, 'B': 11}
    n, octv = name[:-1], int(name[-1])
    midi = 12 * (octv + 1) + names[n]
    hz = 440 * 2 ** ((midi - 69) / 12)
    return round(hz * 16777216 / 985248)


LEAD = ('A4 C5 E5 A5 E5 C5 B4 C5  F4 A4 C5 F5 C5 A4 G4 A4 '
        'C5 E5 G5 C6 G5 E5 D5 E5  G4 B4 D5 G5 D5 B4 A4 B4').split()
BASS = ('A2 A2 A3 A2 A2 A3 A2 E2  F2 F2 F3 F2 F2 F3 F2 C3 '
        'C3 C3 C4 C3 C3 C4 C3 G2  G2 G2 G3 G2 G2 G3 B2 D3').split()

# ------------------------------------------------------------------ intro program
TEXTS = [
    # screen rows: FLD hides the badlines under the logo, so row 0 appears at raster line 155
    (0, 7, 'THE CAMEL TROPHY VIDEO GAME'),
    (2, 1, 'ORIGINAL GAME (C) 1985 BY TEQUILA'),
    (4, 15, 'REBUILT FOR THE WEB BY'),
    (5, 3, '@MATTCODESGAMES & CLAUDE (ANTHROPIC)'),
    (10, 12, 'RUNNING ON A C64 EMULATED IN WEBASSEMBLY'),
]
PROMPT_ROW, PROMPT = 8, 'PRESS FIRE OR SPACE TO CONTINUE'
TEXTS.append((PROMPT_ROW, 1, PROMPT))  # colours animated by the main loop
LOADING = [(11, 'LOADING CAMEL TROPHY...'), (13, 'PLEASE WAIT')]

SOURCE = r"""
; VIC bank 3: text screen A at $CC00, blank screen B at $C800, charset copy at $E000,
; logo sprites at $E800 (pointers $A0-$BF). 8 sprites are reused for 4 rows; at each row
; boundary the display flips between screens A and B, whose sprite pointers are rewritten
; while the other one is showing.
SCREEN  = $CC00
SCREENB = $C800
D018A   = $38
D018B   = $28
FRAME   = $F7
STEP    = $F8
SONGPOS = $F9
ARMED   = $FA
COL     = $02
EVIDX   = $92
EVLINE  = $93
TOP     = $94
EVD018  = $95
FLDEND  = 150           ; no badlines above this line (FLD); text row 0 starts at line 155
SPEED   = 7

        .org $C000
start:
        sei
        cld
        lda #<ORIG_END          ; the CFB depacker reads the program end from $AE/$AF
        sta $AE
        lda #>ORIG_END
        sta $AF
        lda #$0B                ; screen off while we set up
        sta $D011

        lda #$33                ; copy the character ROM to RAM at $E000 (bank 3 can't see the ROM)
        sta $01
        ldx #0
copych: lda $D000,x
        sta $E000,x
        lda $D100,x
        sta $E100,x
        lda $D200,x
        sta $E200,x
        lda $D300,x
        sta $E300,x
        lda $D400,x
        sta $E400,x
        lda $D500,x
        sta $E500,x
        lda $D600,x
        sta $E600,x
        lda $D700,x
        sta $E700,x
        inx
        bne copych
        lda #$36                ; logo sprites: RAM under BASIC ROM -> $E800
        sta $01
        ldx #0
copysp: lda SPRITE_SRC,x
        sta $E800,x
        lda SPRITE_SRC+$100,x
        sta $E900,x
        lda SPRITE_SRC+$200,x
        sta $EA00,x
        lda SPRITE_SRC+$300,x
        sta $EB00,x
        lda SPRITE_SRC+$400,x
        sta $EC00,x
        lda SPRITE_SRC+$500,x
        sta $ED00,x
        lda SPRITE_SRC+$600,x
        sta $EE00,x
        lda SPRITE_SRC+$700,x
        sta $EF00,x
        inx
        bne copysp
        lda #$37
        sta $01

        lda $DD02
        ora #3
        sta $DD02
        lda $DD00
        and #$FC
        sta $DD00
        lda #$38
        sta $D018
        lda #$08
        sta $D016
        lda #0
        sta $D020
        lda #0
        sta $D021

        ldx #0
clr:    lda #32
        sta SCREEN,x
        sta SCREEN+$100,x
        sta SCREEN+$200,x
        sta SCREEN+$2E8,x
        sta SCREENB,x
        sta SCREENB+$100,x
        sta SCREENB+$200,x
        sta SCREENB+$2E8,x
        lda #14
        sta $D800,x
        sta $D900,x
        sta $DA00,x
        sta $DAE8,x
        inx
        bne clr

        lda #<texts
        sta $FB
        lda #>texts
        sta $FC
        jsr print

        jsr resetptrs           ; sprites: pointers, colour, X positions
        ldx #7
spr:    lda #LOGOCOL
        sta $D027,x
        dex
        bpl spr
        ldx #0
        ldy #0
sprx:   lda sprxtab,y
        sta $D000,x
        inx
        inx
        iny
        cpy #8
        bne sprx
        lda #$80                ; sprite 7 sits at X=256
        sta $D010
        lda #0
        sta $D01C
        sta $D01B
        sta $D017
        sta $D01D
        lda #$FF
        sta $D015
        jsr newframe

        ldx #$18                ; SID: clear, then set up lead (pulse) and bass (triangle)
        lda #0
sidclr: sta $D400,x
        dex
        bpl sidclr
        lda #$08
        sta $D403
        lda #$09
        sta $D405
        lda #$0A
        sta $D40C
        lda #$0F
        sta $D418

        lda #0
        sta FRAME
        sta STEP
        sta SONGPOS
        sta ARMED
        lda #$1B
        sta $D011

main:
w46:    lda $D011               ; (line 302 has the same low byte as line 46)
        bmi w46
        lda $D012
        cmp #46
        bne w46
        lda #$1D                ; FLD from here: (46+7)&7 = 5
        sta $D011
w50:    lda $D012
        cmp #50
        bne w50
        ldx FRAME
bar:    lda $D012               ; once per raster line, lines 51..249
wl:     cmp $D012
        beq wl
        lda $D012
        cmp EVLINE              ; a line before a sprite-row boundary? wait for it exactly,
        bcc noev                ; flip screens first thing, then do the slow work
        lda EVD018
        ldx EVLINE
        inx
evw:    cpx $D012
        beq evgo
        bcs evw
evgo:   sta $D018
        jsr event
noev:   jsr fld
        lda bartab,x            ; raster bars in the side borders
        sta $D020
        inx
        lda $D012
        cmp #250
        bcc bar
        lda #0
        sta $D020

        inc FRAME
        jsr newframe

        lda FRAME               ; shimmer the "press fire" line
        lsr
        sta COL
        ldx #39
shim:   txa
        clc
        adc COL
        and #15
        tay
        lda cycle,y
        sta $D800+PROMPT_ROW*40,x
        dex
        bpl shim

        dec STEP
        bpl nomus
        lda #SPEED
        sta STEP
        jsr music
nomus:
        lda #$FF                ; fire on either joystick, or any key
        sta $DC00
        lda $DC00
        and #$10
        beq pressed
        lda $DC01
        and #$10
        beq pressed
        lda #0
        sta $DC00
        lda $DC01
        ldx #$FF
        stx $DC00
        cmp #$FF
        bne pressed
        lda #1
        sta ARMED
        jmp main
pressed:
        lda ARMED
        bne leave
        jmp main

leave:
        lda #$0B                ; blank the display while switching screens
        sta $D011
        lda #0
        sta $D015
        sta $D017
        sta $D01D
        sta $D418
        sta $D404
        sta $D40B
        sta $D020
        sta $D021
        lda $DD00
        ora #3
        sta $DD00
        lda #$15
        sta $D018
        lda #$C8
        sta $D016
        lda #0
        sta $D010
        ldx #0
clr2:   lda #32
        sta $0400,x
        sta $0500,x
        sta $0600,x
        sta $06E8,x
        lda #1
        sta $D800,x
        sta $D900,x
        sta $DA00,x
        sta $DAE8,x
        inx
        bne clr2
        lda #<loadtexts
        sta $FB
        lda #>loadtexts
        sta $FC
        jsr print0400

        ldx #EXITLEN-1          ; install the "move game into place and RUN" routine
cpyex:  lda exitcode,x
        sta $033C,x
        dex
        bpl cpyex
        lda #$7F
        sta $DC00
        lda #$1B
        sta $D011
        jmp $0828               ; original CFB depacker; it ends with JMP $033C (patched)

; bob: logo top line for this frame, all sprites back on row 0
newframe:
        lda FRAME
        and #63
        tax
        lda sintab,x
        sta TOP
        jsr sety
        lda #0
        sta EVIDX
        lda TOP
        clc
        adc evoff
        sta EVLINE
        lda evd018
        sta EVD018
        rts

; sprite-row events, each fired on the raster line TOP+evoff[n] (screen flip already done)
event:  jsr fld                 ; keep FLD ahead while we work
        ldy EVIDX
        bne ne0
        jmp ev0
ne0:    cpy #1
        bne ne1
        jmp ev1
ne1:    cpy #2
        bne ne2
        jmp ev2
ne2:    cpy #3
        bne ev4
        jmp evnext
ev4:
        lda #$A0
        sta SCREEN+$3F8+0
        lda #$A1
        sta SCREEN+$3F8+1
        lda #$A2
        sta SCREEN+$3F8+2
        lda #$A3
        sta SCREEN+$3F8+3
        lda #$A4
        sta SCREEN+$3F8+4
        lda #$A5
        sta SCREEN+$3F8+5
        lda #$A6
        sta SCREEN+$3F8+6
        lda #$A7
        sta SCREEN+$3F8+7
        lda #$A8
        sta SCREENB+$3F8+0
        lda #$A9
        sta SCREENB+$3F8+1
        lda #$AA
        sta SCREENB+$3F8+2
        lda #$AB
        sta SCREENB+$3F8+3
        lda #$AC
        sta SCREENB+$3F8+4
        lda #$AD
        sta SCREENB+$3F8+5
        lda #$AE
        sta SCREENB+$3F8+6
        lda #$AF
        sta SCREENB+$3F8+7
        lda #$FF                ; ev4: logo done, pointers reset for next frame
        sta EVLINE
        rts
ev0:    lda TOP                 ; row 0 is showing: arm row 1
        clc
        adc #21
        jsr sety
        jmp evnext
ev1:    lda TOP                 ; row 1 starts on screen B: arm row 2 on screen A
        clc
        adc #42
        jsr sety
        lda #$B0
        sta SCREEN+$3F8+0
        lda #$B1
        sta SCREEN+$3F8+1
        lda #$B2
        sta SCREEN+$3F8+2
        lda #$B3
        sta SCREEN+$3F8+3
        lda #$B4
        sta SCREEN+$3F8+4
        lda #$B5
        sta SCREEN+$3F8+5
        lda #$B6
        sta SCREEN+$3F8+6
        lda #$B7
        sta SCREEN+$3F8+7
        jmp evnext
ev2:    lda TOP                 ; row 2 starts on screen A: arm row 3 on screen B
        clc
        adc #63
        jsr sety
        lda #$B8
        sta SCREENB+$3F8+0
        lda #$B9
        sta SCREENB+$3F8+1
        lda #$BA
        sta SCREENB+$3F8+2
        lda #$BB
        sta SCREENB+$3F8+3
        lda #$BC
        sta SCREENB+$3F8+4
        lda #$BD
        sta SCREENB+$3F8+5
        lda #$BE
        sta SCREENB+$3F8+6
        lda #$BF
        sta SCREENB+$3F8+7
evnext: inc EVIDX
        ldy EVIDX
        lda TOP
        clc
        adc evoff,y
        sta EVLINE
        lda evd018,y
        sta EVD018
        jmp fld

; FLD: yscroll = (line+7)&7 can't match any of the next 7 lines, so no badlines (and no
; text rows) start under the logo; from FLDEND on, normal yscroll 3 resumes
fld:    lda $D012
        cmp #FLDEND
        bcs fldoff
        clc
        adc #7
        and #7
        ora #$18
        sta $D011
        rts
fldoff: lda #$1B
        sta $D011
        rts

sety:   sta $D001
        sta $D003
        sta $D005
        sta $D007
        sta $D009
        sta $D00B
        sta $D00D
        sta $D00F
        rts

resetptrs:                      ; screen A: row 0 ($A0-$A7), screen B: row 1 ($A8-$AF)
        lda #$A0
        sta SCREEN+$3F8+0
        lda #$A1
        sta SCREEN+$3F8+1
        lda #$A2
        sta SCREEN+$3F8+2
        lda #$A3
        sta SCREEN+$3F8+3
        lda #$A4
        sta SCREEN+$3F8+4
        lda #$A5
        sta SCREEN+$3F8+5
        lda #$A6
        sta SCREEN+$3F8+6
        lda #$A7
        sta SCREEN+$3F8+7
        lda #$A8
        sta SCREENB+$3F8+0
        lda #$A9
        sta SCREENB+$3F8+1
        lda #$AA
        sta SCREENB+$3F8+2
        lda #$AB
        sta SCREENB+$3F8+3
        lda #$AC
        sta SCREENB+$3F8+4
        lda #$AD
        sta SCREENB+$3F8+5
        lda #$AE
        sta SCREENB+$3F8+6
        lda #$AF
        sta SCREENB+$3F8+7
        rts

music:
        ldx SONGPOS
        lda leadlo,x
        sta $D400
        lda leadhi,x
        sta $D401
        lda #$40
        sta $D404
        lda #$41
        sta $D404
        lda basslo,x
        sta $D407
        lda basshi,x
        sta $D408
        lda #$10
        sta $D40B
        lda #$11
        sta $D40B
        inx
        cpx #32
        bne mus1
        ldx #0
mus1:   stx SONGPOS
        rts

; print: table of [offset lo, offset hi, colour, screen codes..., $FF], ends with offset hi $FF
print:  lda #$CC
        sta $F4
        jmp prt
print0400:
        lda #$04
        sta $F4
prt:    ldy #0
        lda ($FB),y
        sta $FD
        sta $F5
        iny
        lda ($FB),y
        cmp #$FF
        beq prdone
        tax
        clc
        adc $F4
        sta $FE
        txa
        clc
        adc #$D8
        sta $F6
        iny
        lda ($FB),y
        sta COL
        lda $FB
        clc
        adc #3
        sta $FB
        bcc prt1
        inc $FC
prt1:   ldy #0
prc:    lda ($FB),y
        cmp #$FF
        beq prend
        sta ($FD),y
        lda COL
        sta ($F5),y
        iny
        bne prc
prend:  iny
        tya
        clc
        adc $FB
        sta $FB
        bcc prt
        inc $FC
        jmp prt
prdone: rts

exitcode:
        .byte $78                               ; sei
        .byte $20,$84,$FF, $20,$81,$FF          ; jsr IOINIT, jsr CINT
        .byte $A9,$34,$85,$01                   ; RAM everywhere
        .byte $A9,$90,$A0,$42,$85,$FB,$84,$FC   ; from $4290
        .byte $A9,$01,$A0,$08,$85,$FD,$84,$FE   ; to $0801
        .byte $A2,$C0,$A0,$00                   ; $C0 pages
        .byte $B1,$FB,$91,$FD,$C8,$D0,$F9,$E6,$FC,$E6,$FE,$CA,$D0,$F2
        .byte $A9,$AA,$A0,$A0,$85,$2D,$85,$AE,$84,$2E,$84,$AF   ; end of BASIC program = $A0AA
        .byte $A9,$37,$85,$01
        .byte $20,$59,$A6                       ; jsr CLR
        .byte $4C,$AE,$A7                       ; jmp NEWSTT (RUN -> SYS 2080)
exitend:
EXITLEN = exitend - exitcode

evoff:    .byte 1, 21, 42, 63, 84    ; events fire on the line after these
evd018:   .byte D018A, D018B, D018A, D018B, D018A
sprxtab:  .byte 88, 112, 136, 160, 184, 208, 232, 0
cycle:    .byte 1, 1, 7, 7, 15, 12, 11, 12, 15, 7, 1, 1, 13, 3, 14, 3
"""


def build_intro(orig_end):
    sprites = build_logo()
    sin = [58 + round(6 * math.sin(2 * math.pi * i / 64)) for i in range(64)]
    barcycle = [0, 9, 2, 8, 10, 7, 1, 7, 10, 8, 2, 9, 0, 0, 0, 0]
    bars = (barcycle * 16)[:256]
    lead = [note_freq(n) for n in LEAD]
    bass = [note_freq(n) for n in BASS]

    def scr_bytes(text):
        out = []
        for c in text:
            o = ord(c)
            out.append(0 if c == '@' else o - 64 if 'A' <= c <= 'Z' else o)
        return out

    def table(entries):
        lines = []
        for row, colr, text in entries:
            off = row * 40 + (40 - len(text)) // 2
            lines.append('.byte %d, %d, %d, %s, $FF' % (off & 255, off >> 8, colr, ', '.join(map(str, scr_bytes(text)))))
        lines.append('.byte 0, $FF')
        return '\n'.join(lines)

    data = '\n'.join([
        'sintab: .byte ' + ', '.join(map(str, sin)),
        'bartab: .byte ' + ', '.join(map(str, bars)),
        'leadlo: .byte ' + ', '.join(str(f & 255) for f in lead),
        'leadhi: .byte ' + ', '.join(str(f >> 8) for f in lead),
        'basslo: .byte ' + ', '.join(str(f & 255) for f in bass),
        'basshi: .byte ' + ', '.join(str(f >> 8) for f in bass),
        'texts:',
        table(TEXTS),
        'loadtexts:',
        table([(r, 1, t) for r, t in LOADING]),
        'codeend:',
    ])
    src = SOURCE + data
    org, code, syms = assemble(src, {'ORIG_END': orig_end, 'PROMPT_ROW': PROMPT_ROW,
                                     'SPRITE_SRC': SPRITE_SRC, 'LOGOCOL': 7})
    assert org == 0xC000 and syms['codeend'] <= 0xC800, 'intro code overlaps screen B at $C800'
    return code, sprites, syms


def main():
    d = open(ORIG_D64, 'rb').read()
    name, prg = d64_read_first_prg(d)
    prg = bytearray(prg)
    load = prg[0] | prg[1] << 8
    end = load + len(prg) - 2
    assert load == 0x0801 and end == 0xAE0C, 'unexpected original program layout'

    # 1. BASIC line: SYS 49152 + new text, same length as the original line
    z = prg.index(0, 6)
    assert prg[6] == 0x9E
    newline = b'49152 CAMEL TROPHY WEB EDITION'
    room = z - 7
    prg[7:z] = newline[:room].ljust(room, b' ')
    # 2. depacker exit: JMP $4000 (CFB intro) -> JMP $033C (our installed routine)
    i = prg.index(bytes.fromhex('a9378501584c0040'))
    prg[i + 6:i + 8] = bytes([0x3C, 0x03])

    code, sprites, syms = build_intro(end)
    assert end <= SPRITE_SRC and SPRITE_SRC + len(sprites) <= 0xC000
    full = (bytearray(prg) + bytes(SPRITE_SRC - end) + sprites +
            bytes(0xC000 - SPRITE_SRC - len(sprites)) + code)
    out = d64_write(bytes(full), b'CAMEL TROPHY', b'CAMEL TROPHY', b'CT')
    open(OUT_D64, 'wb').write(out)
    print('intro $C000-$%04X, PRG $0801-$%04X, wrote %s' % (0xC000 + len(code), 0x0801 + len(full) - 2,
                                                           os.path.relpath(OUT_D64, ROOT)))


if __name__ == '__main__':
    main()
