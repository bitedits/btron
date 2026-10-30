; ==============================================================================
; T-BIOS (B-MSX System BIOS) — Clean-Room Main ROM
; Phase 2: Core System BIOS (Reset, Slots, IRQ, VDP, Keyboard & Joysticks)
; Target: MSX2 / MSX2+ / MSX turbo R System ROM (32 KiB, Base 0x0000)
; Assembler: Pasmo Z80
; ==============================================================================

                include "hardware.inc"
                include "systemvars.inc"
                include "hooks.inc"

                org     $0000

; ==============================================================================
; Fixed Jump Vector Table ($0000 - $0189)
; ==============================================================================

; $0000 CHKRAM: Power-on reset & RAM check
                di
                jp      chkram

; $0004 CGTABL: Base address of MSX character set in ROM
                dw      tbios_font

; $0006 VDP_DR / VDP_DW
vdp_dr:         db      VDP_DATA        ; $98
vdp_dw:         db      VDP_DATA        ; $98

; $0008 SYNCHR
                ds      $0008 - $
                jp      synchr

; $000C RDSLT: Read byte from slot (A = slot ID, HL = addr -> A = val)
                ds      $000C - $
                jp      rdslt

; $0010 CHRGTR
                ds      $0010 - $
                jp      chrgtr

; $0014 WRSLT: Write byte to slot (A = slot ID, HL = addr, E = val)
                ds      $0014 - $
                jp      wrslt

; $0018 OUTDO
                ds      $0018 - $
                jp      outdo

; $001C CALSLT: Inter-slot call (IY = slot ID, IX = addr)
                ds      $001C - $
                jp      calslt

; $0020 DCOMPR: Compare HL with DE
                ds      $0020 - $
                jp      dcompr

; $0024 ENASLT: Enable slot (A = slot ID, HL = addr in page)
                ds      $0024 - $
                jp      enaslt

; $0028 GETYPR
                ds      $0028 - $
                jp      getypr

; $002B IDBYT1 / IDBYT2 / Version ID
                ds      $002B - $
idbyt1:         db      $00             ; 60Hz, English charset, Y-M-D
idbyt2:         db      $00             ; Japanese/QWERTY
romid:
IFDEF MSX_VER
                db      MSX_VER
ELSE
                db      $01             ; 1 = MSX2 (2=MSX2+, 3=turbo R)
ENDIF
midi_flag:      db      $00             ; MSX-MIDI present
                db      $00             ; Reserved

; $0030 CALLF: Far call via RST 30h
                ds      $0030 - $
                jp      callf

; $0038 KEYINT: Interrupt handler (RST 38h)
                ds      $0038 - $
                jp      keyint

; $003B INITIO: Initialize I/O
                ds      $003B - $
                jp      initio

; $003E INIFNK: Initialize function keys
                ds      $003E - $
                jp      inifnk

; $0041 DISSCR: Disable screen display
                ds      $0041 - $
                jp      disscr

; $0044 ENASCR: Enable screen display
                ds      $0044 - $
                jp      enascr

; $0047 WRTVDP: Write to VDP register (B = data, C = reg)
                ds      $0047 - $
                jp      wrtvdp

; $004A RDVRM: Read byte from VRAM (HL = addr -> A = data)
                ds      $004A - $
                jp      rdvrm

; $004D WRTVRM: Write byte to VRAM (HL = addr, A = data)
                ds      $004D - $
                jp      wrtvrm

; $0050 SETRD: Set VRAM address for read (HL = addr)
                ds      $0050 - $
                jp      setrd

; $0053 SETWRT: Set VRAM address for write (HL = addr)
                ds      $0053 - $
                jp      setwrt

; $0056 FILVRM: Fill VRAM (HL = addr, BC = length, A = data)
                ds      $0056 - $
                jp      filvrm

; $0059 LDIRMV: Block transfer VRAM to RAM (HL = VRAM, DE = RAM, BC = count)
                ds      $0059 - $
                jp      ldirmv

; $005C LDIRVM: Block transfer RAM to VRAM (HL = RAM, DE = VRAM, BC = count)
                ds      $005C - $
                jp      ldirvm

; $005F CHGMOD: Change VDP screen mode (A = mode)
                ds      $005F - $
                jp      chgmod

; $0062 CHGCLR: Change colors (FORCLR, BAKCLR, BDRCLR)
                ds      $0062 - $
                jp      chgclr

; $0066 NMI: Non-maskable interrupt handler
                ds      $0066 - $
                jp      nmi

; $0069 CLRSPR: Clear sprites
                ds      $0069 - $
                jp      clrspr

; $006C INITXT: Initialize Screen 0 (40x24)
                ds      $006C - $
                jp      initxt

; $006F INIT32: Initialize Screen 1 (32x24)
                ds      $006F - $
                jp      init32

; $0072 INITGRP: Initialize Screen 2 (Graphics 1)
                ds      $0072 - $
                jp      inigrp

; $0075 INIMLT: Initialize Screen 3 (Multi-color)
                ds      $0075 - $
                jp      inimlt

; $0078 SETTXT: Set VDP for Screen 0
                ds      $0078 - $
                jp      settxt

; $007B SETT32: Set VDP for Screen 1
                ds      $007B - $
                jp      sett32

; $007E SETGRP: Set VDP for Screen 2
                ds      $007E - $
                jp      setgrp

; $0081 SETMLT: Set VDP for Screen 3
                ds      $0081 - $
                jp      setmlt

; $0084 CALPAT: Calc sprite pattern address (A = sprite -> HL = VRAM)
                ds      $0084 - $
                jp      calpat

; $0087 CALATR: Calc sprite attribute address (A = sprite -> HL = VRAM)
                ds      $0087 - $
                jp      calatr

; $008A GSPSIZ: Get sprite size (-> A = size 8 or 32)
                ds      $008A - $
                jp      gspsiz

; $008D GRPPRT: Print character in graphic mode
                ds      $008D - $
                jp      grpprt

; $0090 GICINI: Initialize PSG
                ds      $0090 - $
                jp      gicini

; $0093 WRTPSG: Write PSG register (A = reg, E = data)
                ds      $0093 - $
                jp      wrtpsg

; $0096 RDPSG: Read PSG register (A = reg -> A = data)
                ds      $0096 - $
                jp      rdpsg

; $0099 STRTMS: Start background music
                ds      $0099 - $
                jp      strtms

; $009C CHSNS: Check keyboard buffer status
                ds      $009C - $
                jp      chsns

; $009F CHGET: Get character from keyboard
                ds      $009F - $
                jp      chget

; $00A2 CHPUT: Output character to display
                ds      $00A2 - $
                jp      chput

; $00A5 LPTOUT: Output character to printer
                ds      $00A5 - $
                jp      lptout

; $00A8 LPTSTT: Get printer status
                ds      $00A8 - $
                jp      lptstt

; $00AB CNVCHR: Convert character code
                ds      $00AB - $
                jp      cnvchr

; $00AE PINLIN: Input line from keyboard with prompt
                ds      $00AE - $
                jp      pinlin

; $00B1 INLIN: Input line from keyboard
                ds      $00B1 - $
                jp      inlin

; $00B4 QINLIN: Input line with '?' prompt
                ds      $00B4 - $
                jp      qinlin

; $00B7 BREAKX: Check Ctrl-STOP
                ds      $00B7 - $
                jp      breakx

; $00BA ISCNTC: Check Ctrl-C
                ds      $00BA - $
                jp      iscntc

; $00BD CKCNTC: Check Ctrl-C and abort
                ds      $00BD - $
                jp      ckcntc

; $00C0 BEEP: Short beep
                ds      $00C0 - $
                jp      beep

; $00C3 CLS: Clear screen
                ds      $00C3 - $
                jp      cls_z

; $00C6 POSIT: Position cursor (H = X, L = Y)
                ds      $00C6 - $
                jp      posit

; $00C9 FNKSB: Function key display
                ds      $00C9 - $
                jp      fnksb

; $00CC ERAFNK: Erase function keys from screen
                ds      $00CC - $
                jp      erafnk

; $00CF DSPFNK: Display function keys on screen
                ds      $00CF - $
                jp      dspfnk

