# B-System (BTRON3 3.20 HMI) for Synrc VE OS.1

A cleanroom recreation of the **B-System** (Business TRON / TRON OS Architecture for personal computers)
specification and retro desktop environment. Designed using standard C99, rendering through SDL2,
and organized into a authentic Japanese BTRON tree structure.

TRON Trace Distribution uCode: https://trace.tron.org/tk/00001C00000000000000000000070066

## Specifications B-system is based on

* [BTRON3 SPEC 3.20](https://bitedits.github.io/btron/doc/)
* [B-Free OS 1994](https://bitedits.github.io/btron/b-free/)
* [B-System 2026](https://bitedits.github.io/btron/b-system/)
* [TRON HMI](https://bitedits.github.io/btron/b-hmi/)
* [T-Kernel 2.0](https://bitedits.github.io/btron/t-kernel/)

NOTE: I can't include TRON HMI TAD book to build as it is copyright protected.
You need to ask PMC to give me non-commercial permission to Ukrainian translation (that's only language I ask, but anyway you'll have pictures).

## Components

* [x] seL4 support for Synrc VE OS.1 
* [x] ITRON
* [x] VirtIO
* [x] AArch32
* [x] POSIX
* [x] TIP
* [x] HMI
* [x] TAD
* [x] CAB
* [x] EDIT
* [x] TERM
* [x] HTML to TAD Converter
* [x] GIF
* [x] PNG
* [x] Space Casette HMI Sample
* [x] Own MOZC !!!
* [x] UTF-8 / TRON Code
* [x] Four Books Included in TAD format (BTRON3 3.20 Spec, T-Kernel, B-Free, B-System)
* [x] XMPP Chat (Namdak Tonpa 2010, 30KB)
* [ ] C99 Compiler (Fabrice Bellard?)
* [ ] IDE (Legacy IDE)
* [ ] HTML 4.1 Browser (NetSurf based)
* [ ] LibreSSL 3.1.5
* [ ] Erlang (Joe Armstrong)
* [ ] MinCaml (Eijiro Sumii)
* [ ] GStreamer
* [ ] TURN Server
* [ ] ProcessOne XMPP Server
* [ ] WebRTC RTP MCU Video Conferencing
* [ ] Be Editor (https://be.5ht.co)
* [ ] Sokhatsky Commander (https://sc.5ht.co)
* [ ] Terminal Editor (https://tv.5ht.co)
* [ ] POSIX Shell (https://sh.5ht.co)

## Japanese BTRON Lineage & Subsystem Layout

```
btron/
├── Makefile                    # Plain BSD/BeOS/TRON style Makefile
├── README.md                   # Cleanroom architecture & BTRON spec documentation
├── include/                    # Authentic Japanese BTRON Header Tree
│   └── btron/
│       ├── btron.h             # Master include header
│       ├── types.h             # Fundamental TRON types (W, H, B, UW, ID, ER, etc.)
│       ├── error.h             # TRON error codes (E_OK, E_SYS, E_NOMEM, E_PAR, etc.)
│       ├── itron.h             # μITRON Real-Time Kernel Primitives
│       ├── dp.h                # Display Primitives (Graphics engine)
│       ├── wnd.h               # Sakamura Window Manager APIs
│       ├── event.h             # BTRON System Event Queue
│       ├── vobj.h              # Real Body / Virtual Body Hyper-Data Model Engine
│       ├── troncode.h          # TRON Multilingual Character Code & Font Engine
│       └── desktop.h           # Desktop Shell & Panel Manager APIs
├── src/                        # Core Subsystems Implementation
│   ├── kernel/                 # μITRON Task & Synchronization Subsystem
│   ├── graphics/               # Display Primitives (DP) Vector & Raster Graphics
│   ├── window/                 # Sakamura BTRON Window Manager & Event Dispatcher
│   ├── vobject/                # Real Body / Virtual Body Hyper-Data Engine
│   ├── font/                   # TRON Character Code & Pixel Font Renderer
│   └── desktop/                # Cho-Kanji / BTRON Desktop Compositor & Main Launcher
└── apps/                       # Authentic BTRON Accessories
    ├── vobj_manager.c          # Real Body Cabinet & Virtual Body Explorer Window
    ├── t_editor.c              # TRON Text Editor (Editor) Window
    └── gterm.c                 # BTRON Terminal Shell (GTerm) Window
```

## Raspberry Pi 2/3 T-Kernel 2.0 `Yokobayashi`

```
$ make kernel
$ make run-kernel
```

<img src="https://bitedits.github.io/btron/b-system/img/yokobayashi.png" width="800" alt="Yokobayashi T-Kernel" />

## QEMU VirtIO T-Kernel 2.0 `Sakamura`

```
$ make sakamura
$ ./btron-sakamura.elf
```

<img src="https://bitedits.github.io/btron/b-system/img/sakamura.png" width="800" alt="Sakamura T-Kernel" />

## QEMU VirtIO `Tokita`

```
$ make qemu
$ make run-qemu
```

<img src="https://bitedits.github.io/btron/b-system/img/virtio.png" width="800" alt="QEMU VirtIO Synrc" />

## POSIX `Takada`

```
$ make posix
$ make run-posix
```

<img src="https://bitedits.github.io/btron/b-system/img/posix.png" width="800" alt="POSIX Light" />


## Features

1. **True Japanese BTRON API Header Hierarchy**:
   - Includes `<btron/btron.h>`, `<btron/types.h>`, `<btron/itron.h>`, `<btron/dp.h>`, `<btron/vobj.h>`, `<btron/wnd.h>`, `<btron/event.h>`, and `<btron/troncode.h>`.
   - Native TRON type definitions: `W`, `H`, `B`, `UW`, `UH`, `UB`, `VW`, `ID`, `ER`, `RECT`, `PNT`, `PAT`, `COLOR`.
   - Real-time kernel primitives: `cre_tsk`, `sta_tsk`, `slp_tsk`, `wup_tsk`, `cre_sem`, `wai_sem`, `sig_sem`.

2. **Real Body / Virtual Body Hyper-Data Model Engine (`vobj`)**:
   - Implements Sakamura's legendary hyper-data object engine where documents contain live embedded pointers (Virtual Bodys) to Real Bodys stored in disk cabinets.

3. **Sakamura Cho-Kanji Retro Desktop & Window Manager**:
   - Classic Sakamura teal desktop workspace with top status panel, clock, desktop cabinet launcher, and window z-ordering.
   - Double-bordered retro window frames with titlebars, close buttons, window dragging, and focus.

4. **BTRON Accessories**:
   - **`gterm`**: BTRON Terminal Console window.
   - **`t_editor`**: TRON Text Editor with Virtual Body icon embedding.
   - **`vobj_manager`**: Real Body Cabinet & Virtual Body Link Explorer.

## Building & Running

In the spirit of NetBSD, BeOS, and TRON, compilation requires only a plain **BSD Makefile**:

```bash
$ make sakamura
$ ./btron-sakamura.elf
B-System/BTRON3 3.20 (sakamura-tkernel-virtio) Ken Sakamura — T-Kernel 2.0
Copyright 2026 Synrc Research Center. MIT License.
[BOOT] Host: Darwin 25.5.0 arm64
[CORE] Sakamura T-Kernel 2.0 Engine  BTRON_SAKAMURA
[T-KERNEL] All Sakamura T-Kernel 2.0 subsystems initialized.
[VIRTIO] MMIO Controller registered at address 0x10001000
[MEM ] T-Kernel hosted: memory managed by host OS allocator
[HFDS] POSIX file I/O: host filesystem passthrough  [OK]
[HFDS] HFDS Hierarchical File/Data Set: INIT  [OK]
[TRACE-COCOA] Activated nswin=0x9b15b0280 contentView=0x9b17a8500
[B-System] Launching Sakamura B-System Desktop & Accessories...
```

# Credits

* Namdak Tonpa and Grok 4.5
