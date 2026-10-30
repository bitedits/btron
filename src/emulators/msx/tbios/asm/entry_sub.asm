; ==============================================================================
; T-BIOS (B-MSX System BIOS) — Clean-Room Sub-ROM
; Target: MSX2 / MSX2+ / MSX turbo R Sub-ROM (16 KiB, Base 0x0000)
; Assembler: Pasmo Z80
; ==============================================================================

                include "hardware.inc"
                include "systemvars.inc"
                include "hooks.inc"

                org     $0000

; ==============================================================================
; Sub-ROM Header ($0000 - $000F)
; ==============================================================================
                db      "CD"
                dw      sub_init        ; Initialisation routine ($0002)
                dw      0               ; Statement handler ($0004)
                dw      0               ; Device handler ($0006)
                dw      0               ; Basic text ($0008)
                dw      0, 0, 0

; ==============================================================================
; Fixed Jump Vector Table ($0010 - $01FF)
; ==============================================================================

; $0010 CHRGTR
                ds      $0010 - $
                ld      a, (hl)
                or      a
                ret

; $0014 WRSLT
                ds      $0014 - $
                ret

; $0018
                ds      $0018 - $
                ret

; $001C CALSLT
                ds      $001C - $
                ret

; $0020 DCOMPR
                ds      $0020 - $
                ld      a, h
                cp      d
                ret     nz
                ld      a, l
                cp      e
                ret

; $0028
                ds      $0028 - $
                ret

; $0030 CALLF
                ds      $0030 - $
                ret

; $0038 KEYINT
                ds      $0038 - $
                ret

; $0066 NMI
                ds      $0066 - $
                retn

; $0085 DOGRPH: Draw line
                ds      $0085 - $
                jp      dogrph

; $0089 GRPPRT: Print character on graphic screen
                ds      $0089 - $
                jp      grpprt

; $008D SCALXY
                ds      $008D - $
                jp      scalxy

; $0091 MAPXYC
                ds      $0091 - $
                jp      mapxyc

; $0095 READC
                ds      $0095 - $
                jp      readc

; $0099 SETATR
                ds      $0099 - $
                jp      setatr

; $009D SETC
                ds      $009D - $
                jp      setc

; $00A1 TRIGHT
                ds      $00A1 - $
                ret

; $00A5 RIGHTC
                ds      $00A5 - $
                ret

; $00A9 TLEFTC
                ds      $00A9 - $
                ret

; $00AD LEFTC
                ds      $00AD - $
                ret

; $00B1 TDOWNC
                ds      $00B1 - $
                ret

; $00B5 DOWNC
                ds      $00B5 - $
                ret

; $00B9 TUPC
                ds      $00B9 - $
                ret

; $00BD UPC
                ds      $00BD - $
                ret

; $00C1 SCANR
                ds      $00C1 - $
                ret

; $00C5 SCANL
                ds      $00C5 - $
                ret

; $00C9 NVBXLN: Draw hollow box
                ds      $00C9 - $
                jp      nvbxln

; $00CD NVBXFL: Draw filled box
                ds      $00CD - $
                jp      nvbxfl

; $00D1 CHGMOD: Change screen mode
                ds      $00D1 - $
                jp      chgmod

; $00D5 INITXT: Set Screen 0
                ds      $00D5 - $
                jp      initxt

; $00D9 INIT32: Set Screen 1
                ds      $00D9 - $
                jp      init32

; $00DD INIGRP: Set Screen 2
                ds      $00DD - $
                jp      inigrp

; $00E1 INIMLT: Set Screen 3
                ds      $00E1 - $
                jp      inimlt

; $00E5 SETTXT
                ds      $00E5 - $
                jp      settxt

; $00E9 SETT32
                ds      $00E9 - $
                jp      sett32

; $00ED SETGRP
                ds      $00ED - $
                jp      setgrp

; $00F1 SETMLT
                ds      $00F1 - $
                jp      setmlt

; $00F5 CLRSPR: Clear sprites
                ds      $00F5 - $
                jp      clrspr