; $00D2 TOTEXT: Move cursor to text screen
                ds      $00D2 - $
                jp      totext

; $00D5 GTSTCK: Get joystick direction (A = id -> A = direction 0..8)
                ds      $00D5 - $
                jp      gtstck

; $00D8 GTTRIG: Get joystick trigger (A = id -> A = state $FF/0)
                ds      $00D8 - $
                jp      gttrig

; $00DB GTPAD: Get touchpad / mouse
                ds      $00DB - $
                jp      gtpad

; $00DE GTPDL: Get paddle value
                ds      $00DE - $
                jp      gtpdl

; $00E1 TAPION: Tape read start
                ds      $00E1 - $
                jp      tapion

; $00E4 TAPIN: Tape read byte
                ds      $00E4 - $
                jp      tapin

; $00E7 TAPIOF: Tape stop read
                ds      $00E7 - $
                jp      tapiof

; $00EA TAPOON: Tape write start
                ds      $00EA - $
                jp      tapoon

; $00ED TAPOUT: Tape write byte
                ds      $00ED - $
                jp      tapout

; $00F0 TAPOOF: Tape stop write
                ds      $00F0 - $
                jp      tapoof

; $00F3 STMOTR: Motor control
                ds      $00F3 - $
                jp      stmotr

; $00F6 LFTQ
                ds      $00F6 - $
                jp      lftq

; $00F9 PUTQ
                ds      $00F9 - $
                jp      putq

; $00FC RIGHTC
                ds      $00FC - $
                jp      rightc

; $00FF LEFTC
                ds      $00FF - $
                jp      leftc

; $0102 UPC
                ds      $0102 - $
                jp      upc

; $0105 TUPC
                ds      $0105 - $
                jp      tupc

; $0108 DOWNC
                ds      $0108 - $
                jp      downc

; $010B TDOWNC
                ds      $010B - $
                jp      tdownc

; $010E SCALXY
                ds      $010E - $
                jp      scalxy

; $0111 MAPXY
                ds      $0111 - $
                jp      mapxy

; $0114 FETCHC
                ds      $0114 - $
                jp      fetchc

; $0117 STOREC
                ds      $0117 - $
                jp      storec

; $011A SETATR
                ds      $011A - $
                jp      setatr

; $011D READC
                ds      $011D - $
                jp      readc

; $0120 SETC
                ds      $0120 - $
                jp      setc

; $0123 NSETCX
                ds      $0123 - $
                jp      nsetcx

; $0126 GTASPC
                ds      $0126 - $
                jp      gtaspc

; $0129 PNTINI
                ds      $0129 - $
                jp      pntini

; $012C SCANR
                ds      $012C - $
                jp      scanr

; $012F SCANL
                ds      $012F - $
                jp      scanl

; $0132 CHGCAP
                ds      $0132 - $
                jp      chgcap

; $0135 CHGSND
                ds      $0135 - $
                jp      chgsnd

; $0138 RSLREG: Read primary slot status register
                ds      $0138 - $
                jp      rslreg

; $013B WSLREG: Write primary slot status register
                ds      $013B - $
                jp      wslreg

