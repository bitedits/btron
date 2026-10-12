/*
 * ps2_sys_vol.s -- the /SYS volume, carried inside the image's .data
 *
 * The B-System filesystem layer needs a block device before anything in it can be opened, and
 * on this target there is no disc to read: the CDVD is an IOP peripheral, so an EE-side read
 * would be a module loader and an RPC client for a second processor (doc/md/PS2.md, "Why not the
 * disc").  What this port does instead is ship the bytes.  They are the host tool's output and
 * nothing else -- `mkbtronfs` writes btron_sys.vol from src/tools/manifest.txt, the same image the
 * hosted targets mount and `make test-fs` reads -- so this file names one path and describes no
 * record, no size and no name; a second description of that content is a second thing to keep in
 * step, which is what it is here to avoid.
 *
 * Two details are load-bearing and neither is a style choice.
 *
 *   `.balign 1024` -- BTRON_BLOCK_SIZE is 1024, so an aligned base makes every offset
 *   blk_mem_create's callbacks reach (lba * block_size) aligned too, and every memcpy in the
 *   volume layer a whole number of aligned words.  Unaligned lw/sw on a MIPS is an address error,
 *   and an image whose volume happens to start on an odd paragraph would find it in the first
 *   `dir`, not in the build.
 *
 *   `.section .data.ps2_sys_vol, "aw"` -- "w" because the filesystem writes.  .data is RDRAM once
 *   the loader has done its work, so `mkf` and `vol_sync` operate on the copy in RAM: the volume is
 *   writable, and it is writable from no address, no copy and no second buffer.  "a" because it has
 *   to survive the link, and the `.data.*` spelling is what ps2.ld's `*(.data.*)` collects.  What
 *   does not survive is a reboot: the next boot re-extracts the image the disc carried.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

	.section .data.ps2_sys_vol, "aw", @progbits
	.balign 1024

	.globl	ps2_sys_vol_image
ps2_sys_vol_image:
	.incbin	"btron_sys.vol"

	.globl	ps2_sys_vol_image_end
ps2_sys_vol_image_end:
