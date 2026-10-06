I'll look up BTRON B-System, the MSX emulators openMSX and blueMSX, and the usual C/Z80 toolchain so we can map a practical developer setup.The B-System repo and both emulators are clearer now. Checking whether MSX is already a target and what the practical Z80 C/assembly path looks like.A B-System home for MSX developers should be a cross-dev kit and a tiny BTRON-shaped runtime, not a full BTRON 3.20 port. Current B-System (btron.pro, github.com/bitedits/btron) is a C99 cleanroom of BTRON3 3.20 aimed at ARM64, x86_64 UEFI, PS2 MIPS, PC-98, 68000, and POSIX. MSX is a banked Z80 (or R800) machine with a 64 KiB address space and a VDP. That can host a teaching subset — real/virtual body records, a small µITRON-like task queue, TAD text — not the workstation.

> **Status (2026-10-06): superseded as the specification.** This note is the origin, and
> `b-msx/index.html` (B-MSX, Rev 1.03) is normative. Two decisions here have been overturned by the book:
> the toolchain is **toolchainMSX** — Rasm as assembler, z88dk (`zcc`/`z80asm`/`appmake`) as compiler and
> linker, with our own `crt0_btron.asm` — not "C99 plus Pasmo only"; and the time-travel debugger listed
> below as an openMSX advantage is now a **stated goal** of B-MSX (book Part 8), built on a determinism
> law for the core. Keep this file as the survey record; do not implement from it.

==============
## Emulators

| | openMSX | blueMSX (and forks) |
|---|---|---|
| Status | Active, cross-platform | Frozen at 2.8.2 (2009), Windows. Lives on as libretro and blueMSX Live |
| Accuracy model | XML machine definitions, cycle-aware VDP | Good compatibility, easier GUI |
| Debugger | Separate Qt debugger: breakpoints, watchpoints, slot/segment-selective breaks, VDP tile/bitmap/sprite viewers, symbol manager | Built-in: disasm, CPU, stack, callstack, memory, peripheral regs, I/O |
| Time travel | Reverse and step-back | No |
| Host link | Tcl, socket/pipe control, `debugdevice` I/O log, dir-as-disk | BiFi `ld b,b` breakpoints and `ld d,d` debug strings to the trace logger; plugin SDK |
| Iteration | Script reset + dir-as-disk | Leave it running, rebuild, F12 |
| Hardware lab | Best for real machines | Machine editor is the better slot-config sandbox |

Use openMSX as the primary home. Its command socket, symbol manager, reverse, and debug device are what a B-System Makefile can drive. Keep blueMSX as the Windows quick-look: step an opcode and see the VDP change immediately, and use the machine editor when you are proving slot/mapper layouts. “blueMSX+” in practice means 2.8.2 plus the debugger/plugins (the ducasp tree is a later packaging of that code), not a maintained successor.

Developer hooks worth standardizing on:

- openMSX: start with `-ext debugdevice`, log from Z80 `out` to a reserved port, load a symbol map in the debugger’s Symbol Manager, use dir-as-disk so MSX-DOS sees the build directory.
- blueMSX: `ld b,b` / `jr $+2` as a breakpoint, `ld d,d` plus a length-prefixed string as a trace. Those two idioms should be macros in the kit so the same source builds for both.

## What to ship: C and Z80, not real C++

The MSX home is C99 plus Pasmo only. Launching it opens a BTRON window, boots an MSX2+ with the BIOS from settings, and runs the game. The same binary must also run in openMSX and blueMSX. *(Superseded: the kit is **toolchainMSX** — Rasm + z88dk + our own crt0; C99-only and one-binary still hold. See `b-msx/index.html` 7.2.)*

## Toolchain

No sjasmplus, no z88dk, no Z80 C++. Host tools and the embedded emulator are C99. The MSX program is Pasmo assembly, with optional C99 compiled to Z80 and linked as Pasmo-included blobs.

Pasmo is a fixed-address assembler. It does not emit relocatable objects, so the kit does not pretend to have a linker. Each target is one `ORG` and one Pasmo invocation.

| Target | Pasmo | Load |
|---|---|---|
| Cartridge | `ORG 4000h` plus a 16-byte ROM header, `pasmo --bin` | slot cartridge |
| MSX-DOS | `ORG 100h`, `pasmo --bin` → `.com` | dir-as-disk or a `.dsk` |
| BLOAD | `pasmo --msx` | `BLOAD "name",R` |
| Tape | raw bin wrapped as `.cas` by a C99 packer | cassette |

C99 on the Z80 side is a freestanding subset: no hosted `stdio`, no malloc in the default runtime. SDCC `-mz80 --no-std-crt0` may be used only as a code generator; the CRT, ROM header, and BIOS stubs are Pasmo. Inline BIOS calls stay in Pasmo.

```asm
        org     4000h
        db      "AB"
        dw      start
        dw      0,0,0,0,0,0

        include "btron_msx.inc"
start:
        ld      hl,title
        call    print          ; CHPUT loop
        DBG_STR "b-system msx2+"
        ret
title:  db      "BTRON",0
```

`btron_msx.inc` is the compatibility surface. `print` calls BIOS `CHPUT` (`0x00A2`) via `CALSLT` when the program is not in page 0. `DBG_STR` expands to both conventions, so one source is valid in all three emulators:

- blueMSX: `ld d,d` / `jr $+2+len` / `db string` (trace logger), and `ld b,b` / `jr $+2` as a breakpoint.
- openMSX: the same bytes are harmless Z80, and a following `out (DBGPORT),a` hits `-ext debugdevice`.
- The embedded core logs both patterns.

## First-class MSX program

An MSX program is a real-body, not a side folder. The record is fixed so C99 on the host can read it without a parser framework:

```c
typedef struct {
    char     magic[4];      /* "BMSX" */
    uint16_t kind;          /* 1=rom 2=com 3=bload 4=cas */
    uint16_t machine;       /* 1=MSX 2=MSX2 3=MSX2+ */
    uint32_t entry;
    uint32_t size;
    char     bios_key[16];  /* settings key, not a path */
    char     name[32];
} b_msx_body;
```

A virtual-body in a document or on the workbench points at that real-body. Opening it is the launch path below. Disk and cassette images are child real-bodies, not strings baked into the ROM.

## Launch

Opening the virtual-body creates one BTRON window and starts the machine. No separate emulator process and no “pick a file” step.

1. Read the real-body and settings. BIOS paths live in settings (`msx2p.rom`, `msx2pext.rom`, optional kanji, disk ROM). Missing BIOS is a window with the missing key named, not a silent hang.
2. Create the window: title from `name`, client area 512×424 for MSX2+ width, or 256×212 scaled 2×. Menu is BTRON (reset, pause, disk slot), not a copy of the openMSX or blueMSX UI.
3. Build an MSX2+ machine: Z80 at 3.58 MHz, 128 KiB main RAM, V9958, PSG, primary and secondary slots. Map the BIOS into page 0 and the sub-ROM into its slot. Insert the program as cartridge, drive A, or cassette from `kind`.
4. Reset and run. Disk BIOS or the cartridge header starts the game. The window shows the first frame after the BIOS has handed off, not a firmware menu, unless the real-body says otherwise.

Settings keys, not hardcoded paths: `bios.msx2p`, `bios.msx2pext`, `bios.disk`, `bios.kanji`, `machine` defaulting to MSX2+.

## One binary, three emulators

The embedded core is C99 and speaks the file formats the other two already run. It does not embed openMSX (GPL) or blueMSX.

| Artifact | openMSX | blueMSX | B-System window |
|---|---|---|---|
| 16K/32K ROM with `AB` header | cartridge | cartridge | primary slot |
| `.com` on a directory or DSK | dir-as-disk | disk image | disk ROM from settings, drive A |
| `--msx` BLOAD | disk or cassette | disk or cassette | same |
| `ld d,d` debug string | ignored / optional | trace logger | window log |
| `ld b,b` breakpoint | ignored | debugger break | optional break |
| `out` to debug port | `-ext debugdevice` | ignored | window log |

Ship a C99 packer that writes the `.rom`, `.com`, or `.dsk` next to a one-line openMSX reset and
a blueMSX machine note (slot of the cartridge, no CAS patch on the BIOS).
The program itself does not include emulator-specific source. Only `btron_msx.inc` does, and both expansions are legal Z80.

====================

I'll pull current facts on Rasm, z88dk, and SDCC and put them next to Pasmo and sjasmplus in one comparison.Pasmo, sjasmplus, and Rasm are assemblers. z88dk is a full Z80 kit that can call SDCC. SDCC is a C compiler with its own assembler and linker, and no MSX target. All five can emit a raw binary that openMSX, blueMSX, and the B-System MSX2+ window will run. They do not replace each other.

| | Pasmo 0.5.5 | sjasmplus 1.24.0 | Rasm | z88dk | SDCC 4.5 / 4.6 |
|---|---|---|---|---|---|
| Role | Fixed-address assembler | Assembler with modules and exports | Fast assembler, crunch, banks | C, assembler, linker, CRT, packager | C compiler, `sdasz80`, `sdldz80` |
| License | GPL | BSD-3-Clause | MIT | Clarified Artistic | GPLv2+; runtime has a linking exception |
| Written in | C++ | C++17, Lua 5.4 | C | C and asm | C |
| CPU | Z80 | Z80, R800, Z80N, i8080, LR35902 | Z80, undocumented included | 8080, Z80, R800 via the MSX target | Z80, Z180, eZ80, Z80N, GBZ80, and others |
| C | None | None | None | sccz80 (near C90) and patched SDCC | C99, plus some C11/C23 |
| Linker | No | No real linker; multi-file via `INCLUDE` / modules | No; workspaces and banks | `z80asm` links sections and libraries | Relocatable `.rel`, areas, banked calls |
| MSX out of the box | `--msx` BLOAD; `--bin` + `ORG 100h` is `.COM` | No header flag. You emit the `AB` header and `SAVEBIN` | Topic-tagged MSX. Output is raw bin, tape, snapshot; no MSX ROM flag | `zcc +msx`: `.cas`, `.wav`, `.com`, `.dsk`/`.img`, `-subtype=rom`, MSX-DOS1 and DOS2 | None. You supply `crt0` |
| Symbols | `EQU` dump | `LABELSLIST`, breakpoint lists | Symbol and breakpoint export, makefile deps | Map from the linker | `.sym`, `.map`, `.noi` |
| Structures | No | `STRUCT` | Yes, plus shared labels across workspaces | In C; asm is sections | In C; asm is areas |
| Extra | Old syntax, several 8-bit headers | 3-pass, fake ops, colon inlining, virtual `DEVICE` | On-the-fly LZ/ZX0/Exomizer, float engine, Pasmo compatibility mode | BIOS-shaped CRT, `appmake` | Optimizer, `--std-c99`, `__sdcccall(1)` since 4.2 |
| Maintenance | Docs dated March 2022 | August 2026, 512+ tests | Active; v3.3 (Sep 2025), CPC-centered | Active; MSX wiki page edited Oct 2026 | Active; 4.6 recommended by MSXgl |

## Assemblers

Pasmo is one `ORG` and one output. `pasmo --msx` writes a BLOAD header. `pasmo --bin` with `ORG 100h` is an MSX-DOS `.COM`. A cartridge is an `AB` header you write yourself plus `--bin`. Macros, `INCLUDE`, and `INCBIN` exist. There is no module namespace, no struct, no listing a debugger loads, and mnemonics are reserved words. GPL is the other cost if the assembler is vendored into B-System.

sjasmplus is the assembler you want once the source is more than one namespace. R800 is first-class, which matters for turbo R. `STRUCT` matches a real-body record. `LABELSLIST` loads in the openMSX symbol manager. `DEVICE` / `SAVEBIN` are ZX and CPC oriented, so the MSX header stays yours:

```asm
        device noslot64k
        org 4000h
        db "AB" : dw start : ds 10
start:  ret
        savebin "game.rom", 4000h, 4000h
```

Those bytes are the same cartridge Pasmo would emit. Lua is optional and not needed for the kit.

Rasm is the speed tool. It is C, MIT, and aimed at huge CPC and ZX builds: millions of instructions, crunched `INCBIN`, banks that may exceed 64K before compression, symbol and breakpoint export. A Pasmo compatibility mode means a Pasmo source can move. It does not know an MSX cartridge header or a BLOAD header. For the B-System window it is a raw-bin producer plus a cruncher, useful when a MegaROM bank is data, not useful as the MSX packager.

## C kits

z88dk is the only one of the five that already speaks MSX. `zcc +msx` builds cassette, DOS1, DOS2, and ROM images, and mixes `.c` with `.asm`. The MSX target is the classic library, not newlib. sccz80 is the simple compiler; `-compiler=sdcc -clib=sdcc_iy` uses its patched SDCC and is usually smaller. `-create-app` runs `appmake`, so the artefact is already an emulator file: `.cas`, a `.dsk` with a `.com`, or a ROM. That is the shortest path from C99-like source to a disk the openMSX dir-as-disk, blueMSX, and the BTRON window can boot. It is not freestanding C99, and the CRT owns the first bytes.

