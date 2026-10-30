;;; crt0_btron.s — 8-bit BTRON startup assembly for MSX MegaROM
;;; Imported from ../msx.btron.pro/src/sdcc/crt0_btron.s
;;; Target: MSX turbo R (ASCII R800 / Z80 fallback), ASCII16 MegaROM
;;; Assembler: sdasz80 (SDCC)

    .module crt0_btron
    .globl  _main
    .globl  s__DATA
    .globl  l__DATA
    .globl  s__INITIALIZED
    .globl  s__INITIALIZER
    .globl  l__INITIALIZER
    .globl  gsinit

    ;; Define area ordering for SDLD linker:
    ;; ROM areas (placed starting at 0x4010)
    .area   _CODE
    .area   _HOME
    .area   _GSINIT
    .area   _GSFINAL
    .area   _CONST
    .area   _INITIALIZER
    ;; RAM areas (placed starting at 0xC000)
    .area   _DATA
    .area   _INITIALIZED
    .area   _BSS
    .area   _HEAP

    ;; MSX ROM Cartridge Header at 0x4000 (Slot Page 1)
    .area   _HEADER (ABS)
    .org    0x4000

    .byte   0x41, 0x42          ; "AB" Cartridge ROM signature
    .word   _init               ; Cartridge INIT vector
    .word   _exec               ; Cartridge EXEC vector
    .word   0x0000              ; STATEMENT expansion (none)
    .word   0x0000              ; DEVICE expansion (none)
    .word   0x0000              ; TEXT pointer (none)
    .word   0x0000              ; Reserved
    .word   0x0000              ; Reserved

    .area   _CODE

_init:
_exec:
    di
    ld      sp, #0xEFFF         ; Set stack pointer below MSX system area (0xF380)

    ;; ── Map Page 2 (0x8000..0xBFFF) to Cartridge Slot ─────────────────
    in      a, (#0xA8)
    ld      b, a
    and     #0x0C               ; bits 2-3 (Page 1 slot)
    rlca
    rlca                        ; shift to bits 4-5 (Page 2 slot)
    ld      c, a
    ld      a, b
    and     #0xCF               ; clear bits 4-5
    or      c
    out     (#0xA8), a          ; Page 2 now maps to cartridge slot

    ;; ── For ASCII16 MegaROM: Select Bank 1 for Page 2 ──────────────────
    ld      a, #0x01
    ld      (#0x7000), a

    ;; ── Detect MSX turbo R and activate R800 CPU Mode ───────────────────
    ld      a, (#0x002D)        ; MSX version ID in BIOS ROM (3 = MSX turbo R)
    cp      #0x03
    jr      nz, .z80_mode
    ;; Select R800 ROM mode (Mode 0x82: R800 with ROM in fast mode)
    ld      a, #0x82
    call    0x0180              ; CHGCPU BIOS entry in Page 0
.z80_mode:

    ;; ── Clear uninitialized DATA (BSS) segment ──────────────────────────
    ld      hl, #s__DATA
    ld      bc, #l__DATA
    ld      a, b
    or      c
    jr      z, .bss_done
    xor     a
.bss_loop:
    ld      (hl), a
    inc     hl
    dec     bc
    ld      a, b
    or      c
    jr      nz, .bss_loop
.bss_done:

    ;; ── Copy initialized data to RAM ────────────────────────────────────
    ld      bc, #l__INITIALIZER
    ld      a, b
    or      c
    jr      z, .data_done
    ld      hl, #s__INITIALIZER
    ld      de, #s__INITIALIZED
    ldir
.data_done:

    ;; ── Global static initializers ──────────────────────────────────────
    call    gsinit

    ;; ── Jump to BTRON OS main entry point ───────────────────────────────
    ei
    jp      _main

    ;; Hang if main returns
_halt:
    di
    halt
    jr      _halt

    ;; SDCC Global static initialization table areas
    .area   _GSINIT
gsinit::
    .area   _GSFINAL
    ret

