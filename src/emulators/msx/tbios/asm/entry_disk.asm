; ==============================================================================
; T-DOS (B-MSX Disk Operating System) — Clean-Room Disk BIOS ROM
; Target: MSX Disk BIOS (16 KiB, Base 0x4000)
; Assembler: Pasmo Z80
; Standard Disk ROM jump table: $4010 DSKIO, $4013 DSKCHG, $4016 GETDPB,
; $4019 CHOICE, $401C DSKFMT, $401F MTOFF, $402D DSKSLT
; References:
;   [SPEC:MSX-Datapack §2] Disk ROM jump table and DPB layout
;   [CB:disk.asm:dskio] Multi-sector transfer & page boundary logic
;   [GRAUW:dos2_environment.php] Page-0 RAM layout & EXTBIO mapper table
; ==============================================================================

                org     $4000

                db      "AB"
                dw      tdos_init       ; INIT
                dw      0               ; STATEMENT
                dw      0               ; DEVICE
                dw      0               ; BASIC TEXT
                dw      0, 0, 0

; ------------------------------------------------------------------------------
; Disk BIOS Jump Table ($4010..$402D)
; ------------------------------------------------------------------------------

; $4010 DSKIO: Disk Read/Write
; Input:  CF = 0:Read, 1:Write; A = Drive (0=A:); B = Sectors; C = Media ID;
;         DE = Start Sector; HL = Buffer
; Output: CF = 0:Success, 1:Error; A = Error code; B = Sectors remaining
                ds      $4010 - $
                jp      tdos_dskio

; $4013 DSKCHG: Check disk change
; Output: B = 1:Unchanged, 0:Unknown, -1:Changed; CF = 0:OK, 1:Error
                ds      $4013 - $
                jp      tdos_dskchg

; $4016 GETDPB: Get Drive Parameter Block
; Input:  A = Drive; B = First FAT byte; C = Media descriptor; HL = Destination
; Output: [HL..HL+17] filled with 18-byte DPB
                ds      $4016 - $
                jp      tdos_getdpb

; $4019 CHOICE: Choice string for FORMAT
; Output: HL = ASCIIz string or 0 if single format
                ds      $4019 - $
                jp      tdos_choice

; $401C DSKFMT: Format disk
; Output: CF = 0:OK, 1:Error; A = Error code
                ds      $401C - $
                jp      tdos_dskfmt

; $401F MTOFF: Motor off
                ds      $401F - $
                jp      tdos_mtoff

; $4022 BASIC: Disk basic cold boot vector
                ds      $4022 - $
                scf
                ret

; $4026 FORMAT: Disk format vector
                ds      $4026 - $
                scf
                ret

; $4029 DSKSTP: Stop motor on all interfaces
                ds      $4029 - $
                ret

; $402D DSKSLT: Return Disk ROM slot ID
                ds      $402D - $
                jp      tdos_dskslt

; ------------------------------------------------------------------------------
; System Variable Addresses
; ------------------------------------------------------------------------------

DRVINF:         equ     $FB21           ; Number of drives & Disk ROM slot ID
H_BDOS:         equ     $F37D           ; BDOS entry point in page 3
H_BDOS_ALT:     equ     $F331           ; BDOS inter-slot call area
H_PHYD:         equ     $F340           ; PHYDIO hook
H_RUNC:         equ     $FECB           ; Disk ROM boot hook
H_EXTB:         equ     $FFCA           ; Extended BIOS hook
PSL_STAT:       equ     $A8             ; Primary slot select port
SSL_REGS:       equ     $FFFF           ; Secondary slot select address

; ------------------------------------------------------------------------------
; Initialization (tdos_init)
; ------------------------------------------------------------------------------

tdos_init:
                push    af
                push    bc
                push    de
                push    hl

                ; 1. Set up DRVINF at $FB21 (1 drive, Disk ROM slot ID)
                call    tdos_dskslt     ; A = Slot ID
                ld      hl, DRVINF
                ld      (hl), 1         ; 1 drive
                inc     hl
                ld      (hl), a         ; Disk ROM Slot ID

                ; 2. Setup BDOS entry point at $F37D ($C3, low, high)
                ld      hl, H_BDOS
                ld      (hl), $C3       ; JP
                inc     hl
                ld      (hl), low(tdos_bdos_entry)
                inc     hl
                ld      (hl), high(tdos_bdos_entry)

                ; 3. Setup H_RUNC hook at $FECB (RST $30 inter-slot call to tdos_boot)
                call    tdos_dskslt
                ld      c, a            ; C = Slot ID
                ld      hl, H_RUNC
                ld      (hl), $F7       ; RST $30
                inc     hl
                ld      (hl), c         ; Slot ID
                inc     hl
                ld      (hl), low(tdos_boot)
                inc     hl
                ld      (hl), high(tdos_boot)
                inc     hl
                ld      (hl), $C9       ; RET

                ; 4. Setup H_EXTB hook at $FFCA for DOS 2 Memory Mapper table
                ld      hl, H_EXTB
                ld      (hl), $C3       ; JP
                inc     hl
                ld      (hl), low(tdos_extbio)
                inc     hl
                ld      (hl), high(tdos_extbio)

                ; 5. Initialize bank switching at $6000 and $8000
                ld      a, 1
                ld      ($6000), a
                ld      ($8000), a

                pop     hl
                pop     de
                pop     bc
                pop     af
                ret

