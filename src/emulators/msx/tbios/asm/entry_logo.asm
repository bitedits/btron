; ==============================================================================
; T-BIOS (B-MSX System BIOS) — Clean-Room Logo ROM
; Target: MSX Logo ROM (16 KiB, Base 0x0000)
; Assembler: Pasmo Z80
; ==============================================================================

                org     $0000

                db      "T-BIOS Logo ROM",$FF

logo_init:
                ret

; Pad to full 16 KiB (0x4000)
                ds      $4000 - $