SDCC is the C compiler, not an MSX system. You write `crt0` (Konamiman’s and MSXgl’s are the known ones), place code at `0x4000` or `0x0100`, and link with `sdasz80` / `sdldz80`. `sdasz80` is old ASxxxx: relocatable, area-based, awkward macros, not Pasmo syntax. Since 4.2 the default Z80 calling convention is `__sdcccall(1)` (arguments in registers). 4.6 is the version MSXgl currently recommends; 4.2 grew code. C99 is real enough for this job (compound literals landed in 4.6; variadic functions and most of C99 already worked). Float and 32-bit math pull in a large runtime. No exceptions, no standard library that fits a 16K ROM unless you bring one.

## What the window should call

The embedded emulator should not care which of these ran. It loads the real-body: ROM with an `AB` header, `.COM` at `0x0100`, BLOAD, or a `.dsk`. openMSX and blueMSX load those same files.

- Assembly-only game: Pasmo if the program is one file and you want `--msx` today. sjasmplus if you want R800, structs, and a symbol file. Rasm if the payload is large compressed data and you will wrap the header in C99.
- C game: z88dk `+msx` is the kit that already produces the disk and ROM. SDCC is the compiler inside that kit, or the compiler you drive yourself when the CRT must be the B-System one and the only language on the Z80 side is freestanding C99.

====================

The useful sources are the ones an AI can rewrite into C99 without dragging in a framework. I'll check which MSX2+ emulators are small enough and licensed for that.Take small C cores and the chip manuals as the sources the AI rewrites. Use openMSX and blueMSX only as behavior references. Do not paste them into the B-System tree: openMSX is GPL, fMSX is non-commercial, and blueMSX mixes BSD with GPL sound chips and a GPL disassembler.

## Rewrite from these

