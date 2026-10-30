;;; crt0_tbios.s — T-BIOS startup and area definitions for SDCC
;;; Clean-room MSX/MSX2/MSX2+/turbo R system ROM startup
;;; Target: MSX Main System ROM (Base 0x0000)
;;; Assembler: sdasz80 (SDCC)

    .module crt0_tbios
    .globl  _main
    .globl  s__DATA
    .globl  l__DATA
    .globl  s__INITIALIZED
    .globl  s__INITIALIZER
    .globl  l__INITIALIZER
    .globl  gsinit

    ;; Area ordering for SDLD linker
    .area   _HEADER (ABS)
    .org    0x0000

    ;; Main entry point at 0x0000
    di
    jp      _tbios_startup

    .area   _CODE
    .area   _HOME
    .area   _GSINIT
    .area   _GSFINAL
    .area   _CONST
    .area   _INITIALIZER
    .area   _DATA
    .area   _INITIALIZED
    .area   _BSS
    .area   _HEAP

    .area   _CODE
_tbios_startup:
    di
    ld      sp, #0xF380         ; Initialize SP below MSX system variable area

    ;; Clear BSS segment if any
    ld      hl, #s__DATA
    ld      bc, #l__DATA
    ld      a, b
    or      c
    jr      z, 1$
    xor     a
0$:
    ld      (hl), a
    inc     hl
    dec     bc
    ld      a, b
    or      c
    jr      nz, 0$
1$:

    ;; Copy initialized data to RAM if present
    ld      bc, #l__INITIALIZER
    ld      a, b
    or      c
    jr      z, 3$
    ld      hl, #s__INITIALIZER
    ld      de, #s__INITIALIZED
    ldir
3$:

    ;; Call global static initializers
    call    gsinit

    ;; Jump to C main or supervisor loop
    jp      _main

    .area   _GSINIT
gsinit:
    .area   _GSFINAL
    ret