; $00F9 CALPAT
                ds      $00F9 - $
                ret

; $00FD CALATR
                ds      $00FD - $
                ret

; $0101 GSPSIZ
                ds      $0101 - $
                ret

; $0105 GETPAT
                ds      $0105 - $
                ret

; $0109 WRTVRM: Write byte to 128K VRAM
                ds      $0109 - $
                jp      nwrvrm

; $010D RDVRM: Read byte from 128K VRAM
                ds      $010D - $
                jp      nrdvrm

; $0111 CHGCLR: Change colors
                ds      $0111 - $
                jp      chgclr

; $0115 CLS: Clear screen
                ds      $0115 - $
                jp      cls

; $0119 CLRTXT
                ds      $0119 - $
                jp      clrtxt

; $012D WRTVDP: Write VDP register
                ds      $012D - $
                jp      wrtvdp

; $0131 VDPSTA: Read VDP status register
                ds      $0131 - $
                jp      vdpsta

; $013D SETPAG: Set display / active page
                ds      $013D - $
                jp      setpag

; $0141 INIPLT: Initialize palette
                ds      $0141 - $
                jp      iniplt

; $0145 RSTPLT: Reset palette to default
                ds      $0145 - $
                jp      rstplt

; $0149 GETPLT: Get palette entry
                ds      $0149 - $
                jp      getplt

; $014D SETPLT: Set palette entry
                ds      $014D - $
                jp      setplt

; $017D BEEP: Emit beep
                ds      $017D - $
                ret

; $0181 PROMPT
                ds      $0181 - $
                ret

; $0185 SDFSCR: Restore screen from RTC
                ds      $0185 - $
                jp      sdfscr

; $0189 SETSCR: Restore screen from RTC
                ds      $0189 - $
                jp      setscr

; $0191 BLTVV: Copy VRAM to VRAM (HMMM / YMMM)
                ds      $0191 - $
                jp      bltvv

; $0195 BLTVM: Copy Memory to VRAM (LMMC)
                ds      $0195 - $
                jp      bltvm

; $0199 BLTMV: Copy VRAM to Memory (LMCM)
                ds      $0199 - $
                jp      bltmv

; $01B5 CHGMDP: Set screen mode & reset palette
                ds      $01B5 - $
                jp      chgmdp

; $01F5 REDCLK: Read RTC register
                ds      $01F5 - $
                jp      redclk

; $01F9 WRTCLK: Write RTC register
                ds      $01F9 - $
                jp      wrtclk

; End of fixed vector area
                ds      $0200 - $

; ==============================================================================
; Sub-ROM Initialization (sub_init)
; ==============================================================================

sub_init:
                ; Reset palette to standard MSX2 colors
                call    rstplt
                ret

; ==============================================================================
; Palette Engine (INIPLT, RSTPLT, GETPLT, SETPLT)
; ==============================================================================

iniplt:
rstplt:
                push    bc
                push    hl
                ; Set VDP R#16 = 0 (palette write pointer)
                xor     a
                out     (VDP_ADDR), a
                ld      a, 16 | $80
                out     (VDP_ADDR), a

                ; Stream 32 bytes (16 pairs of (R<<4|B, G)) to Port $9A
                ld      hl, default_palette
                ld      b, 32
                ld      c, VDP_PALT
                otir
                pop     hl
                pop     bc
                ret

setplt:
                ; Input: A = Color index (0..15)
                ;        D = (Red << 4) | Blue
                ;        E = Green
                push    af
                out     (VDP_ADDR), a
                ld      a, 16 | $80
                out     (VDP_ADDR), a
                ld      a, d
                out     (VDP_PALT), a
                ld      a, e
                out     (VDP_PALT), a
                pop     af
                ret

getplt:
                ; Input: A = Color index (0..15)
                ; Output: D = (Red << 4) | Blue, E = Green
                and     $0F
                add     a, a            ; 2 bytes per color
                ld      e, a
                ld      d, 0
                ld      hl, default_palette
                add     hl, de
                ld      d, (hl)
                inc     hl
                ld      e, (hl)
                ret