| Unit | Source | Why |
|---|---|---|
| Z80 | [superzazu/z80](https://github.com/superzazu/z80) | One C99 core, MIT, passes zexdoc and zexall. Instruction cycles, not per-T-state. That is the right accuracy for a window. |
| Slot map | [libretro/blueMSX-libretro](https://github.com/libretro/blueMSX-libretro) `Src/Board/MSX.c`, `Src/Memory/SlotManager.c` | C89, the clearest picture of primary and secondary slots, a cartridge page, and RAM. Read the layout. The blueMSX-authored files are BSD; do not copy `R800Dasm.c` (openMSX, GPL) or `VideoChips/V9938.c` (Alex Wulms, freeware, not your license). |
| Boot ROMs | [C-BIOS](https://cbios.sourceforge.io/) | BSD, redistributable. Enough to boot a cartridge with no copyrighted BIOS. Settings still load `MSX2P.ROM` and `MSX2PEXT.ROM` when the user has them. |
| VDP behavior | Yamaha V9938 Technical Data Book, V9958 application manual, MSX Red Book chapter 2 | Register map, VRAM ports `0x98`/`0x99`, modes 0–8, sprites, palette. Write a new renderer from this, not from openMSX `VDP.cc`. |
| PSG | AY-3-8910 datasheet, or a short MIT/BSD AY core | Three tones plus noise. MSX2+ needs no FM for the first game. |
| PPI / keyboard | MSX Red Book, port `0xA8`–`0xAB` | Slot select and the keyboard matrix. A fixed matrix is enough. |

Fallback CPU if the AI fights superzazu’s generated tables: [ggambetta/libz80](https://github.com/ggambetta/libz80). Same idea, older C, still a library rather than an application.

## Do not feed these in as code

- openMSX. Accurate, C++, Tcl, GPL. Point the AI at its VDP timing notes only when a game is wrong.
- fMSX `MSX.c` / EMULib. Smallest full MSX2+ in C, and the license cannot be relicensed into B-System.
- blueMSX sound files taken from openMSX (`YM2413`, `YMF262`) and fMSX (`Fmopl.c`, `Ymdeltat.c`).
- brMSX. Assembly, useless as a C99 base.

## Native shape to generate

One C99 file pair per chip, no C++, no plugin system, no machine XML. The window owns the framebuffer.

```text
msx2p.c      reset, run N cycles, blit
z80.c        superzazu core, memory callbacks
slots.c      primary slot, 128 KiB RAM, one 32K ROM at 0x4000
vdp.c        ports 0x98/0x99, VRAM, SCREEN 0–5, 256x212 framebuffer
psg.c        AY, ports 0xA0–0xA2
ppi.c        0xA8 slot select, 0xA9 keyboard
bios.c       load settings keys bios.msx2p, bios.msx2pext; else C-BIOS
```

First machine is MSX2+, Z80 at 3.58 MHz, 128 KiB RAM, V9958 treated as a V9938 plus the MSX2+ I/O bit, one cartridge. No disk, no Kanji, no FM, no R800. Disk ROM from settings is the second step, and only if the real-body is a `.dsk`.

The AI’s acceptance test is a cartridge with an `AB` header that reaches its entry after the BIOS in settings is mapped into page 0, draws a screen, and returns a 256×212 frame to the BTRON window. The same ROM must also start in openMSX and blueMSX.

========================

The useful sources are the ones an AI can rewrite into C99 without dragging in a framework. I'll check which MSX2+ emulators are small enough and licensed for that.Take small C cores and the chip manuals as the sources the AI rewrites. Use openMSX and blueMSX only as behavior references. Do not paste them into the B-System tree: openMSX is GPL, fMSX is non-commercial, and blueMSX mixes BSD with GPL sound chips and a GPL disassembler.

## Rewrite from these

| Unit | Source | Why |
|---|---|---|
| Z80 | [superzazu/z80](https://github.com/superzazu/z80) | One C99 core, MIT, passes zexdoc and zexall. Instruction cycles, not per-T-state. That is the right accuracy for a window. |
| Slot map | [libretro/blueMSX-libretro](https://github.com/libretro/blueMSX-libretro) `Src/Board/MSX.c`, `Src/Memory/SlotManager.c` | C89, the clearest picture of primary and secondary slots, a cartridge page, and RAM. Read the layout. The blueMSX-authored files are BSD; do not copy `R800Dasm.c` (openMSX, GPL) or `VideoChips/V9938.c` (Alex Wulms, freeware, not your license). |
| Boot ROMs | [C-BIOS](https://cbios.sourceforge.io/) | BSD, redistributable. Enough to boot a cartridge with no copyrighted BIOS. Settings still load `MSX2P.ROM` and `MSX2PEXT.ROM` when the user has them. |
| VDP behavior | Yamaha V9938 Technical Data Book, V9958 application manual, MSX Red Book chapter 2 | Register map, VRAM ports `0x98`/`0x99`, modes 0–8, sprites, palette. Write a new renderer from this, not from openMSX `VDP.cc`. |
| PSG | AY-3-8910 datasheet, or a short MIT/BSD AY core | Three tones plus noise. MSX2+ needs no FM for the first game. |
| PPI / keyboard | MSX Red Book, port `0xA8`–`0xAB` | Slot select and the keyboard matrix. A fixed matrix is enough. |

Fallback CPU if the AI fights superzazu’s generated tables: [ggambetta/libz80](https://github.com/ggambetta/libz80). Same idea, older C, still a library rather than an application.

## Do not feed these in as code

- openMSX. Accurate, C++, Tcl, GPL. Point the AI at its VDP timing notes only when a game is wrong.
- fMSX `MSX.c` / EMULib. Smallest full MSX2+ in C, and the license cannot be relicensed into B-System.
- blueMSX sound files taken from openMSX (`YM2413`, `YMF262`) and fMSX (`Fmopl.c`, `Ymdeltat.c`).
- brMSX. Assembly, useless as a C99 base.

## Native shape to generate

One C99 file pair per chip, no C++, no plugin system, no machine XML. The window owns the framebuffer.

```text
msx2p.c      reset, run N cycles, blit
z80.c        superzazu core, memory callbacks
slots.c      primary slot, 128 KiB RAM, one 32K ROM at 0x4000
vdp.c        ports 0x98/0x99, VRAM, SCREEN 0–5, 256x212 framebuffer
psg.c        AY, ports 0xA0–0xA2
ppi.c        0xA8 slot select, 0xA9 keyboard
bios.c       load settings keys bios.msx2p, bios.msx2pext; else C-BIOS
```

First machine is MSX2+, Z80 at 3.58 MHz, 128 KiB RAM, V9958 treated as a V9938 plus the MSX2+ I/O bit, one cartridge. No disk, no Kanji, no FM, no R800. Disk ROM from settings is the second step, and only if the real-body is a `.dsk`.

The AI’s acceptance test is a cartridge with an `AB` header that reaches its entry after the BIOS in settings is mapped into page 0, draws a screen, and returns a 256×212 frame to the BTRON window. The same ROM must also start in openMSX and blueMSX.