; ------------------------------------------------------------------------------
; Slot Identification (tdos_dskslt)
; Returns Disk ROM Slot ID in A (Slot 3-3 on standard B-MSX machines: $8F)
; ------------------------------------------------------------------------------

tdos_dskslt:
                ; Primary slot is 3, secondary subslot is 3:
                ; $80 | (3 << 2) | 3 = $8F
                ld      a, $8F
                ret

; ------------------------------------------------------------------------------
; DSKIO: Multi-sector Read and Write Driver (Corner Case K3)
; ------------------------------------------------------------------------------

tdos_dskio:
                ; Check drive: only drive 0 (A:) supported
                push    af              ; Save caller's CF (read/write) and A (drive)
                cp      1
                jr      c, .drive_ok
                pop     af
                ld      a, 12           ; Error 12 = Other errors (invalid drive)
                scf
                ret

.drive_ok:
                ; Check sector count
                ld      a, b
                or      a
                jr      nz, .check_rw
                pop     af              ; 0 sectors -> success
                xor     a
                ret

.check_rw:
                pop     af              ; Restore caller's CF (read vs write)
                jr      c, .dskio_write

.dskio_read_loop:
                push    bc
                push    de
                push    hl

                ; Calculate bank = 2 + (DE >> 4)
                ld      a, d
                rrca
                rrca
                rrca
                rrca
                and     $0F
                ld      c, a            ; high 4 bits of DE >> 4
                ld      a, e
                rlca
                rlca
                rlca
                rlca
                and     $F0
                or      c
                rrca
                rrca
                rrca
                rrca
                ld      a, d
                sla     a
                sla     a
                sla     a
                sla     a
                ld      b, a
                ld      a, e
                srl     a
                srl     a
                srl     a
                srl     a
                or      b
                add     a, 2            ; Bank = 2 + (DE / 16)
                ld      c, a            ; C = Bank

                ; Calculate bank offset: (DE & 15) << 9
                ld      a, e
                and     $0F
                add     a, a            ; high byte of offset = (DE & 15) * 2
                ld      d, a
                ld      e, 0            ; DE = offset within bank (0x0000..0x1E00)

                ; Avoid window collision with destination HL:
                ; If HL < $8000, map bank at $8000
                ; If HL >= $8000, map bank at $6000
                ld      a, h
                cp      $80
                jr      nc, .map_6000_rd

                ; Map data bank at $8000
                ld      a, c
                ld      ($8000), a
                ex      de, hl          ; HL = offset within bank, DE = target
                ld      a, h
                add     a, $80          ; Base $8000
                ld      h, a
                ld      bc, 512
                ldir
                ex      de, hl          ; HL = updated target address
                jr      .sec_rd_done

.map_6000_rd:
                ; Map data bank at $6000
                ld      a, c
                ld      ($6000), a
                ex      de, hl          ; HL = offset within bank, DE = target
                ld      a, h
                add     a, $60          ; Base $6000
                ld      h, a
                ld      bc, 512
                ldir
                ex      de, hl          ; HL = updated target address

.sec_rd_done:
                pop     hl
                ld      bc, 512
                add     hl, bc
                pop     de
                inc     de
                pop     bc
                djnz    .dskio_read_loop

                ; Restore default banks
                ld      a, 1
                ld      ($6000), a
                ld      ($8000), a
                xor     a               ; CF = 0, A = 0
                ret

.dskio_write:
                ; Sector writing loop
.dskio_write_loop:
                push    bc
                push    de
                push    hl

                ; Calculate bank = 2 + (DE >> 4)
                ld      a, d
                sla     a
                sla     a
                sla     a
                sla     a
                ld      b, a
                ld      a, e
                srl     a
                srl     a
                srl     a
                srl     a
                or      b
                add     a, 2
                ld      c, a

                ; Calculate bank offset: (DE & 15) << 9
                ld      a, e
                and     $0F
                add     a, a
                ld      d, a
                ld      e, 0

                ; If HL < $8000, map at $8000; else map at $6000
                ld      a, h
                cp      $80
                jr      nc, .map_6000_wr

                ld      a, c
                ld      ($8000), a
                ld      a, d
                add     a, $80
                ld      d, a            ; DE = $8000 + offset
                ld      bc, 512
                ldir
                jr      .sec_wr_done

