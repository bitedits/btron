# T-BIOS & T-DOS Clean-Room Provenance & Compliance

## 1. Clean-Room Architecture Principles

T-BIOS (B-MSX System BIOS) and T-DOS (B-MSX Disk Operating System) are developed under strict clean-room engineering specifications:

1. **Zero Proprietary Code Contamination:**
   - No copyrighted source code or binary disassembly from Microsoft MSX-BIOS, MSX-BASIC, MSX-DOS (1.x or 2.x), or ASCII Corporation system ROMs is included, consulted, or replicated.
   - Nextor source code or proprietary MSX-DOS extensions are explicitly excluded from T-DOS.

2. **Permissible References:**
   - Publicly available MSX architecture standards and technical reference manuals:
     - MSX Technical Data Book & MSX Datapack (ASCII Corporation, 1983–1988)
     - MSX-DOS 2 Environment Specification: `https://map.grauw.nl/resources/dos2_environment.php`
     - MSX-DOS 2 Function Calls Reference: `https://map.grauw.nl/resources/dos2_functioncalls.php`
     - MSX BIOS Jump Table Reference: `https://map.grauw.nl/resources/msxbios.php`
     - FAT12 / FAT16 File System Specifications (ECMA-107, ISO/IEC 9293)
   - C-BIOS open-source architecture (3-clause BSD license) consulted solely for architectural memory mapping conventions and public system variable addresses ($F380–$FFFF).

3. **In-Tree Toolchain:**
   - SDCC (Small Device C Compiler) 4.6.0 (`sdcc`, `sdasz80`)
   - Pasmo 0.5.5 in-tree (`third_party/pasmo/pasmo`)
   - In-tree Intel HEX to ROM converter (`src/emulators/msx/tbios/sdcc/ihx2rom.c`)
   - Zero git submodules; fully self-contained in `./btron`.

4. **Code Quality & Verification:**
   - Strict adherence to C99 and BTRON microkernel coding conventions.
   - Zero heap allocations post-boot.
   - 100% pass requirement on the 18+ title MSX corpus in `./assets/msx`.
   - ≥ 80% coverage of well-known MSX-DOS 1/2 application and game interfaces.

## 2. Hardware Peripheral Extension Provenance (Phase 8)

The hardware peripheral models integrated in B-MSX are clean-room specifications derived solely from public hardware reference standards:

1. **Hitachi HD64180 Workstation Microprocessor:**
   - Reference: Hitachi HD64180 Hardware Manual (Hitachi Semiconductor, 1985–1989).
   - On-chip Memory Management Unit (MMU): 20-bit dynamic physical address translation via Common Base Register (`CBR`, 38H), Bank Base Register (`BBR`, 39H), and Common/Bank Area Register (`CBAR`, 3AH).
   - Direct Memory Access Controller (DMAC): Channels 0 and 1 memory-to-memory and memory-to-I/O block transfers (`SAR0`, `DAR0`, `BCR0`, `MAR1`, `IAR1`, `BCR1`, `DSTAT`, `DMODE`, `DCNTL`).
   - Asynchronous Serial Communication Interface (ASCI): Full-duplex channels 0 and 1 with status flags (`TDRE`, `RDRF`, `OVRN`) and 16-byte FIFO buffers (`CNTLA`, `CNTLB`, `STAT`, `TDR`, `RDR`).
   - Clocked Synchronous Serial I/O (CSIO): Synchronous half-duplex transfer (`CNTR`, `TRDR`).
   - Programmable Reload Timers (PRT): Timer channels 0 and 1 down-counting at phi/20 with interrupt triggers.

2. **MSX-MIDI & turbo R MIDI Interface:**
   - Reference: MSX Datapack (ASCII Corporation) and Panasonic FS-A1GT Hardware Reference.
   - Ports 0xE8–0xEF: Intel 8251 UART data/status/control registers (`0xE8`/`0xE9`), 8253 Timer counter channels 0..2 and control word (`0xEC..0xEF`), and Timer Counter #2 Interrupt Release / Clear (`0xEA`/`0xEB`).