; $013E RDVDP: Read VDP status register (S#0)
                ds      $013E - $
                jp      rdvdp

; $0141 SNSMAT: Sense keyboard matrix row (A = row -> A = data)
                ds      $0141 - $
                jp      snsmat

; $0144 PHYDIO
                ds      $0144 - $
                jp      phydio

; $0147 FORMAT
                ds      $0147 - $
                jp      format

; $014A ISFLIO
                ds      $014A - $
                jp      isflio

; $014D OUTDLP
                ds      $014D - $
                jp      outdlp

; $0150 GETVCP
                ds      $0150 - $
                jp      getvcp

; $0153 GETVC2
                ds      $0153 - $
                jp      getvc2

; $0156 KILBUF: Clear keyboard buffer
                ds      $0156 - $
                jp      kilbuf

; $0159 CALBAS: Call BASIC
                ds      $0159 - $
                jp      calbas

; $015C SUBROM: Call routine in Sub-ROM
                ds      $015C - $
                jp      subrom

; $015F EXTROM: Call routine in Sub-ROM
                ds      $015F - $
                jp      extrom

; $0162 CHKSLZ: Search slots for Sub-ROM
                ds      $0162 - $
                jp      chkslz

; $0165 CHKNEW: Check if screen mode is bitmap mode
                ds      $0165 - $
                jp      chknew

; $0168 EOL: Delete to end of line
                ds      $0168 - $
                jp      eol

; $016B BIGFIL: Fill VRAM (128K)
                ds      $016B - $
                jp      bigfil

; $016E NSETRD: Set VRAM read address (128K)
                ds      $016E - $
                jp      nsetrd

; $0171 NSETWR: Set VRAM write address (128K)
                ds      $0171 - $
                jp      nsetwr

; $0174 NRDVRM: Read byte from VRAM (128K)
                ds      $0174 - $
                jp      nrdvrm

; $0177 NWRVRM: Write byte to VRAM (128K)
                ds      $0177 - $
                jp      nwrvrm

; $017A RDRES
                ds      $017A - $
                jp      rdres

; $017D WRRES
                ds      $017D - $
                jp      wrres

; $0180 CHGCPU: Change CPU mode (A = mode 0:Z80, 1:R800 ROM, 2:R800 DRAM)
                ds      $0180 - $
                jp      chgcpu

; $0183 GETCPU: Get CPU mode (-> A = mode)
                ds      $0183 - $
                jp      getcpu

; $0186 PCMPLY: PCM play
                ds      $0186 - $
                jp      pcmply

; $0189 PCMREC: PCM record
                ds      $0189 - $
                jp      pcmrec

; Pad to $0200 for code area
                ds      $0200 - $

; ==============================================================================
; System Initialization & Boot Sequence (CHKRAM)
; ==============================================================================

chkram:
                di
                im      1

                ; Check if CPU switch is in flight (resuming newly selected CPU)
                ld      hl, (CHGCPU_MAGIC)
                ld      de, $A55A
                or      a
                sbc     hl, de
                jp      z, chgcpu_resume

                ; On turbo R, initialize PCM port: $A5 = 3 (unmuted)
                ld      a, (romid)
                cp      3
                jr      nz, .not_tr_init
                ld      a, 3
                out     ($A5), a
.not_tr_init:

                ; 1. Initialize PPI 8255 (#AB = 0x82: Port A out, Port B in, Port C out)
                ld      a, $82
                out     ($AB), a

                ; Initialize Memory Mapper Ports ($FC..$FF)
                xor     a
                out     ($FF), a        ; Page 3 mapper = segment 0
                inc     a
                out     ($FE), a        ; Page 2 mapper = segment 1
                inc     a
                out     ($FD), a        ; Page 1 mapper = segment 2
                inc     a
                out     ($FC), a        ; Page 0 mapper = segment 3

                ; 2. Search for RAM and map Page 2 ($8000-$BFFF) & Page 3 ($C000-$FFFF) to RAM
                ; Test slots for RAM starting from slot 3 down to 0
                ld      b, 3            ; Primary slot
.chk_pslot_lp:
                ; Map Page 2 and Page 3 to primary slot B
                in      a, (PSL_STAT)
                and     $0F             ; preserve Page 0 & 1 (BIOS)
                ld      c, b
                sla     c
                sla     c
                sla     c
                sla     c               ; Page 2 = B << 4
                or      c
                ld      c, b
                sla     c
                sla     c
                sla     c
                sla     c
                sla     c
                sla     c               ; Page 3 = B << 6
                or      c
                out     (PSL_STAT), a   ; Page 2 & 3 now in slot B

                ; Check subslots 3 down to 0
                ld      c, 3            ; Subslot
.chk_sslot_lp:
                ; Try setting subslot C on Page 2 & 3
                ld      a, ($FFFF)
                cpl
                and     $0F
                ld      e, c
                sla     e
                sla     e
                sla     e
                sla     e
                or      e
                ld      e, c
                sla     e
                sla     e
                sla     e
                sla     e
                sla     e
                sla     e
                or      e
                ld      ($FFFF), a

                ; Test RAM at $C000 and $FF00
                ld      hl, $C000
                ld      a, $55
                ld      (hl), a
                cp      (hl)
                jr      nz, .chk_sslot_next
                cpl
                ld      (hl), a
                cp      (hl)
                jr      nz, .chk_sslot_next

                ld      hl, $FF00
                ld      a, $55
                ld      (hl), a
                cp      (hl)
                jr      nz, .chk_sslot_next
                cpl
                ld      (hl), a
                cp      (hl)
                jr      z, .ram_found   ; RAM confirmed!

.chk_sslot_next:
                dec     c
                jp      p, .chk_sslot_lp

                dec     b
                jp      p, .chk_pslot_lp

.ram_found:
                ; RAM is now mapped on Page 2 and Page 3!
                ld      sp, $F380       ; Place stack below MSX system vars in RAM

                ; 2. Initialize Hook Table ($FD9A - $FFCF) with RET ($C9) and NOPs
                ld      hl, H_KEYI
                ld      de, H_KEYI + 1
                ld      bc, $FFD0 - H_KEYI - 1
                ld      (hl), $C9       ; RET opcode
                ldir

                ; 3. Initialize Trampolines in RAM ($F380 - $F399)
                ld      hl, tbl_trampolines
                ld      de, $F380
                ld      bc, sz_trampolines
                ldir

                ; 4. Initialize Standard System Variables
                ld      a, 15
                ld      (FORCLR), a     ; White text
                ld      a, 4
                ld      (BAKCLR), a     ; Blue background
                ld      (BDRCLR), a     ; Blue border
                ld      a, 40
                ld      (LINL40), a     ; 40 columns (SCREEN 0)
                ld      a, 29
                ld      (LINL32), a     ; 29/32 columns (SCREEN 1)
                ld      a, 40
                ld      (LINLEN), a
                ld      a, 1
                ld      (CSRX), a
                ld      (CSRY), a
                ld      (CLIKSW), a     ; Key click sound enabled
                xor     a
                ld      (CPUMOD), a     ; Default CPU mode = 0 (Z80)

                ; Initialize Table Base Addresses
                ; SCREEN 0
                ld      hl, $0000
                ld      (TXTNAM), hl
                ld      (TXTCOL), hl
                ld      hl, $0800
                ld      (TXTCGP), hl
                ld      hl, $0000
                ld      (TXTATR), hl
                ld      (TXTPAT), hl

                ; SCREEN 1
                ld      hl, $1800
                ld      (T32NAM), hl
                ld      hl, $2000
                ld      (T32COL), hl
                ld      hl, $0000
                ld      (T32CGP), hl
                ld      hl, $1B00
                ld      (T32ATR), hl
                ld      hl, $3800
                ld      (T32PAT), hl

                ; SCREEN 2
                ld      hl, $1800
                ld      (GRPNAM), hl
                ld      hl, $2000
                ld      (GRPCOL), hl
                ld      hl, $0000
                ld      (GRPCGP), hl
                ld      hl, $1B00
                ld      (GRPATR), hl
                ld      hl, $3800
                ld      (GRPPAT), hl

                ; Circular Keyboard Buffer initialization
                call    kilbuf

                ; Software timer (JIFFY) initialization
                ld      hl, 0
                ld      (JIFFY), hl

                ; Initialize VDP register mirror area (RG0SAV..RG7SAV)
                xor     a
                ld      (RG0SAV), a
                ld      a, $B0          ; 16K VRAM ($80) + IE0 ($20) + M1 ($10)
                ld      (RG1SAV), a
                xor     a
                ld      (RG2SAV), a
                ld      (RG3SAV), a
                ld      a, $01
                ld      (RG4SAV), a
                xor     a
                ld      (RG5SAV), a
                ld      (RG6SAV), a
                ld      a, $F4          ; Text color 15 on border 4
                ld      (RG7SAV), a

                ; 5. Detect and initialize Slot Tables (EXPTBL / SLTTBL)
                call    detect_slots

                ; 5b. Search and register Sub-ROM (EXBRSA)
                call    chkslz

                ; 5c. Search and initialize Extension ROMs (Disk ROM in Slot 3-3)
                call    search_ext_roms

                ; 6. Initialize VDP to Screen 0
                call    initxt

                ; 7. Search and boot Cartridge ROMs
                call    search_cartridges

                ; 8. Boot disk if Disk ROM is present (calls H_RUNC)
                call    H_RUNC

                ; 9. Idle loop if no cartridge or disk boots, or upon return
hang_loop:
                ei
                halt
                jp      hang_loop

; ==============================================================================
; Slot Detection (EXPTBL & SLTTBL)
; ==============================================================================

detect_slots:
                ; Initialize EXPTBL ($FCC1 - $FCC4)
                ld      hl, EXPTBL
                ld      a, (romid)
                cp      3               ; turbo R has Slot 0 expanded
                jr      nz, .not_tr_exp
                ld      a, $80
                jr      .set_exp0
.not_tr_exp:
                xor     a
.set_exp0:
                ld      (hl), a         ; EXPTBL + 0
                inc     hl
                ld      (hl), 0         ; EXPTBL + 1 (Cartridge 1)
                inc     hl
                ld      (hl), 0         ; EXPTBL + 2 (Cartridge 2)
                inc     hl
                ld      (hl), $80       ; EXPTBL + 3 (Slot 3 is expanded)

                ; Initialize SLTTBL ($FCC5 - $FCC8)
                ld      hl, SLTTBL
                ld      (hl), 0
                inc     hl
                ld      (hl), 0
                inc     hl
                ld      (hl), 0
                inc     hl
                ; Slot 3 subslot register current value
                ld      a, ($FFFF)
                cpl
                ld      (hl), a
                ret

; ==============================================================================
; Primary & Secondary Slot Operations (RDSLT, WRSLT, CALSLT, ENASLT, CALLF)
; ==============================================================================

; ------------------------------------------------------------------------------
; RDSLT: Read byte from slot
; Input:  A  = Slot ID (E000SSPP)
;         HL = Address to read
; Output: A  = Byte read
; Destroys: F, C, DE
; ------------------------------------------------------------------------------
rdslt:
                push    bc
                push    hl
                push    af
                ld      d, a
                and     a               ; Bit 7 set = expanded slot
                di
                call    m, select_subslot
                pop     af
                pop     hl

                push    hl              ; HL = address
                push    de              ; D = slot ID, E = saved SSL
                push    hl
                push    af

                ; Calculate page number from HL (bits 14-15)
                ld      a, h
                rlca
                rlca
                and     $03
                ld      l, a            ; L = page number

                ld      b, a
                ld      a, $FC
                call    slot_shift_mask
                ld      e, a            ; E = mask shifted
                ld      b, l            ; B = page
                pop     af              ; A = slot ID
                and     $03
                call    slot_shift_mask
                ld      b, a            ; B = primary slot shifted
                in      a, (PSL_STAT)
                ld      d, a            ; D = restore PSL
                and     e
                or      b               ; A = read PSL
                pop     hl

                call    $F380           ; call RDPRIM in RAM
                ld      a, e
                pop     de              ; D = slot ID, E = saved SSL
                push    af
                bit     7, d
                call    nz, restore_subslot
                pop     af
                pop     hl
                pop     bc
                ret

slot_shift_mask:
                inc     b
                dec     b
                ret     z
.shift_lp:
                rlca
                rlca
                djnz    .shift_lp
                ret

; ------------------------------------------------------------------------------
; WRSLT: Write byte to slot
; Input:  A  = Slot ID (E000SSPP)
;         HL = Address to write
;         E  = Byte to write
; Output: None
; Destroys: AF, BC, D
; ------------------------------------------------------------------------------
wrslt:
                push    hl
                ld      d, a            ; D = slot ID
                push    de
                and     a
                di
                call    m, select_subslot
                pop     bc              ; B = slot ID, C = data to write
                pop     hl
                push    de              ; D = slot ID, E = saved SSL

                push    hl
                ld      a, h
                rlca
                rlca
                and     $03
                ld      l, a            ; L = page

                ld      b, a
                ld      a, $FC
                call    slot_shift_mask
                ld      e, a            ; E = mask shifted

                ld      b, l
                ld      a, d
                and     $03
                call    slot_shift_mask
                ld      b, a            ; B = primary slot shifted
                in      a, (PSL_STAT)
                ld      d, a            ; D = restore PSL
                and     e
                or      b               ; A = write PSL
                pop     hl
                ld      e, c            ; E = data to write

                call    $F385           ; call WRPRIM in RAM
                pop     de              ; D = slot ID, E = saved SSL
                bit     7, d
                call    nz, restore_subslot
                ret

; ------------------------------------------------------------------------------
; CALSLT: Inter-slot call
; Input:  IY = Slot ID (E000SSPP)
;         IX = Address to call
; Output: Dependent on called routine
; ------------------------------------------------------------------------------
calslt:
                ex      af, af'
                exx

                di
                ld      a, iyh
                or      a
                jr      nz, .calslt_got_slot
                ld      a, iyl
.calslt_got_slot:
                push    ix
                pop     hl              ; HL = address to call
                ld      d, a            ; D = slot ID
                and     a               ; expanded slot?
                call    m, select_subslot
                push    de              ; D = slot ID, E = saved SSL
                in      a, (PSL_STAT)
                push    af              ; saved PSL at SP+9
                push    de              ; padding for MSX-DOS stack layout

                ; Calculate primary slot select for target page
                ld      a, d
                and     $03
                ld      b, a            ; B = primary slot
                ld      c, $FC          ; C = mask
                push    ix
                pop     af              ; A = high byte of call address
                rlca
                rlca
                and     $03             ; A = page (0..3)
                add     a, a            ; page * 2
                jr      z, .calslt_sh2
.calslt_sh1:
                rlc     b
                rlc     c
                dec     a
                jr      nz, .calslt_sh1
.calslt_sh2:
                ld      hl, .calslt_ret
                push    hl              ; return address after clprim
                in      a, (PSL_STAT)
                push    af              ; push restore PSL for CLPRIM
                and     c
                or      b               ; A = target PSL
                exx
                jp      $F38C           ; call CLPRIM in RAM

.calslt_ret:
                ex      af, af'
                exx
                di
                pop     de              ; discard padding
                pop     af              ; discard saved PSL
                pop     de              ; D = slot ID, E = saved SSL
                bit     7, d
                call    nz, restore_subslot
                ex      af, af'
                exx
                ret

; ------------------------------------------------------------------------------
; ENASLT: Enable slot on page
; Input:  A  = Slot ID (E000SSPP)
;         HL = Address in page
; ------------------------------------------------------------------------------
enaslt:
                di
                push    bc
                push    de
                ld      d, a            ; D = Slot ID

                ; Page = H >> 6 (0..3)
                ld      a, h
                rlca
                rlca
                and     $03
                ld      b, a            ; B = page

                ; Calculate page shift (page * 2)
                add     a, a
                ld      c, a            ; C = shift
                ld      a, $03
                jr      z, .enaslt_m_done
.enaslt_m_lp:
                sla     a
                dec     c
                jr      nz, .enaslt_m_lp
.enaslt_m_done:
                cpl
                ld      e, a            ; E = page inverted mask

                ; If expanded slot, update subslot register for this page
                bit     7, d
                jr      z, .enaslt_psl

                ; Select subslot for page
                push    bc
                push    de
                push    hl
                ld      a, d
                call    select_subslot
                pop     hl
                pop     de
                pop     bc

.enaslt_psl:
                in      a, (PSL_STAT)
                and     e               ; clear page bits
                ld      c, a

                ld      a, d
                and     $03             ; A = target primary slot
                ld      e, b
                inc     e
.enaslt_s_lp:
                dec     e
                jr      z, .enaslt_s_done
                sla     a
                sla     a
                jr      .enaslt_s_lp
.enaslt_s_done:
                or      c
                out     (PSL_STAT), a   ; apply slot change

                pop     de
                pop     bc
                ret

; ------------------------------------------------------------------------------
; CALLF: Far call via RST 30h
; ------------------------------------------------------------------------------
callf:
                exx
                pop     hl              ; HL = address of inline far call data
                ld      e, (hl)
                inc     hl
                ld      d, (hl)         ; DE = call target
                inc     hl
                ld      a, (hl)         ; A = slot ID
                inc     hl
                exx
                push    hl              ; return address
                exx
                push    de              ; IX target
                pop     ix
                ld      iyh, a
                ld      iyl, a          ; IY = slot ID
                jp      calslt

; ------------------------------------------------------------------------------
; Subslot Selection Helpers
; ------------------------------------------------------------------------------
select_subslot:
                ; D = E000SSPP, HL = address
                ; Switching page 0 subslot of the primary slot the BIOS executes
                ; from would unmap the BIOS mid-routine; skip the switch in that case.
                ld      d, a            ; D = E000SSPP
                ld      b, 0            ; B = 0: perform switch
                ld      a, h
                and     $C0
                ld      a, d
                jr      nz, .sel_sub_do
                in      a, (PSL_STAT)
                xor     d
                and     $03
                ld      a, d
                jr      nz, .sel_sub_do
                inc     b               ; B = 1: keep register value
.sel_sub_do:
                ; Select primary slot of target in Page 3 ($C000-$FFFF)
                rrca
                rrca
                ld      e, a            ; E = PPE000SS
                and     $C0
                ld      l, a            ; L = PP000000
                in      a, (PSL_STAT)
                ld      c, a            ; C = saved PSL
                and     $3F
                or      l
                out     (PSL_STAT), a

                ; Shift subslot and mask according to page
                ld      a, e
                and     $03
                ld      l, a            ; L = subslot
                ld      a, h            ; A = high byte of address
                ld      h, $03          ; H = mask
                jr      .sel_sub_next
.sel_sub_lp:
                add     hl, hl          ; Shift 2 bits left
                add     hl, hl
.sel_sub_next:
                sub     $40
                jr      nc, .sel_sub_lp
                ld      a, h
                cpl
                ld      h, a

                ; Read and update SSL_REGS at $FFFF
                ld      a, ($FFFF)
                cpl
                ld      e, a            ; E = saved SSL
                dec     b
                jr      z, .sel_sub_asis
                and     h
                or      l
.sel_sub_asis:
                ld      ($FFFF), a
                ld      l, a            ; L = value written to SSL_REGS

                ; Restore original primary slot on Page 3
                ld      a, c
                out     (PSL_STAT), a

                ; Update SLTTBL (no stack usage)
                ld      a, d
                and     $03
                ld      c, a
                ld      b, 0
                ld      a, l            ; A = value written to SSL_REGS
                ld      hl, SLTTBL
                add     hl, bc
                ld      (hl), a
                ret

restore_subslot:
                ; D = E000SSPP, E = saved SSL
                ld      a, d
                rrca
                rrca
                and     $C0
                ld      b, a
                in      a, (PSL_STAT)
                ld      c, a            ; C = saved PSL
                and     $3F
                or      b
                out     (PSL_STAT), a

                ld      a, e
                ld      ($FFFF), a      ; restore SSL_REGS

                ld      a, c
                out     (PSL_STAT), a   ; restore PSL

                ld      a, d
                and     $03
                ld      c, a
                ld      b, 0
                ld      hl, SLTTBL
                add     hl, bc
                ld      (hl), e
                ret

; ==============================================================================
; Trampolines Template (Copied to $F380 in RAM)
; ==============================================================================
tbl_trampolines:
; $F380: RDPRIM (5 bytes: $F380 - $F384)
                out     (PSL_STAT), a
                ld      e, (hl)
                jr      .wrprm1

; $F385: WRPRIM (7 bytes: $F385 - $F38B)
                out     (PSL_STAT), a
                ld      (hl), e
.wrprm1:
                ld      a, d
                out     (PSL_STAT), a
                ret

; $F38C: CLPRIM (12 bytes: $F38C - $F397)
                out     (PSL_STAT), a
                ex      af, af'
                call    $F398           ; call RAM trampoline at $F398 (jp (ix))
                ex      af, af'
                pop     af              ; restore PSL from stack
                out     (PSL_STAT), a
                ex      af, af'
                ret
; $F398: CLPRIM target in RAM
                jp      (ix)

sz_trampolines: equ     $ - tbl_trampolines

; ==============================================================================
; Interrupt Handler (KEYINT / RST 38h)
; ==============================================================================

keyint:
                push    af
                push    bc
                push    de
                push    hl
                push    ix
                push    iy

                ; 1. Read VDP Status Register 0 to clear VBlank interrupt
                in      a, (VDP_STAT)
                ld      (STATFL), a

                ; 2. Call H_KEYI Hook ($FD9A)
                call    H_KEYI

                ; 3. Check VBlank flag (bit 7 of S#0)
                ld      a, (STATFL)
                and     $80
                jr      z, .keyint_done

                ; 4. Update software clock JIFFY ($FC9E)
                ld      hl, (JIFFY)
                inc     hl
                ld      (JIFFY), hl

                ; 5. Scan keyboard row 7 for STOP / CTRL-STOP
                ld      a, 7
                call    snsmat
                bit     4, a            ; Bit 4 = STOP key
                jr      nz, .no_stop
                ; STOP key pressed
                ld      a, 1
                ld      (INTFLG), a
.no_stop:

                ; 6. Call H_TIMI Hook ($FD9F)
                call    H_TIMI

.keyint_done:
                pop     iy
                pop     ix
                pop     hl
                pop     de
                pop     bc
                pop     af
                ei
                reti

; ==============================================================================
; Input Subsystem (SNSMAT, KILBUF, GTSTCK, GTTRIG, GTPAD, GTPDL)
; ==============================================================================

; ------------------------------------------------------------------------------
; SNSMAT: Sense keyboard matrix row
; Input:  A = Row number (0..10)
; Output: A = Column bits (active low: 0 = pressed, 1 = idle)
; ------------------------------------------------------------------------------
snsmat:
                push    bc
                and     $0F
                ld      c, a
                in      a, ($AA)        ; Read PPI Port C
                and     $F0             ; Preserve cassette / click bits
                or      c
                out     ($AA), a        ; Select row on Port C low nibble
                in      a, ($A9)        ; Read column data from PPI Port B
                pop     bc
                ret

; ------------------------------------------------------------------------------
; KILBUF: Clear keyboard buffer
; ------------------------------------------------------------------------------
kilbuf:
                ld      hl, KEYBUF
                ld      (GETPNT), hl
                ld      (PUTPNT), hl
                ret

; ------------------------------------------------------------------------------
; GTSTCK: Get joystick direction
; Input:  A = 0 (cursor keys), 1 (port 1), 2 (port 2)
; Output: A = Direction (0..8)
; ------------------------------------------------------------------------------
gtstck:
                push    bc
                push    de
                push    hl
                cp      0
                jr      nz, .gtstck_port

                ; Cursor keys from keyboard matrix (Row 8)
                ld      a, 8
                call    snsmat
                rrca
                rrca
                rrca
                rrca
                cpl
                and     $0F             ; 0000RDUL
                ld      hl, joypos_kbd_tbl
                ld      d, 0
                ld      e, a
                add     hl, de
                ld      a, (hl)
                pop     hl
                pop     de
                pop     bc
                ret

.gtstck_port:
                ; Read PSG Port A via Reg 14
                ld      e, 0
                dec     a
                jr      z, .sel_port1
                set     6, e            ; Select port 2 (bit 6 of PSG Reg 15)
.sel_port1:
                ld      a, $0F
                call    rdpsg
                and     $BF
                or      e
                ld      e, a
                ld      a, $0F
                call    wrtpsg

                ld      a, $0E
                call    rdpsg
                cpl
                and     $0F             ; 0000RLDU
                ld      hl, joypos_joy_tbl
                ld      d, 0
                ld      e, a
                add     hl, de
                ld      a, (hl)
                pop     hl
                pop     de
                pop     bc
                ret

joypos_joy_tbl:
                ;         0   1   2   3   4   5   6   7
                db      $00,$01,$05,$00,$07,$08,$06,$07
                ;         8   9   A   B   C   D   E   F
                db      $03,$02,$04,$03,$00,$01,$05,$00

joypos_kbd_tbl:
                ;         0   1   2   3   4   5   6   7
                db      $00,$07,$01,$08,$05,$06,$00,$07
                ;         8   9   A   B   C   D   E   F
                db      $03,$00,$02,$01,$04,$05,$03,$00

; ------------------------------------------------------------------------------
; GTTRIG: Get trigger status
; Input:  A = 0 (space), 1 (port 1 trig A), 2 (port 2 trig A), 3 (port 1 trig B), 4 (port 2 trig B)
; Output: A = $FF if pressed, $00 if not pressed
; ------------------------------------------------------------------------------
gttrig:
                cp      5
                jr      nc, .trig_space
                or      a
                jr      nz, .trig_port

.trig_space:
                ; Spacebar: Row 8, bit 0
                ld      a, 8
                call    snsmat
                or      $FE
                inc     a               ; $FF if pressed, $00 if not
                ret

.trig_port:
                dec     a
                push    de
                push    bc
                ld      e, $03          ; Enable trig A+B port 1
                ld      b, a
                and     $01
                jr      z, .sel_trig1
                ld      e, $4C          ; Enable trig A+B port 2, select port 2
.sel_trig1:
                ld      a, $0F
                call    rdpsg
                and     $BF
                or      e
                ld      e, a
                ld      a, $0F
                call    wrtpsg

                ld      a, b
                ld      b, $10          ; Trig A mask (bit 4)
                and     $02
                jr      z, .check_trig
                ld      b, $20          ; Trig B mask (bit 5)

.check_trig:
                ld      a, $0E
                call    rdpsg
                and     b
                sub     1
                sbc     a, a            ; $FF if pressed, $00 if not
                pop     bc
                pop     de
                ret

gtpad:
gtpdl:
                xor     a
                ret

; ==============================================================================
; Video Subsystem (Screen 0/1/2 Initialization & Mode Switching)
; ==============================================================================

; ------------------------------------------------------------------------------
; CHGMOD: Change screen mode
; Input: A = Mode (0 = Screen 0, 1 = Screen 1, 2 = Screen 2)
; ------------------------------------------------------------------------------
chgmod:
                cp      0
                jp      z, initxt
                cp      1
                jp      z, init32
                cp      2
                jp      z, inigrp
                ret

; ------------------------------------------------------------------------------
; INITXT: Initialize SCREEN 0 (Text 1, 40x24)
; ------------------------------------------------------------------------------
initxt:
                call    disscr
                xor     a
                ld      (SCRMOD), a
                ld      (OLDSCR), a

                ld      a, (LINL40)
                ld      (LINLEN), a

                ld      a, 1
                ld      (CSRX), a
                ld      (CSRY), a

                ; Set Base Addresses
                ld      hl, (TXTNAM)
                ld      (NAMBAS), hl
                ld      hl, (TXTCGP)
                ld      (CGPBAS), hl

                ; Set VDP registers for SCREEN 0
                call    settxt

                ; Copy Font to VRAM Pattern Table at $0800
                ld      hl, tbios_font
                ld      de, $0800
                ld      bc, 256 * 8
                call    ldirvm

                ; Clear Screen 0 Name Table (40x24 = 960 bytes of ' ')
                ld      hl, $0000
                ld      bc, 960
                ld      a, ' '
                call    filvrm

                call    chgclr
                call    enascr
                ret

settxt:
                ; VDP R#0 = 0
                ld      b, 0
                ld      c, 0
                call    wrtvdp

                ; VDP R#1: 16K VRAM ($80), IE0 ($20), M1 ($10)
                ld      a, (RG1SAV)
                and     $43             ; preserve display enable and sprite bits
                or      $B0             ; 16K VRAM ($80) + IE0 ($20) + M1 ($10)
                ld      b, a
                ld      c, 1
                call    wrtvdp

                ; VDP R#2 = Name Table ($0000 >> 10 = $00)
                ld      b, 0
                ld      c, 2
                call    wrtvdp

                ; VDP R#4 = Pattern Table ($0800 >> 11 = $01)
                ld      b, 1
                ld      c, 4
                call    wrtvdp
                ret

; ------------------------------------------------------------------------------
; INIT32: Initialize SCREEN 1 (Graphic 1, 32x24)
; ------------------------------------------------------------------------------
init32:
                call    disscr
                ld      a, 1
                ld      (SCRMOD), a
                ld      (OLDSCR), a

                ld      a, (LINL32)
                ld      (LINLEN), a

                ld      a, 1
                ld      (CSRX), a
                ld      (CSRY), a

                ; Set Base Addresses
                ld      hl, (T32NAM)
                ld      (NAMBAS), hl
                ld      hl, (T32CGP)
                ld      (CGPBAS), hl
                ld      hl, (T32ATR)
                ld      (ATRBAS), hl
                ld      hl, (T32PAT)
                ld      (PATBAS), hl

                ; Set VDP registers for SCREEN 1
                call    sett32

                ; Copy Font to VRAM Pattern Table at $0000
                ld      hl, tbios_font
                ld      de, $0000
                ld      bc, 256 * 8
                call    ldirvm

                ; Color table at $2000: 32 bytes (FORCLR << 4 | BAKCLR)
                ld      a, (FORCLR)
                sla     a
                sla     a
                sla     a
                sla     a
                ld      b, a
                ld      a, (BAKCLR)
                and     $0F
                or      b
                ld      hl, $2000
                ld      bc, 32
                call    filvrm

                ; Clear Sprites ($1B00, 128 bytes with $D0)
                call    clrspr

                ; Clear Screen 1 Name Table (32x24 = 768 bytes of ' ')
                ld      hl, $1800
                ld      bc, 768
                ld      a, ' '
                call    filvrm

                call    chgclr
                call    enascr
                ret

sett32:
                ; VDP R#0 = 0
                ld      b, 0
                ld      c, 0
                call    wrtvdp

                ; VDP R#1: 16K VRAM ($80), IE0 ($20)
                ld      a, (RG1SAV)
                and     $43             ; preserve display enable and sprite bits
                or      $A0             ; 16K VRAM ($80) + IE0 ($20)
                ld      b, a
                ld      c, 1
                call    wrtvdp

                ; VDP R#2 = Name Table ($1800 >> 10 = $06)
                ld      b, $06
                ld      c, 2
                call    wrtvdp

                ; VDP R#3 = Color Table ($2000 >> 6 = $80)
                ld      b, $80
                ld      c, 3
                call    wrtvdp

                ; VDP R#4 = Pattern Table ($0000 >> 11 = $00)
                ld      b, $00
                ld      c, 4
                call    wrtvdp

                ; VDP R#5 = Sprite Attribute Table ($1B00 >> 7 = $36)
                ld      b, $36
                ld      c, 5
                call    wrtvdp

                ; VDP R#6 = Sprite Pattern Generator ($3800 >> 11 = $07)
                ld      b, $07
                ld      c, 6
                call    wrtvdp
                ret

; ------------------------------------------------------------------------------
; INIGRP: Initialize SCREEN 2 (Graphic 2, 256x192)
; ------------------------------------------------------------------------------
inigrp:
                call    disscr
                ld      a, 2
                ld      (SCRMOD), a
                ld      (OLDSCR), a

                ld      hl, (GRPNAM)
                ld      (NAMBAS), hl
                ld      hl, (GRPCGP)
                ld      (CGPBAS), hl
                ld      hl, (GRPATR)
                ld      (ATRBAS), hl
                ld      hl, (GRPPAT)
                ld      (PATBAS), hl

                ; Set VDP registers for SCREEN 2
                call    setgrp

                ; Name Table: 3 blocks of 0..255 (768 bytes)
                ld      hl, $1800
                call    setwrt
                ld      c, 3
.grp_b_lp:
                xor     a
.grp_c_lp:
                out     (VDP_DATA), a
                inc     a
                jr      nz, .grp_c_lp
                dec     c
                jr      nz, .grp_b_lp

                ; Clear Sprites
                call    clrspr

                call    chgclr
                call    enascr
                ret

setgrp:
                ; VDP R#0 = $02 (Screen 2 bit set)
                ld      b, $02
                ld      c, 0
                call    wrtvdp

                ; VDP R#1: 16K VRAM ($80), IE0 ($20)
                ld      a, (RG1SAV)
                and     $43             ; preserve display enable and sprite bits
                or      $A0             ; 16K VRAM ($80) + IE0 ($20)
                ld      b, a
                ld      c, 1
                call    wrtvdp

                ; VDP R#2 = Name Table ($1800 >> 10 = $06)
                ld      b, $06
                ld      c, 2
                call    wrtvdp

                ; VDP R#3 = Color Table ($2000 >> 6 | $7F = $FF)
                ld      b, $FF
                ld      c, 3
                call    wrtvdp

                ; VDP R#4 = Pattern Table ($0000 >> 11 | $03 = $03)
                ld      b, $03
                ld      c, 4
                call    wrtvdp

                ; VDP R#5 = Sprite Attribute Table ($1B00 >> 7 = $36)
                ld      b, $36
                ld      c, 5
                call    wrtvdp

                ; VDP R#6 = Sprite Pattern Generator ($3800 >> 11 = $07)
                ld      b, $07
                ld      c, 6
                call    wrtvdp
                ret

inimlt:
setmlt:
                ret

clrspr:
                ; Fill Sprite Attribute Table ($1B00..$1B7F) with $D0 (disables all sprites)
                ld      hl, $1B00
                ld      bc, 128
                ld      a, $D0
                jp      filvrm

chgclr:
                ; Set VDP R#7 = (FORCLR << 4) | (BAKCLR & $0F)
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
                jp      wrtvdp

; ==============================================================================
; Cartridge Detection & Execution ($4000 & $8000 "AB" headers)
; ==============================================================================

search_cartridges:
                ; Search primary slot 1 first (standard cartridge slot)
                ld      a, 1
                call    check_slot_cartridge

                ; Search primary slot 2 (second cartridge slot)
                ld      a, 2
                call    check_slot_cartridge
                ret

check_slot_cartridge:
                ld      b, a            ; B = slot to test (1 or 2)

                ; Check for "AB" signature at $4000
                ld      hl, $4000
                ld      a, b
                call    rdslt
                cp      'A'
                ret     nz
                ld      hl, $4001
                ld      a, b
                call    rdslt
                cp      'B'
                ret     nz

                ; Cartridge signature found!
                ; Read INIT vector at $4002-$4003
                ld      hl, $4002
                ld      a, b
                call    rdslt
                ld      e, a
                ld      hl, $4003
                ld      a, b
                call    rdslt
                ld      d, a
                ld      a, d
                or      e
                jr      z, .try_exec

                ; Call Cartridge INIT via CALSLT
                ; Reg C = slot ID (required by games for slot detection)
                push    de
                pop     ix              ; IX = INIT address
                ld      iyh, b
                ld      iyl, b          ; IY = slot ID
                ld      c, b
                call    calslt
                di

.try_exec:
                ; Read EXEC vector at $4004-$4005
                ld      hl, $4004
                ld      a, b
                call    rdslt
                ld      e, a
                ld      hl, $4005
                ld      a, b
                call    rdslt
                ld      d, a
                ld      a, d
                or      e
                ret     z

                ; Jump to Cartridge EXEC via CALSLT
                push    de
                pop     ix              ; IX = EXEC address
                ld      iyh, b
                ld      iyl, b
                ld      c, b
                jp      calslt

; ==============================================================================
; Extension ROM Scanner & Initializer (Disk ROM in Slot 3-3)
; ==============================================================================

search_ext_roms:
                push    bc
                push    de
                push    hl

                ; Check Slot 3-3 (Disk ROM: $80 | (3 << 2) | 3 = $8F)
                ld      a, (EXPTBL + 3)
                and     $80
                jr      z, .done_ext

                ; Check for "AB" signature at $4000 in Slot 3-3
                ld      a, $8F
                ld      hl, $4000
                call    rdslt
                cp      $41             ; 'A'
                jr      nz, .done_ext
                ld      a, $8F
                ld      hl, $4001
                call    rdslt
                cp      $42             ; 'B'
                jr      nz, .done_ext

                ; Read INIT vector at $4002
                ld      a, $8F
                ld      hl, $4002
                call    rdslt
                ld      e, a
                ld      a, $8F
                ld      hl, $4003
                call    rdslt
                ld      d, a            ; DE = INIT address
                ld      a, d
                or      e
                jr      z, .done_ext

                ; Call INIT via CALSLT: IX = address, IYh = Slot ID ($8F)
                push    de
                pop     ix
                ld      iy, $8F00
                call    calslt

.done_ext:
                pop     hl
                pop     de
                pop     bc
                ret

; ==============================================================================
; Hardware I/O & VDP Primitives
; ==============================================================================

dcompr:
                ; Compare HL with DE: ZF=1 if equal, CF=1 if HL < DE
                ld      a, h
                cp      d
                ret     nz
                ld      a, l
                cp      e
                ret

wrtvdp:
                ; B = data, C = register
                ld      a, b
                out     (VDP_ADDR), a
                ld      a, c
                or      $80
                out     (VDP_ADDR), a
                ; Update register save area (RG0SAV..RG7SAV)
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
rdvdp:
                in      a, (VDP_STAT)
                ret

rdvrm:
                ; HL = VRAM address -> A = byte
                call    setrd
                in      a, (VDP_DATA)
                ret

wrtvrm:
                ; HL = VRAM address, A = byte
                push    af
                call    setwrt
                pop     af
                out     (VDP_DATA), a
                ret

setrd:
                ; HL = VRAM address
                ld      a, l
                out     (VDP_ADDR), a
                ld      a, h
                and     $3F
                out     (VDP_ADDR), a
                ret

setwrt:
                ; HL = VRAM address
                ld      a, l
                out     (VDP_ADDR), a
                ld      a, h
                and     $3F
                or      $40
                out     (VDP_ADDR), a
                ret

filvrm:
                ; HL = VRAM addr, BC = length, A = fill data
                push    af
                call    setwrt
                pop     af
.fil_lp:
                out     (VDP_DATA), a
                dec     bc
                ld      d, a
                ld      a, b
                or      c
                ld      a, d
                jr      nz, .fil_lp
                ret

ldirmv:
                ; HL = VRAM, DE = RAM, BC = count
                call    setrd
.mv_lp:
                in      a, (VDP_DATA)
                ld      (de), a
                inc     de
                dec     bc
                ld      a, b
                or      c
                jr      nz, .mv_lp
                ret

ldirvm:
                ; HL = RAM, DE = VRAM, BC = count
                ex      de, hl
                call    setwrt
                ex      de, hl
.vm_lp:
                ld      a, (hl)
                out     (VDP_DATA), a
                inc     hl
                dec     bc
                ld      a, b
                or      c
                jr      nz, .vm_lp
                ret

disscr:
                ld      a, (RG1SAV)
                and     $BF             ; Display blank
                ld      b, a
                ld      c, 1
                jr      wrtvdp

enascr:
                ld      a, (RG1SAV)
                or      $40             ; Display enable
                ld      b, a
                ld      c, 1
                jr      wrtvdp

gicini:
                ; Initialize PSG (all channels quiet)
                push    af
                push    bc
                push    de
                ld      e, 0
                ld      a, 8
                call    wrtpsg          ; R8 (ch A volume) = 0
                inc     a
                call    wrtpsg          ; R9 (ch B volume) = 0
                inc     a
                call    wrtpsg          ; R10 (ch C volume) = 0
                ld      a, 7
                ld      e, $B8          ; R7 mixer: tones enabled, noise disabled
                call    wrtpsg
                pop     de
                pop     bc
                pop     af
                ret

wrtpsg:
                ; Input: A = reg (0..15), E = data
                out     (PSG_REGS), a
                push    af
                ld      a, e
                out     (PSG_DATA), a
                pop     af
                ret

rdpsg:
                ; Input: A = reg -> Output: A = data
                out     (PSG_REGS), a
                in      a, (PSG_STAT)
                ret

chsns:
                ; Zero flag = 1 if no key pressed
                xor     a
                inc     a               ; clear ZF
                ret

chget:
                ; Wait for key
                ld      a, ' '
                ret

chput:
                ; Output character in A
                ret

cls_z:
                xor     a
                ret

posit:
                ld      a, h
                ld      (CSRX), a
                ld      a, l
                ld      (CSRY), a
                ret

beep:
                ret

calpat:
calatr:
gspsiz:
grpprt:
                ret

strtms:
                ; Start music playback (sets MUSICF $FB3F)
                push    af
                xor     a
                ld      (MUSICF), a
                pop     af
                ret
lptout:
lptstt:
cnvchr:
pinlin:
inlin:
qinlin:
breakx:
iscntc:
ckcntc:
fnksb:
erafnk:
dspfnk:
totext:
synchr:
chrgtr:
outdo:
getypr:
initio:
inifnk:
nmi:
tapion:
tapin:
tapiof:
tapoon:
tapout:
tapoof:
stmotr:
lftq:
putq:
rightc:
leftc:
upc:
tupc:
downc:
tdownc:
scalxy:
mapxy:
fetchc:
storec:
setatr:
readc:
setc:
nsetcx:
gtaspc:
pntini:
scanr:
scanl:
chgcap:
                xor     a
                ret

chgsnd:
                or      a
                ld      a, $0E          ; bit 7 reset command
                jr      z, .chgsnd_w
                inc     a               ; bit 7 set command ($0F)
.chgsnd_w:
                out     ($AB), a
                ret

rslreg:
                in      a, (PSL_STAT)
                ret

wslreg:
                out     (PSL_STAT), a
                ret

phydio:
format:
isflio:
outdlp:
getvcp:
getvc2:
calbas:
                xor     a
                ret

; ------------------------------------------------------------------------------
; CHGCPU ($0180): Change CPU mode on turbo R
; Input:  A = bits 1-0: 0 = Z80, 1 = R800 ROM, 2 = R800 DRAM
;             bit 7: 1 = update CPU LED (port $A7 bit 7)
; ------------------------------------------------------------------------------
chgcpu:
                push    af
                push    bc
                ld      b, a            ; B = target mode + LED flag
                and     $03
                cp      3
                jr      z, .chgcpu_end   ; mode 3 is invalid
                ld      c, a            ; C = target mode (0..2)
                ld      a, i
                push    af              ; save interrupt state
                di

                push    bc
                push    de
                push    hl
                push    ix
                push    iy

                ld      (CHGCPU_SPSAV), sp
                ld      hl, $A55A
                ld      (CHGCPU_MAGIC), hl

                ; Select S1990 Register 6 (CPU mode select)
                ld      a, 6
                out     ($E4), a

                ; S1990 Register 6 mode mappings:
                ; Mode 0: Z80 ($60)
                ; Mode 1: R800 ROM mode ($40)
                ; Mode 2: R800 DRAM mode ($00)
                ld      a, c
                cp      1
                ld      a, $60
                jr      c, .chgcpu_out
                ld      a, $40
                jr      z, .chgcpu_out
                xor     a
.chgcpu_out:
                out     ($E5), a        ; CPU switches mode here!

chgcpu_resume:
                ld      sp, (CHGCPU_SPSAV)
                ld      hl, 0
                ld      (CHGCPU_MAGIC), hl
                im      1

                pop     iy
                pop     ix
                pop     hl
                pop     de
                pop     bc

                ld      a, c
                ld      (CPUMOD), a     ; store active CPU mode

                ; Update turbo R CPU LED if bit 7 of B was requested
                bit     7, b
                jr      z, .chgcpu_no_led
                ld      a, c
                or      a
                ld      a, 0
                jr      z, .chgcpu_set_led
                ld      a, $80          ; LED ON in R800 mode
.chgcpu_set_led:
                out     ($A7), a
.chgcpu_no_led:
                pop     af
                jp      po, .chgcpu_end
                ei
.chgcpu_end:
                pop     bc
                pop     af
                ret

; ------------------------------------------------------------------------------
; GETCPU ($0183): Get CPU mode
; Output: A = 0 (Z80), 1 (R800 ROM), 2 (R800 DRAM)
; ------------------------------------------------------------------------------
getcpu:
                ld      a, (CPUMOD)
                ret

; ------------------------------------------------------------------------------
; PCMPLY ($0186): Play PCM audio buffer
; Input:  HL = buffer address
;         BC = buffer length
; ------------------------------------------------------------------------------
pcmply:
                push    af
                push    bc
                push    de
                push    hl
                ld      a, 3
                out     ($A5), a        ; Unmute PCM DAC
.pcmply_lp:
                ld      a, b
                or      c
                jr      z, .pcmply_done
                ld      a, (hl)
                out     ($A4), a        ; Write sample to PCM DAC
                inc     hl
                dec     bc
                jr      .pcmply_lp
.pcmply_done:
                pop     hl
                pop     de
                pop     bc
                pop     af
                ret

; ------------------------------------------------------------------------------
; PCMREC ($0189): Record PCM audio into buffer
; Input:  HL = buffer address
;         BC = buffer length
; ------------------------------------------------------------------------------
pcmrec:
                push    af
                push    bc
                push    hl
.pcmrec_lp:
                ld      a, b
                or      c
                jr      z, .pcmrec_done
                in      a, ($A4)        ; Read sample from PCM ADC
                ld      (hl), a
                inc     hl
                dec     bc
                jr      .pcmrec_lp
.pcmrec_done:
                pop     hl
                pop     bc
                pop     af
                ret

; ------------------------------------------------------------------------------
; CHKSLZ: Search slots for Sub-ROM ("CD" header at $0000)
; Output: CF = 1 if found, (EXBRSA) = slot ID
;         CF = 0 if not found, (EXBRSA) = 0
; ------------------------------------------------------------------------------
chkslz:
                push    bc
                push    de
                push    hl

                ; Search primary slots 0..3
                ld      b, 0            ; B = primary slot (0..3)
.chkslz_pslot_lp:
                ; Check if primary slot B is expanded in EXPTBL
                ld      hl, EXPTBL
                ld      e, b
                ld      d, 0
                add     hl, de
                ld      a, (hl)
                and     $80
                jr      z, .chkslz_check_prim

                ; Slot B is expanded: check subslots 0..3
                ld      c, 0            ; C = subslot (0..3)
.chkslz_sslot_lp:
                ; Construct Slot ID: $80 | (C << 2) | B
                ld      a, c
                sla     a
                sla     a
                or      b
                or      $80
                call    .test_subrom_slot
                jr      c, .chkslz_found

                inc     c
                ld      a, c
                cp      4
                jr      c, .chkslz_sslot_lp
                jr      .chkslz_pslot_next

.chkslz_check_prim:
                ; Primary slot unexpanded: Slot ID = B
                ld      a, b
                call    .test_subrom_slot
                jr      c, .chkslz_found

.chkslz_pslot_next:
                inc     b
                ld      a, b
                cp      4
                jr      c, .chkslz_pslot_lp

                ; Sub-ROM not found
                xor     a
                ld      (EXBRSA), a
                or      a               ; clear CF
                jr      .chkslz_done

.chkslz_found:
                ld      (EXBRSA), a
                scf                     ; set CF

.chkslz_done:
                pop     hl
                pop     de
                pop     bc
                ret

.test_subrom_slot:
                ; Input: A = Slot ID. Returns CF=1 if Sub-ROM found ("CD" at $0000)
                push    bc
                push    de
                push    af              ; save Slot ID

                ; Read byte 0 at $0000 via RDSLT
                ld      hl, $0000
                call    rdslt
                cp      'C'
                jr      nz, .not_subrom

                ; Read byte 1 at $0001 via RDSLT
                pop     af              ; restore Slot ID
                push    af
                ld      hl, $0001
                call    rdslt
                cp      'D'
                jr      nz, .not_subrom

                ; Found!
                pop     af              ; A = Slot ID
                pop     de
                pop     bc
                scf
                ret

.not_subrom:
                pop     af
                pop     de
                pop     bc
                or      a               ; clear CF
                ret

; ------------------------------------------------------------------------------
; EXTROM: Call routine in Sub-ROM
; Input:  IX = Address in Sub-ROM
; Output: Dependent on routine called
; ------------------------------------------------------------------------------
extrom:
                push    af
                ld      a, (EXBRSA)
                or      a
                jr      z, .no_subrom_ext
                ld      iyh, a
                ld      iyl, a          ; IY = Sub-ROM slot ID
                pop     af
                jp      calslt

.no_subrom_ext:
                pop     af
                ret

; ------------------------------------------------------------------------------
; SUBROM: Call routine in Sub-ROM
; Input:  IX = Address in Sub-ROM
; ------------------------------------------------------------------------------
subrom:
                jp      extrom

; ------------------------------------------------------------------------------
; CHKNEW: Check if current screen mode is bitmap mode (Screen 5..12)
; Output: CF = 1 if text/char mode (Screen 0..4)
;         CF = 0 if bitmap mode (Screen 5..12)
; ------------------------------------------------------------------------------
chknew:
                ld      a, (SCRMOD)
                cp      5
                ret

; ------------------------------------------------------------------------------
; 128K VRAM Primitives (NSETRD, NSETWR, NRDVRM, NWRVRM, BIGFIL)
; ------------------------------------------------------------------------------
nsetrd:
                ; Input: A = bit 16, HL = bits 0-15
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
                call    wrtvdp          ; Set VDP R#14 (bank)
                ld      a, l
                out     (VDP_ADDR), a
                ld      a, h
                and     $3F
                out     (VDP_ADDR), a   ; Set read address (bits 0-13)
                ret

nsetwr:
                ; Input: A = bit 16, HL = bits 0-15
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
                call    wrtvdp          ; Set VDP R#14 (bank)
                ld      a, l
                out     (VDP_ADDR), a
                ld      a, h
                and     $3F
                or      $40             ; bit 6 = write setup
                out     (VDP_ADDR), a
                ret

nrdvrm:
                ; Input: A = bit 16, HL = bits 0-15 -> A = byte
                call    nsetrd
                in      a, (VDP_DATA)
                ret

nwrvrm:
                ; Input: A = bit 16, HL = bits 0-15, E = byte
                call    nsetwr
                ld      a, e
                out     (VDP_DATA), a
                ret

bigfil:
                ; Input: HL = start, BC = length, A = data, IXH = bit 16
                push    af
                ld      a, ixh
                call    nsetwr
                pop     af
.bigfil_lp:
                out     (VDP_DATA), a
                dec     bc
                ld      e, a
                ld      a, b
                or      c
                ld      a, e
                jr      nz, .bigfil_lp
                ret

eol:
                ; Erase to end of line from cursor
                push    bc
                push    de
                push    hl
                ld      a, (CSRY)
                dec     a
                ld      l, a
                ld      h, 0
                ld      a, (LINLEN)
                ld      e, a
                ld      d, 0
                ; HL = (CSRY - 1) * LINLEN
                call    mul_hl_e
                ld      a, (CSRX)
                dec     a
                ld      e, a
                add     hl, de          ; HL = name table offset
                ld      de, (NAMBAS)
                add     hl, de          ; HL = VRAM address
                ld      a, (LINLEN)
                ld      b, a
                ld      a, (CSRX)
                sub     b
                neg
                inc     a               ; A = count of chars to end of line
                ld      b, a
                call    setwrt
                ld      a, ' '
.eol_lp:
                out     (VDP_DATA), a
                djnz    .eol_lp
                pop     hl
                pop     de
                pop     bc
                ret

mul_hl_e:
                ; HL = L * E
                ld      b, l
                ld      hl, 0
                ld      a, b
                or      a
                ret     z
.mul_lp:
                add     hl, de
                djnz    .mul_lp
                ret

rdres:
wrres:
                xor     a
                ret

; ==============================================================================
; Embedded Character Font (Eric Boon Public / BSD Font)
; ==============================================================================

tbios_font:
                include "font_us.inc"

; ==============================================================================
; Pad to full 32 KiB (0x8000)
; ==============================================================================

                ds      $8000 - $