default_palette:
                ; Format: (Red << 4) | Blue, Green (values 0..7)
                db      $00, $00        ; 0: Black
                db      $00, $00        ; 1: Black
                db      $11, $06        ; 2: Medium Green (R=1, G=6, B=1)
                db      $33, $07        ; 3: Light Green  (R=3, G=7, B=3)
                db      $17, $01        ; 4: Dark Blue    (R=1, G=1, B=7)
                db      $27, $03        ; 5: Light Blue   (R=2, G=3, B=7)
                db      $51, $01        ; 6: Dark Red     (R=5, G=1, B=1)
                db      $27, $06        ; 7: Cyan         (R=2, G=6, B=7)
                db      $71, $01        ; 8: Medium Red   (R=7, G=1, B=1)
                db      $73, $03        ; 9: Light Red    (R=7, G=3, B=3)
                db      $61, $06        ; 10: Dark Yellow (R=6, G=6, B=1)
                db      $64, $06        ; 11: Light Yellow(R=6, G=6, B=4)
                db      $11, $04        ; 12: Dark Green  (R=1, G=4, B=1)
                db      $65, $02        ; 13: Magenta     (R=6, G=2, B=5)
                db      $55, $05        ; 14: Gray        (R=5, G=5, B=5)
                db      $77, $07        ; 15: White       (R=7, G=7, B=7)

; ==============================================================================
; Screen Modes (CHGMOD, CHGMDP, Screen 0..8 & 10..12)
; ==============================================================================

chgmdp:
                call    rstplt
                ; fall through to chgmod

chgmod:
                ; Input: A = Screen mode number (0..8, 10..12)
                push    af
                call    disscr

                pop     af
                push    af
                ld      (SCRMOD), a
                ld      (OLDSCR), a

                cp      1
                jr      z, .mode_1
                cp      2
                jr      z, .mode_2
                cp      3
                jr      z, .mode_3
                cp      4
                jr      z, .mode_4
                cp      5
                jr      z, .mode_5
                cp      6
                jr      z, .mode_6
                cp      7
                jr      z, .mode_7
                cp      8
                jr      z, .mode_8
                cp      10
                jr      z, .mode_10
                cp      11
                jr      z, .mode_11
                cp      12
                jr      z, .mode_12

                ; Default to Screen 0
                call    initxt
                jr      .mode_done

.mode_1:
                call    init32
                jr      .mode_done
.mode_2:
                call    inigrp
                jr      .mode_done
.mode_3:
                call    inimlt
                jr      .mode_done
.mode_4:
                call    initg3
                jr      .mode_done
.mode_5:
                call    initg4
                jr      .mode_done
.mode_6:
                call    initg5
                jr      .mode_done
.mode_7:
                call    initg6
                jr      .mode_done
.mode_8:
                call    initg7
                jr      .mode_done
.mode_10:
.mode_12:
                call    init_yjk
                jr      .mode_done
.mode_11:
                call    init_yae
                jr      .mode_done

.mode_done:
                call    cls
                call    enascr
                pop     af
                ret

; Screen 3: Multicolor (64x48)
inimlt:
setmlt:
                ld      b, 0
                ld      c, 0
                call    wrtvdp
                ld      a, (RG1SAV)
                and     $43
                or      $A8             ; M2=1, 16K VRAM, IE0
                ld      b, a
                ld      c, 1
                call    wrtvdp
                ret

; Screen 4: Graphic 3 (256x192, 16x16 sprites mode 2)
initg3:
                ld      b, $04          ; M5=1 (Screen 4)
                ld      c, 0
                call    wrtvdp
                ld      a, (RG1SAV)
                and     $43
                or      $A0
                ld      b, a
                ld      c, 1
                call    wrtvdp
                ret

; Screen 5: Graphic 4 (256x212, 16 colors / 4 bpp, 4 pages)
initg4:
                ld      b, $06          ; M4=1, M3=1
                ld      c, 0
                call    wrtvdp
                ld      a, (RG1SAV)
                and     $43
                or      $A0
                ld      b, a
                ld      c, 1
                call    wrtvdp
                ld      b, $1F          ; Name table: $0000
                ld      c, 2
                call    wrtvdp
                ld      b, $08          ; 128K VRAM
                ld      c, 8
                call    wrtvdp
                ld      b, $80          ; 212 lines
                ld      c, 9
                call    wrtvdp
                ret

; Screen 6: Graphic 5 (512x212, 4 colors / 2 bpp, 4 pages)
initg5:
                ld      b, $08          ; M5=1
                ld      c, 0
                call    wrtvdp
                ld      a, (RG1SAV)
                and     $43
                or      $A0
                ld      b, a
                ld      c, 1
                call    wrtvdp
                ld      b, $1F
                ld      c, 2
                call    wrtvdp
                ld      b, $08
                ld      c, 8
                call    wrtvdp
                ld      b, $80
                ld      c, 9
                call    wrtvdp
                ret

; Screen 7: Graphic 6 (512x212, 16 colors / 4 bpp, 2 pages)
initg6:
                ld      b, $0A          ; M5=1, M3=1
                ld      c, 0
                call    wrtvdp
                ld      a, (RG1SAV)
                and     $43
                or      $A0
                ld      b, a
                ld      c, 1
                call    wrtvdp
                ld      b, $1F
                ld      c, 2
                call    wrtvdp
                ld      b, $08
                ld      c, 8
                call    wrtvdp
                ld      b, $80
                ld      c, 9
                call    wrtvdp
                ret

; Screen 8: Graphic 7 (256x212, 256 colors / 8 bpp, 2 pages)
initg7:
                ld      b, $0E          ; M5=1, M4=1, M3=1
                ld      c, 0
                call    wrtvdp
                ld      a, (RG1SAV)
                and     $43
                or      $A0
                ld      b, a
                ld      c, 1
                call    wrtvdp
                ld      b, $1F
                ld      c, 2
                call    wrtvdp
                ld      b, $08
                ld      c, 8
                call    wrtvdp
                ld      b, $80
                ld      c, 9
                call    wrtvdp
                ret

; Screen 10 / 12: V9958 YJK mode (256x212, YJK=1, YAE=0)
init_yjk:
                call    initg7
                ld      b, $08          ; R#25: YJK=1, YAE=0
                ld      c, 25
                call    wrtvdp
                ret

; Screen 11: V9958 YJK + YAE attribute mode (256x212, YJK=1, YAE=1)
init_yae:
                call    initg7
                ld      b, $18          ; R#25: YJK=1, YAE=1
                ld      c, 25
                call    wrtvdp
                ret

; Mode 0 / 1 / 2 handlers (when called inside Sub-ROM)
initxt:
settxt:
                ld      b, 0
                ld      c, 0
                call    wrtvdp
                ld      a, (RG1SAV)
                and     $43
                or      $B0             ; 16K + IE0 + M1
                ld      b, a
                ld      c, 1
                call    wrtvdp
                ld      b, 0
                ld      c, 2
                call    wrtvdp
                ld      b, 1
                ld      c, 4
                jp      wrtvdp

init32:
sett32:
                ld      b, 0
                ld      c, 0
                call    wrtvdp
                ld      a, (RG1SAV)
                and     $43
                or      $A0             ; 16K + IE0
                ld      b, a
                ld      c, 1
                call    wrtvdp
                ld      b, $06
                ld      c, 2
                call    wrtvdp
                ld      b, $80
                ld      c, 3
                call    wrtvdp
                ld      b, $00
                ld      c, 4
                jp      wrtvdp

inigrp:
setgrp:
                ld      b, $02          ; M3=1
                ld      c, 0
                call    wrtvdp
                ld      a, (RG1SAV)
                and     $43
                or      $A0
                ld      b, a
                ld      c, 1
                call    wrtvdp
                ld      b, $06
                ld      c, 2
                call    wrtvdp
                ld      b, $FF
                ld      c, 3
                call    wrtvdp
                ld      b, $03
                ld      c, 4
                jp      wrtvdp