.map_6000_wr:
                ld      a, c
                ld      ($6000), a
                ld      a, d
                add     a, $60
                ld      d, a            ; DE = $6000 + offset
                ld      bc, 512
                ldir

.sec_wr_done:
                pop     hl
                ld      bc, 512
                add     hl, bc
                pop     de
                inc     de
                pop     bc
                djnz    .dskio_write_loop

                ld      a, 1
                ld      ($6000), a
                ld      ($8000), a
                xor     a
                ret

; ------------------------------------------------------------------------------
; DSKCHG: Check Disk Change
; ------------------------------------------------------------------------------

tdos_dskchg:
                ld      b, 1            ; 1 = Disk unchanged
                xor     a               ; CF = 0, A = 0
                ret

; ------------------------------------------------------------------------------
; GETDPB: Drive Parameter Block Builder (18 bytes per MSX-Datapack)
; ------------------------------------------------------------------------------

tdos_getdpb:
                push    de
                push    bc
                push    hl

                ; Check media descriptor in C
                ld      a, c
                cp      $F8
                jr      z, .dpb_360k

                ; Standard 720K 2DD ($F9)
                ld      de, .dpb_data_720k
                jr      .copy_dpb

.dpb_360k:
                ld      de, .dpb_data_360k

.copy_dpb:
                ex      de, hl          ; HL = source, DE = dest
                ld      bc, 18
                ldir

                pop     hl
                pop     bc
                pop     de
                xor     a               ; CF = 0, A = 0
                ret

.dpb_data_720k:
                db      $F9             ; MEDIA ($F9)
                dw      $0200           ; SECSIZ (512)
                db      $0F             ; DIRMSK (15)
                db      $04             ; DIRSHFT (4)
                db      $01             ; CLUSMSK (2 sec/clus - 1)
                db      $02             ; CLUSSHFT
                dw      $0001           ; FIRFAT (Sector 1)
                db      $02             ; FATCNT (2 FATs)
                db      $70             ; MAXENT (112 entries)
                dw      $000E           ; FIRREC (First data sector = 14)
                dw      $02CA           ; MAXCLUS (714 clusters)
                db      $03             ; FATSIZ (3 sectors/FAT)
                dw      $0007           ; FIRDIR (First directory sector = 7)

.dpb_data_360k:
                db      $F8             ; MEDIA ($F8)
                dw      $0200           ; SECSIZ (512)
                db      $0F             ; DIRMSK (15)
                db      $04             ; DIRSHFT (4)
                db      $01             ; CLUSMSK (2 sec/clus - 1)
                db      $02             ; CLUSSHFT
                dw      $0001           ; FIRFAT (Sector 1)
                db      $02             ; FATCNT (2 FATs)
                db      $70             ; MAXENT (112 entries)
                dw      $000C           ; FIRREC (First data sector = 12)
                dw      $0164           ; MAXCLUS (356 clusters)
                db      $02             ; FATSIZ (2 sectors/FAT)
                dw      $0005           ; FIRDIR (First directory sector = 5)

; ------------------------------------------------------------------------------
; CHOICE, DSKFMT, MTOFF
; ------------------------------------------------------------------------------

tdos_choice:
                ld      hl, 0           ; No multi-format menu
                ret

tdos_dskfmt:
                xor     a               ; Format success
                ret

tdos_mtoff:
                ret

; ------------------------------------------------------------------------------
; BDOS Dispatcher Stub
; ------------------------------------------------------------------------------

tdos_bdos_entry:
                out     ($D4), a        ; Host BDOS acceleration trap
                ret

; ------------------------------------------------------------------------------
; Extended BIOS (EXTBIO $FFCA) Handler for DOS 2 Memory Mapper
; ------------------------------------------------------------------------------

tdos_extbio:
                push    af
                ld      a, d
                cp      4               ; Device 4 = MSX-DOS 2
                jr      nz, .extbio_pass
                ld      a, e
                cp      2               ; Function 2 = Memory Mapper table
                jr      nz, .extbio_pass

                ; Return Memory Mapper information:
                ; A = segment count (32 for 512K, up to 256 for 4 MiB)
                ; B = mapper slot ID
                ; HL = address of 16-entry mapper jump table
                pop     af
                call    tdos_dskslt
                ld      b, a            ; B = Slot ID
                ld      a, 32           ; Default 32 x 16K segments = 512 KiB
                ld      hl, tdos_mapper_table
                ret

