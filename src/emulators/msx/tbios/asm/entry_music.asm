; ==============================================================================
; T-BIOS (B-MSX System BIOS) — Clean-Room MSX-MUSIC ROM
; Target: MSX-MUSIC / OPLL ROM (16 KiB, Base 0x4000)
; Assembler: Pasmo Z80
; Required by MGSDRV and game drivers: "APRLOPLL" at $4018
; ==============================================================================

                include hardware.inc
                include systemvars.inc

                org     $4000

                db      "AB"
                dw      music_init      ; INIT vector
                dw      0               ; STATEMENT vector
                dw      0               ; DEVICE vector
                dw      0               ; BASIC TEXT vector
                dw      0, 0, 0

                ds      $4018 - $
                db      "APRLOPLL"      ; Standard MSX-MUSIC detection signature

; ==============================================================================
; FM BIOS Standard Vectors ($4110 - $4124)
; ==============================================================================

; $4110 WRTOPL: Write OPLL register
                ds      $4110 - $
                jp      wrtopl

; $4113 INIOPL: Initialize OPLL
                ds      $4113 - $
                jp      iniopl

; $4116 MSTART: Start music playback
                ds      $4116 - $
                jp      mstart

; $4119 MSTOP: Stop music playback
                ds      $4119 - $
                jp      mstop

; $411C RDDATA: Read music data
                ds      $411C - $
                jp      rddata

; $411F OPLDRV: OPLL driver dispatch
                ds      $411F - $
                jp      opldrv

; $4122 TSTBGM: Test BGM status
                ds      $4122 - $
                jp      tstbgm

; ==============================================================================
; Implementation
; ==============================================================================

music_init:
                ; Initialize OPLL on cartridge boot
                call    iniopl
                ret

; ------------------------------------------------------------------------------
; WRTOPL: Write OPLL register
; Input:  A = Register address (0..$3F)
;         E = Data byte
; ------------------------------------------------------------------------------
wrtopl:
                push    af
                out     (OPLL_ADDR), a
                ; The YM2413 requires a minimum delay after address write
                push    af
                pop     af
                ld      a, e
                out     (OPLL_DATA), a
                pop     af
                ret

; ------------------------------------------------------------------------------
; INIOPL: Initialize OPLL hardware (silence all channels)
; ------------------------------------------------------------------------------
iniopl:
                push    af
                push    bc
                push    de

                ; 1. Clear user instrument registers ($00 - $07)
                ld      c, 0
.ini_patch_lp:
                ld      a, c
                ld      e, 0
                call    wrtopl
                inc     c
                ld      a, c
                cp      8
                jr      c, .ini_patch_lp

                ; 2. Clear rhythm control ($0E = 0)
                ld      a, $0E
                ld      e, 0
                call    wrtopl

                ; 3. Silence all 9 channels: key off ($20..$28) and quietest volume ($30..$38)
                ld      c, 0
.ini_chan_lp:
                ; Key off: reg $20 + c = 0
                ld      a, $20
                add     a, c
                ld      e, 0
                call    wrtopl

                ; Volume = $0F (quietest, instrument 0): reg $30 + c = $0F
                ld      a, $30
                add     a, c
                ld      e, $0F
                call    wrtopl

                inc     c
                ld      a, c
                cp      9
                jr      c, .ini_chan_lp

                pop     de
                pop     bc
                pop     af
                ret

; ------------------------------------------------------------------------------
; MSTART: Start background music playback
; ------------------------------------------------------------------------------
mstart:
                ret

; ------------------------------------------------------------------------------
; MSTOP: Stop background music playback
; ------------------------------------------------------------------------------
mstop:
                jp      iniopl

; ------------------------------------------------------------------------------
; RDDATA: Read next music byte
; Output: A = byte
; ------------------------------------------------------------------------------
rddata:
                xor     a
                ret

; ------------------------------------------------------------------------------
; OPLDRV: Execute driver command
; ------------------------------------------------------------------------------
opldrv:
                ret

; ------------------------------------------------------------------------------
; TSTBGM: Test if music playback is active
; Output: A = 0 (stopped), 1 (playing)
; ------------------------------------------------------------------------------
tstbgm:
                xor     a
                ret

; ==============================================================================
; Pad to exactly 16 KiB ($4000 -> $8000)
; ==============================================================================
                ds      $8000 - $, $FF