; ==============================================================================
; Hardware VDP Command Engine (HMMV, LMMV, YMMM, HMMM, LINE, PSET, POINT)
; ==============================================================================

vdp_wait:
                ; Wait until VDP command engine is idle (ST_CE = 0 in S#2)
                ld      a, 2
                out     (VDP_ADDR), a
                ld      a, 15 | $80
                out     (VDP_ADDR), a
.vdp_w_lp:
                in      a, (VDP_STAT)
                rrca                    ; Bit 0 = ST_CE (1 = executing)
                jr      c, .vdp_w_lp

                ; Restore Status Register pointer to S#0
                xor     a
                out     (VDP_ADDR), a
                ld      a, 15 | $80
                out     (VDP_ADDR), a
                ret

; Execute VDP Command: HL points to 15-byte parameter block for R#32..R#46
vdp_exec_cmd:
                call    vdp_wait
                ; Point indirect register write to R#32
                ld      a, 32
                out     (VDP_ADDR), a
                ld      a, 17 | $80
                out     (VDP_ADDR), a

                ; Stream 15 bytes to Port $9B
                ld      c, VDP_REGS
                ld      b, 15
                otir
                ret

; ------------------------------------------------------------------------------
; CLS: Clear Screen via HMMV on bitmap screens or space-fill on text screens
; ------------------------------------------------------------------------------
cls:
                ld      a, (SCRMOD)
                cp      5
                jr      nc, .cls_bitmap

                ; Text / Graphic 1/2 clear: fill name table with ' '
                ld      hl, (NAMBAS)
                call    setwrt
                ld      bc, 768
                ld      a, (SCRMOD)
                or      a
                jr      nz, .cls_txt_lp
                ld      bc, 960         ; 40x24 for Screen 0
.cls_txt_lp:
                ld      a, ' '
                out     (VDP_DATA), a
                dec     bc
                ld      a, b
                or      c
                jr      nz, .cls_txt_lp
                ret

.cls_bitmap:
                ; Hardware HMMV ($C0): fill entire page with BAKCLR
                ld      hl, cmd_cls_params
                ld      a, (BAKCLR)
                and     $0F
                ld      (hl), 0         ; DX low = 0
                inc     hl
                ld      (hl), 0         ; DX high = 0
                inc     hl
                ld      (hl), 0         ; DY low = 0
                inc     hl
                ld      (hl), 0         ; DY high = 0
                inc     hl
                ld      (hl), 0         ; NX low = 0 (256 or 512 full line)
                inc     hl
                ld      (hl), 0         ; NX high = 0
                inc     hl
                ld      (hl), 212       ; NY low = 212
                inc     hl
                ld      (hl), 0         ; NY high = 0
                inc     hl
                ; Repeat color into high and low nibbles
                ld      b, a
                sla     a
                sla     a
                sla     a
                sla     a
                or      b
                ld      (hl), a         ; CLR ($2C)
                inc     hl
                ld      (hl), 0         ; ARG ($2D)
                inc     hl
                ld      (hl), $C0       ; CMD ($2E) = HMMV
                ld      hl, cmd_cls_params
                jp      vdp_exec_cmd

cmd_cls_params: ds      15

; ------------------------------------------------------------------------------
; BLTVV: Copy VRAM to VRAM (HMMM $D0 / YMMM $E0)
; ------------------------------------------------------------------------------
bltvv:
                ; Sets up HMMM command and executes
                ld      hl, cmd_blt_params
                ld      (hl), $D0       ; CMD = HMMM
                jp      vdp_exec_cmd

; ------------------------------------------------------------------------------
; BLTVM / BLTMV: Memory <-> VRAM Transfers
; ------------------------------------------------------------------------------
bltvm:
bltmv:
                ret

; ------------------------------------------------------------------------------
; DOGRPH: Draw line via LINE command ($70)
; Input: BC = Start X, HL = Start Y, GXPOS = End X, GYPOS = End Y, ATRBYT = Color
; ------------------------------------------------------------------------------
dogrph:
                ret

grpprt:
scalxy:
mapxyc:
readc:
setatr:
setc:
nvbxln:
nvbxfl:
clrtxt:
sdfscr:
setscr:
                ret

; ==============================================================================
; Page & Display Management (SETPAG)
; ==============================================================================

setpag:
                ; Input: A = Display page, B = Active page
                push    af
                ld      (DPPAGE), a
                ld      a, b
                ld      (ACPAGE), a
                pop     af

                ; Display page affects VDP R#2 (bits 5-6)
                rlca
                rlca
                rlca
                rlca
                rlca
                and     $60
                or      $1F             ; Screen 5..8 pattern
                ld      b, a
                ld      c, 2
                jp      wrtvdp

; ==============================================================================
; Real-Time Clock (RP5C01) Primitives (REDCLK, WRTCLK)
; ==============================================================================

redclk:
                ; Input: A = Register number (0..12) -> A = Value
                out     (RTC_ADDR), a
                in      a, (RTC_DATA)
                and     $0F
                ret

wrtclk:
                ; Input: A = Register number, E = Value
                push    af
                out     (RTC_ADDR), a
                ld      a, e
                out     (RTC_DATA), a
                pop     af
                ret

; ==============================================================================
; Common VDP Primitives & 128K Access
; ==============================================================================

wrtvdp:
                ld      a, b
                out     (VDP_ADDR), a
                ld      a, c
                or      $80
                out     (VDP_ADDR), a
                ld      a, c
                cp      8
                ret     nc
                ld      hl, RG0SAV
                ld      e, c
                ld      d, 0
                add     hl, de
                ld      (hl), b
                ret

vdpsta:
                in      a, (VDP_STAT)
                ret

disscr:
                ld      a, (RG1SAV)
                and     $BF
                ld      b, a
                ld      c, 1
                jr      wrtvdp

enascr:
                ld      a, (RG1SAV)
                or      $40
                ld      b, a
                ld      c, 1
                jr      wrtvdp

chgclr:
                ld      a, (FORCLR)
                sla     a
                sla     a
                sla     a
                sla     a
                ld      b, a
                ld      a, (BAKCLR)
                and     $0F
                or      b
                ld      b, a
                ld      c, 7
                jr      wrtvdp

clrspr:
                ld      hl, $1B00
                ld      bc, 128
                ld      a, $D0
                jp      filvrm

setwrt:
                ld      a, l
                out     (VDP_ADDR), a
                ld      a, h
                and     $3F
                or      $40
                out     (VDP_ADDR), a
                ret

filvrm:
                call    setwrt
.fil_lp:
                out     (VDP_DATA), a
                dec     bc
                ld      e, a
                ld      a, b
                or      c
                ld      a, e
                jr      nz, .fil_lp
                ret

nsetrd:
                and     $01
                sla     a
                sla     a
                ld      c, a
                ld      a, h
                rlca
                rlca
                and     $03
                or      c
                ld      b, a
                ld      c, 14
                call    wrtvdp
                ld      a, l
                out     (VDP_ADDR), a
                ld      a, h
                and     $3F
                out     (VDP_ADDR), a
                ret

nsetwr:
                and     $01
                sla     a
                sla     a
                ld      c, a
                ld      a, h
                rlca
                rlca
                and     $03
                or      c
                ld      b, a
                ld      c, 14
                call    wrtvdp
                ld      a, l
                out     (VDP_ADDR), a
                ld      a, h
                and     $3F
                or      $40
                out     (VDP_ADDR), a
                ret

nrdvrm:
                call    nsetrd
                in      a, (VDP_DATA)
                ret

nwrvrm:
                call    nsetwr
                ld      a, e
                out     (VDP_DATA), a
                ret

cmd_blt_params: ds      15

; ==============================================================================
; Pad to full 16 KiB ($4000)
; ==============================================================================
                ds      $4000 - $