.extbio_pass:
                pop     af
                ret

; ------------------------------------------------------------------------------
; DOS 2 Memory Mapper Jump Table (16 x 3-byte vectors)
; ------------------------------------------------------------------------------

tdos_mapper_table:
                jp      mapper_all_seg  ; +00 ALL_SEG: allocate segment
                jp      mapper_fre_seg  ; +03 FRE_SEG: free segment
                jp      mapper_rd_seg   ; +06 RD_SEG: read byte from segment
                jp      mapper_wr_seg   ; +09 WR_SEG: write byte to segment
                jp      mapper_cal_seg  ; +0C CAL_SEG: inter-segment call
                jp      mapper_calls    ; +0F CALLS: inter-segment slot call
                jp      mapper_put_ph   ; +12 PUT_PH: put segment in page A
                jp      mapper_get_ph   ; +15 GET_PH: get segment in page A
                jp      mapper_put_p0   ; +18 PUT_P0: put segment in page 0
                jp      mapper_get_p0   ; +1B GET_P0: get segment in page 0
                jp      mapper_put_p1   ; +1E PUT_P1: put segment in page 1
                jp      mapper_get_p1   ; +21 GET_P1: get segment in page 1
                jp      mapper_put_p2   ; +24 PUT_P2: put segment in page 2
                jp      mapper_get_p2   ; +27 GET_P2: get segment in page 2
                jp      mapper_put_p3   ; +2A PUT_P3: put segment in page 3
                jp      mapper_get_p3   ; +2D GET_P3: get segment in page 3

mapper_all_seg:
                ; Allocate 16K segment: returns A = allocated segment, CF=0
                xor     a
                ret

mapper_fre_seg:
                ; Free 16K segment in A
                xor     a
                ret

mapper_rd_seg:
                ; Read byte from segment A at HL into E
                push    af
                in      a, ($FE)        ; Save current Page 2 segment
                ld      c, a
                pop     af
                out     ($FE), a        ; Map segment into Page 2
                ld      e, (hl)
                ld      a, c
                out     ($FE), a        ; Restore Page 2 segment
                ret

mapper_wr_seg:
                ; Write byte E to segment A at HL
                push    af
                in      a, ($FE)        ; Save current Page 2 segment
                ld      c, a
                pop     af
                out     ($FE), a        ; Map segment into Page 2
                ld      (hl), e
                ld      a, c
                out     ($FE), a        ; Restore Page 2 segment
                ret

mapper_cal_seg:
mapper_calls:
                ret

mapper_put_ph:
                and     3
                jr      z, mapper_put_p0
                dec     a
                jr      z, mapper_put_p1
                dec     a
                jr      z, mapper_put_p2
mapper_put_p3:  ld      a, b
                out     ($FF), a
                ret

mapper_get_ph:
                and     3
                jr      z, mapper_get_p0
                dec     a
                jr      z, mapper_get_p1
                dec     a
                jr      z, mapper_get_p2
mapper_get_p3:  in      a, ($FF)
                ret

mapper_put_p0:  ld      a, b
                out     ($FC), a
                ret
mapper_get_p0:  in      a, ($FC)
                ret

mapper_put_p1:  ld      a, b
                out     ($FD), a
                ret
mapper_get_p1:  in      a, ($FD)
                ret

mapper_put_p2:  ld      a, b
                out     ($FE), a
                ret
mapper_get_p2:  in      a, ($FE)
                ret

; ------------------------------------------------------------------------------
; Boot Loader (tdos_boot)
; ------------------------------------------------------------------------------

tdos_boot:
                ; 1. Load Sector 0 of Drive 0 to $C000
                ld      hl, $C000
                ld      de, 0           ; Sector 0
                ld      bc, $01F9       ; 1 sector, 720K 2DD
                xor     a               ; Drive 0, Read (NC)
                call    tdos_dskio
                ret     c               ; Disk read error -> abort

                ; 2. Check for custom executable boot sector ($C01E has RET NC)
                ld      a, ($C01E)
                cp      $D0             ; RET NC opcode
                jr      nz, .do_launch

                ; Custom boot sector present: invoke with CF=1 per MSX protocol
                scf
                call    $C01E
                ; If boot sector returns, check if it returned failure (CF=1)
                ; and fall through to T-DOS launch

.do_launch:
                ; 3. Invoke T-DOS host boot launcher to setup Page-0 RAM and run
                ld      a, $FF
                out     ($D4), a
                ret

; ------------------------------------------------------------------------------
; Pad to full 16 KiB (0x4000 bytes from $4000 -> $8000)
; ------------------------------------------------------------------------------

                ds      $8000 - $
