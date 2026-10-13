# PS2 GS backend — development record

This file is the **quarantined history** of [`PS2.md`](PS2.md)'s EE/GS backend plan of
record.  Nothing here is normative: a law lives in `PS2.md`, and this file keeps the
evidence that earned it -- the control runs and their failure counts, the measurements
that were later withdrawn and why, the hand-derived literals, and the dated landing
notes.  Read it when a claim in `PS2.md` needs its provenance, and never to learn what
the design is.

Moved out of `PS2.md` on 2026-10-12, when that document was compactified to its
normative body; the text below is verbatim, including the numbers it later corrected.
Every section is linked from the row or law it belongs to.

---

## Increments 1-8, as landed (the table verbatim)

<a name="increment-records"></a>

| # | increment | the gate that says it works |
|:--|:---|:---|
| 1 | *landed 2026-10-12*: the setting and the runtime selection; `GL_BACKEND_EEGS` installs the build's default ops as a placeholder and says so on the console, so nothing can regress | `make -k test` green with five new assertions in `verify/tests/test_xmb_render.c` (default matches the define, un-chosen selection reads the default, `select(EEGS)` is remembered, `gl_init(EEGS)` lands on the linked rasterizer, the round trip back); shown to have teeth -- with `gl_backend_selected()` forced to return the default the third one reports `[FAIL]`; both builds of the suite still 75/75; `make test-ps2` 54/54; `scripts/ps2_bench.sh` on the same image gives `[GL] TinyGL ZBuffer init`, `swap 0`, `render 532 us/pass` |
| 2 | *landed 2026-10-12*: the EE-side packet builder -- `src/drivers/ps2/ps2_gs_pack.{h,c}`, nineteen encoders (`GIF_TAG`, `PRIM`, `RGBAQ`, `ST`/`UV`, `XYZ2`/`XYZF2` with the 12.4 `+2048` page offset, `TEX0`, `TEX1`, `CLAMP`, `XYOFFSET`, `SCISSOR`, `ALPHA`, `TEST`, `FRAME`, `ZBUF`, the REGS nibble packer, the AD-pair writer) and nothing else: no DMA, no register write, no caller | `verify/tests/test_ps2_gs_pack.c` #includes the builder and compares 35 words against literals hand-derived from the manual's field table; `make test-ps2-gs-pack` reports `checks=35 failures=0`, and it has teeth -- shifting `SCISSOR`'s `y1` from bit 48 to 47 produces exactly one `[FAIL] scissor 800x600  got=0x012c000003200000` and exit 1, then the clean source restores 35/0.  Two of the hand literals were wrong on the first run (a `UV` field past 14 bits, and `GIF_TAG`'s 2-bit `FLG`) and the encoders were right both times, which is the evidence the expectations are not computed by the code they check.  `make ps2` compiles the builder under the real `-msoft-float` flags, `make test-ps2` stays 54/54 (test 39 still finds COP1 only in `ps2_builtins.ps2.o`), and `make -k test` is green with the new suite in the aggregate |
| 3 | *landed 2026-10-12*: the drawing environment as one AD-pair stream -- `src/drivers/ps2/ps2_gs_vram.{h,c}` (the two page units below, a bump cursor, the canvas reservation) and `src/drivers/ps2/ps2_gs_env.{h,c}` (GIFTAG then `XYOFFSET`, `SCISSOR`, `FRAME`, `ZBUF`, `TEST`, `ALPHA`, `CLAMP`, `TEX0`, `TEX1`: nine writes, ten quadwords), with `ps2_gs_draw_env_apply()` in `ps2_gs.c` as the only part that touches the bus.  `ps2_dma_gif_send()` became the shared `ps2_gs_gif_send()` -- declared in `ps2_gs.h`, since increment 4 is the second producer on that pipe and a copy of the STR poll is what sharing exists to prevent.  Nothing calls the apply path yet, which is the increment's point: it programs registers the blit path never has, and the screen is unchanged | `verify/tests/test_ps2_gs_env.c` compares the built packet word-for-word and the allocator against hand arithmetic: `checks=59 failures=0`.  It has teeth, three controls each failing the one law it edited: swapping `TEST` and `ALPHA` in the stream -> `[FAIL] test reg`; dropping the 12.4 scaling on the page origin -> `[FAIL] xyoffset value got=0x000081400000800c want=0x00008140000080c0`; taking `FRAME.FBA` in the 256-byte unit -> 25 failures starting `[FAIL] fba atlas`, because the builder refuses a frame plan whose offset is not a whole 8 KB page.  `make ps2` compiles both units under the real `-msoft-float` flags and `make test-ps2` is 57/57 with three new rows read off the image: 39 the apply path calls the builder once and pokes no GS register, 40 the same path writes D2_CHCR, D2_MADR, D2_QWC and D_STAT, 41 the builder touches no memory-mapped register at all, which is what makes the host suite the right instrument.  Controls D and E on the image gate: replacing the send with `GS_REG_POKE(0x18, ...)` makes 39 evaluate `[ 1 -eq 1 ] && [ 1 -eq 0 ]` and drives 40's four counters to zero; one `*(volatile uint32_t *)0x1000a000 = 1` inside the builder makes 41's counter 1.  `make -k test` exits 0 with the suite in the aggregate |
| 4 | *landed 2026-10-12*: recorder: `src/drivers/ps2/ps2_gs_rec.{h,c}` -- a fixed-capacity GIF stream that takes a primitive's vertices and emits packed groups whose REGS list is `PRIM, RGBAQ, ST, XYZ2`, the vertex coordinates folded to 12.4 around the `+2048` page origin by the increment-2 encoder, and nothing else: it holds no matrix and no projection, because the modelview and lighting arithmetic belongs to the downstack row 5 hangs on rather than to the packet layer.  A full buffer is sent and a new group started rather than a vertex dropped -- the one exception is an `op` this recorder does not know, where the vertex is refused and `gs_rec_unsplittable()` counts it -- so capacity is a latency knob and the counter that matters is DMA starts per frame, see the capacity note below the table | host suite against hand-built words, the increment 2 and 3 discipline: a four-vertex quad in full (17 quadwords: one tag plus the four registers of each vertex, every field from the field table), a two-vertex strip that latches a `glColor` set after the first vertex, a primitive change closing its group, and the boundary rule at all seven shapes it knows.  `verify/tests/test_ps2_gs_rec.c` #includes the recorder and the packer with `GS_REC_QW_CAP` overridden to 64 quadwords by the test's own `#define` -- the shipped 4,096 makes the split paths unreachable on a host that would rather write ten thousand vertices -- and `make test-ps2-gs-rec` gives `checks=139 failures=0` with no warnings -- 139 because the suite also gates the quadword unit itself and the mid-stream state write increment 5b hangs on.  It has teeth, five controls one edit each: dropping the 12.4 scaling gives exactly 3 failures, `[FAIL] vertex 2 XYZ2: 10 px is 0xA0 sixteenths, not 10  got=0x000000008014800a want=0x00000000814080a0`, and the two (0,0) vertices stay silent because the page offset is the same either way; swapping `ST`'s halves gives exactly 1, `[FAIL] vertex 1 ST got=0x3f80000040000000 want=0x400000003f800000`; filling `NLOOP` with the quadword count instead of the vertex count gives 15; and dropping the AD group's register offset out of its upper half gives 3, which is the control that says the state write increment 5b needs is gated and not merely present.  That increment's own group -- `test_ad_group()` -- checks the tag against the literal `0x1000000000008002` (NLOOP 2, EOP, NREG 1, `PRE` clear: the same shape `send_trx_setup()` sends and the running canvas proves), the two register values in the low halves, their GP offsets in the upper halves, and the refusal of a write that would not fit rather than a truncated one.  That group's *size* was the defect found after it shipped: `gs_rec_ad()` handed its sink `1 + 2n` quadwords for `n` writes, counting every register a pair of quadwords where the AD form is one, so a real transfer would have been given a `QWC` larger than the packet and the GIF would have decoded the overflow as more of the same stream.  The count now has one name, `GS_REC_AD_QWC(n)` in `ps2_gs_rec.h` -- the general `1 + NLOOP x NREG` law with NREG=1, stated where the register is written instead of re-derived at each of the places that need it -- and the machine's proof of the shape is `send_trx_setup_at()`, which sends a tag and four AD registers as five quadwords with NLOOP=4.  Restoring the wrong count costs exactly 1 failure in this suite and 1 in the raster suite, each naming the group's quadword total.  The fifth control is not hypothetical but the defect this revision exists to fix: narrowing `s_qw` from pairs of `uint64_t` back to one word per register -- the half-width layout the recorder shipped with before this pass -- fails **29 of the 139 checks**, and the identical layout in the raster suite fails **18 of its then-49 checks** (measured before 5b grew that suite to 78; the unit both read through is shared, so the count is the one that revision had).  Both suites now read the captured stream through one shared statement of the law, `verify/tests/gs_group_view.h` (a group is `1 + NLOOP x NREG` quadwords, a quadword is two `uint64_t`, a register's value sits in its quadword's low half), because the reason each of them stayed green through the bug is that each indexed the words the recorder wrote by its own private convention -- two parallel models of the same unit, which is exactly the failure this port's single-source law is about.  Two bugs were caught by the suite rather than by reading, which is the evidence the expectations are not computed by the code they check: `open_group()` built its tag pair with `gs_pack_pair()`, the AD-form writer, so the REGS quadword arrived as its own low byte -- `[FAIL] REGS got=0x0000000000000010 want=0x0000000000005210`, a group the GIF would read as `PRIM, RGBAQ, PRIM, PRIM` and feed four data quadwords to the wrong four registers -- and `reemit_continuation()` took its second vertex from the sliding window that `write_vertex()` had just overwritten, so every continued strip and fan wrote its first re-emitted vertex twice: a well-formed packet, a degenerate first triangle, and a strip continuing from an edge it never had.  Three of *the test's* literals were wrong on the first pass too (the fan's tag, whose `PRIM` 0x25D sets the tag's bit 47 where 0x25C does not, and the POINT/LINE start counts, which are 12+12+6 in three starts and 12+2 in two, not the 12+18 guessed), corrected from the field table and the boundary arithmetic before the code was consulted.  `make ps2` compiles the recorder under the real `-msoft-float` flags and `make test-ps2` was 61/61 at that revision and 67/67 through 5b, and is 72/72 since 5c, its two increment-4 rows read off the image at the same numbers: 42 the recorder is linked (10 `gs_rec_*` functions, 920 lines of disassembly), 43 the recorder is pure -- no `ps2_gs_poke64`, no `ps2_gs_gif_send`, no memory-mapped address in any of its instructions, which is what makes the host suite the right instrument for its words.  Nothing calls it yet, so `scripts/ps2_bench.sh` on that same image is the byte-for-byte image before it: `[GL] TinyGL ZBuffer init`, the XMB opening and animating, `run complete`.  `make -k test` exits 0 with the suite in the aggregate -- and the wiring was itself a trap worth recording: `test-ps2-gs-rec` was added to `.PHONY` and to `test:` before its rule existed, where `make` printed "Nothing to be done" and still exited 0, a suite that passes by not running |
| 5 | presentation, in three steps, and *not* as a second front end.  The step this section used to describe -- "`backend_eegs.c` implements the `gl_ops_t` table on the recorder" -- is the wrong shape, and the tree says so: `gl_ops_t` has 38 members, and everything in them that a GS backend would have to reimplement (the matrix stack, the viewport map, lighting, clipping, texture state, display lists) already exists and is already what the XMB's 20 calls and glgears' 15 exercise.  A parallel implementation would be a second model of the same laws, and the two would drift exactly where drift is invisible -- one binade of a projection, one rounding of a clip.  The seam is instead the one TinyGL already has: `GLContext.draw_triangle_front/back`, assigned per primitive mode in `src/gl/tinygl/vertex.c:101-123` and called from `src/gl/tinygl/clip.c:271-284` *after* clipping, handing three device-space `GLVertex` records.  So **5a** *landed 2026-10-12*, and it replaced only the span loops.  It is not a `gl_draw_triangle_gs()` hung on those hooks -- the hooks already point at three named functions, so the seam is one condition in each of them (`gl_draw_triangle_fill` at `clip.c:611`, `_line` at :721, `_point` at :747; in the fill it sits after the bound plane has been resolved and the fill accounted for, `clip.c:638`) that calls `eegs_triangle`/`eegs_line`/`eegs_point` of the new `src/gl/backend_eegs.{h,c}` and returns.  What it hands over is what the software fill was about to use: `zp.x`/`zp.y` unchanged, `zp.r/g/b/a` each as its top byte, `zp.s`/`zp.t` divided by the upload's own fixed-point scale, and the three PRIM flags read off the same three conditions the software dispatch branches on -- `current_shade_model` for IIP, "a plane exists" for TME, `zb->enable_blend` for ABE -- so no state is modelled twice.  A flat-shaded primitive is recorded with the *third* vertex's colour on all three of its vertices, because that is the colour `ZB_fillTriangleFlat` draws (`ztriangle.c:64-68`) and because a flat GS group's choice between first and last vertex is not something this port has an instrument for -- writing the same colour on all three makes the question unable to bite.  The line and point equivalents exist because the XMB's message-box border is a `GL_LINE_LOOP` (`xmb.c:1786`), and they are recorded untextured whatever the texture state is, because `ZB_line` and `ZB_plot` sample no texture.  **5b** *landed 2026-10-12*: it puts the two textures the XMB binds into GS VRAM and points `TEX0` at them: the four transfer encoders (`BITBLTBUF`/`TRXPOS`/`TRXREG`/`TRXDIR`, offsets `0x50..0x53` already named in `ps2_gs_pack.h`) with the blit path's own hand-written shifts moved onto them, the icon atlas (a 384x384 upload into a 512x512 plane, 1,048,576 B, `xmb.c:1196`) and the font sheet (128x96 into 128x128, 65,536 B, `xmb.c:1202`) uploaded as IMAGE-mode (`FLG=2`) groups into the pages increment 3's allocator reserves, and `TEX0`/`TEX1`/`CLAMP` following `glBindTexture` -- which means mid-stream, as a recorder AD group, see the paragraph below the table.  **5c** *landed 2026-10-12* and is the present: the environment packet and then the recorded stream after `ps2_gs_upload()`, clipped to the GL window's rect, with `redraw_all_windows_clip()` ceasing to blit that window's surface into the canvas -- under `BTRON_PS2_TARGET` only.  One fact of the seam governs all three: `g_gl` is a single process-wide global (`gl_dispatch.c:21`) and TinyGL's ZBuffer is one static pair, so the image has one GL target, "last surface created wins", and no per-window backend choice exists to fall back on | **5a** is *landed*, host-gated in the increment 2/3/4 discipline and before anything on screen depends on it: `verify/tests/test_eegs_raster.c` #includes the raster stage, the recorder and the packer, and compares 49 checks against words written from the field tables (**78 today**; the growth is 5b's plane rule, in its gate below) -- the vertex at the measured far corner of the 768x500 client (`767,499` -> `0x000000009F30AFF0`), a `20.09375` px vertex whose 12.4 value is 321 and not 322, `RGBAQ`'s byte order, the hand tag a three-vertex group of that `PRIM` carries (`0x412DC00000008003`), three same-state triangles in one 37-quadword send (`1 + 9 vertices x 4 registers`), the state change that closes, and a twelve-triangle run in which every group's `NLOOP` is a whole number of triangles.  `make test-eegs-raster` gave `checks=49 failures=0` at that revision (78 checks today, still no failures and no warnings) and it has teeth, seven controls one edit each, all re-measured on the 49-check suite: making `select_prim()` re-send unconditionally costs 8, starting `[FAIL] three same-state triangles are one send`; ignoring IIP costs 6, `[FAIL] ... PRIM 0x25B got=0x0000000000000253 want=0x000000000000025b`; scaling x by a half costs exactly the two pass-through checks, `got=0x000000009f3097f8 want=0x000000009f30aff0`; unpacking the packed colour high byte first costs exactly 1, `got=0x3f800000442233ff want=0x3f800000ff332244`; dropping the 12.4 scaling costs 2; filling `NLOOP` with the quadword count costs 9; and dropping the frame's `gs_rec_init()` costs **12 failures out of the 15 checks** the tests' own empty-capture guards still let run, the whole suite -- which is the control that says the suite is one instrument and not six independent ones, because a recorder that never met its frame keeps no sink, so it is a stream nobody reads and there is nothing left to compare.  The eighth is the unit defect of the revision below it: storing one 64-bit word per register instead of a full quadword costs **18 of these 49 checks** and 29 of the recorder's 139, and the reason it cost so little before is that this suite, like the other, indexed the words by its own convention rather than the GIF's; both now share `verify/tests/gs_group_view.h`.  The y-direction law this row called the one a hand literal and nothing else can pin turned out to need no pinning, which is the increment's most useful result: the coordinates are *passed through*, so there is no second viewport map here to be upside down, and those two checks measure the encoder's 12.4 fold and the absence of a re-projection rather than a projection of GS shape.  `make ps2` compiles the stage and the adapter under the real `-msoft-float` flags, `make test-ps2` was 67/67 at that revision, with the dial row (45 then, 51 now) still `GLDEF_RET=0`, `make -k test` exits 0 with the suite in the aggregate, and `scripts/ps2_bench.sh` on that image is the byte-for-byte image before it -- `[GL] TinyGL ZBuffer init`, `total=56851 us render 56033/56057`, the XMB's three frames at 31/321/609 ms folded, `run complete` -- because nothing reaches this path until 5c installs the backend, exactly as the dial rows hold (44/45 then, 50/51 now).  One thing found outside the increment's own files: `test-ps2-fb-order` compiles `clip.c` on the *host* with `-DBTRON_PS2_TARGET`, so the guarded reference made `backend_eegs.c`, `ps2_gs_rec.c` and `ps2_gs_pack.c` link inputs of that suite, and the Makefile comment's claim that nothing else in the tree reads `BTRON_PS2_TARGET` was the stale part -- and both of its binaries still normalise to the same 31,488 words, which is the first direct evidence that linking the GS stage changes nothing the canvas holds.  What that suite gates is the *stage* (`backend_eegs.c`); the *adapter* in `clip.c` that fills its struct has no host assertion and cannot have one yet, because `gl_active_backend()` cannot answer EE/GS on any build and the recorder's sink is installed only by 5c's present -- so the colour shift, the texel scale and the flat-shade source below are gated today by the bench's unchanged canvas and will be gated by the screen at 5c, which is why that step's gate says the user's eyes twice.  **5b** is *landed*, and its gate has three layers because the step spans three kinds of thing: words on a host, calls in an image, and bytes on a bus.  **Words**: `verify/tests/test_ps2_gs_tex.c` #includes the registry with the packer and the allocator and compares **103 checks** against hand literals -- the icon atlas (384x384 into 512x512) planned at byte 1,998,848, `TBA` 7,808, `TEX0 0x664021E80`, `CLAMP 0x7FC007FC005`; the font sheet (128x96 into 128x128) at 3,047,424, 11,904, `0x5DC00AE80`, `0x1FC001FC005`; `TEX1 0x20`; 1,081,344 B left in the cursor's budget; the 16 KiB plane unit at the one size where it bites (a 64x32 CT32 picture is 8,192 B, a whole 8 KiB page and half a plane, and the span is still 16,384); the sink seeing a *complete* plan during the registration call; a recycled pointer re-planned into a fresh span at 2,064,384 instead of inheriting the dead plane's; the 8-slot cap; an exhaustion that refuses whole and marks the recycled slot dead rather than leaving it keyed on a page another texture now owns; and all seven refusals (null plane, zero extent, non-power-of-two pad on either axis, an upload wider or taller than its plane).  Teeth, five controls one edit each, counted as failures of that suite: rounding a plane to 8 KiB instead of 16 costs **1** -- the two shipped planes are both exact multiples, so only the small-plane check can see the unit; `CLAMP` over the upload instead of the plane costs **2** (both sheets); `qwc` from the upload costs **6**, stream-length rows included; handing the sink over before the plan is complete costs **3**, where `byte`, `qwc` and `pending` all read back zero, which is the exact shape of the law (the bus owner must never see a half plan); and letting the recycled-pointer path take the re-upload branch costs **13**, the whole fresh-span law plus the refuse-after-recycling rows.  The transfer words are gated where they were already proven: `make test-ps2-gs-pack` (55 checks) asserts `gs_pack_bitbltbuf/trxpos/trxreg/trxdir()` rebuild the four literals the blit path used to write by hand, so the running canvas is the instrument and not a second copy of its arithmetic.  **Calls**: `make test-ps2` was **67/67** at that revision (72/72 since 5c), rows 44-49 being this increment's -- 44 the registry is linked (9 `gs_tex_*` functions), 45 it is pure (no register write, no DMAC address, no transfer call anywhere in it, which is what makes the host suite the right instrument for its words), 46 `ps2_gs_init()` installs the driver as the sink, 47 it starts the VRAM cursor, 48 a GL upload reaches `gs_tex_register` (3 call sites: `gl_image_store()` is static and inlines into `glopTexImage1D`/`glopTexImage2D`/`glopCopyTexImage2D`, so the count is taken over the image rather than inside one symbol), 49 the textured fill calls `gs_tex_find` and `eegs_texture` from `gl_draw_triangle_fill`.  Four of those rows were measured by deleting the thing they name and rebuilding: without the publish, 48 reports `(0 call sites)` and fails while 44-47 hold -- the TU survives on the driver's own calls, so 48 is the only detector of that seam; without the lookup, only 49 fails; without `gs_tex_init()`, only 47 -- and 47 is the row whose absence is catastrophe rather than silence, because a cursor left at zero is page 0, the canvas the PCRTC reads, so the first uploaded icon would overwrite the screen it is drawn on.  **Bytes**: none of the above can say that a plane's pixels ever reached VRAM, so the bench now says it -- `ps2_bench_gs_planes()` prints, after the frame-3 surface row, `gs planes: registered=2 pending=0 vram_left=1081344` and per plane `upload 384x384 into 512x512 at byte 1998848 TBA 7808 qwc 65536` and `upload 128x96 into 128x128 at byte 3047424 TBA 11904 qwc 4096`.  Every number in those three rows is the host suite's hand literal, produced by the shipped image on PCSX2, and `pending=0` is the sink having taken both planes' bytes (1,048,576 + 65,536 = 1,114,112 B) across DMAC channel 2.  A harness trap worth recording: that phase is behind `-DBTRON_PS2_BENCH_APP`, which is a target-specific flag on `core_ps2.pbench.o`, so `BENCH_APP=1` alone does not rebuild it -- `touch src/cores/core_ps2.c` or the ELF keeps the previous phase list and the XMB never opens.  What 5b did *not* change is the screen: `xmb surface frame 3 : 768x500 black=0 other=384000 first_colour=0xff014c9d box=0,0..767,499`, frames at 31/322/610 ms folded, `full 800x600 total=56888 us render 56068/56077` -- the same rows as the image before it; the bench prints no canvas hash, so the strict unchanged-pixels claim stays `test-ps2-fb-order`'s (both domains still normalise to the same 31,488 words), and the reason nothing moved is that `eegs_frame_begin()` is still called only by the host suite, so no recorded stream is emitted and the canvas blit is still the only present path.  The raster suite grew to **78 checks** with this step because the stage now has a plane rule: `eegs_texture()` with a plane keeps `TME` and emits the `TEX0`/`TEX1`/`CLAMP` AD group, `eegs_texture(0)` clears `TME` and counts `eegs_tex_missing()`, and every textured case now names a plane and reads its capture one group later (`start_plane()`, `VG()/VN()`).  Three rows gate the rule itself: the switch group leaves *before* `eegs_frame_end()`, a fresh frame inherits no plane (PRIM `0x24B`, missing=1), and re-naming a plane pays a second switch group.  The AD group's quadword count is the defect found in this pass and is written up in row 4.  5c is gated by `scripts/ps2_smooth.sh`: the XMB's `draw=` column, **74.047 ms against the 176.5 ms the CPU rasteriser measures on the same sources** -- **and by the user's eyes**, because it is the first increment that can change what a window looks like, and because "the GS drew it" and "the GS drew it upside down, or one page over, or with the canvas still blitted underneath it" are indistinguishable from a console.  The image gate is **72/72** (`make test-ps2 PS2_GL=eegs`), and the dial pair that used to hold this increment out has been retired in place: 50 now reads the pipeline install (`gl_init` calls `tinygl_backend_init` twice, once per arm), 51 reads that no arm resolves a name to a placeholder or to the dial, and 52 became a bound rather than a prohibition -- `GLDEF_RET <= 3`, because 3 is what `PS2_GL=eegs` means and the two rows above are what make the name mean something.  Restoring the old placeholder arm in `gl_init()` fails 50 (`1 times`), and `(GL_BACKEND)7` as the default fails 52 with 50 and 51 holding; both were measured.  The four present rows (53-56), the net five they add to the gate, and the controls that prove each of them are written up in 5c's results below the table.  The two counters this increment had to read out are read: `gs_tex_pending()` is `pending=0` on the bench's own row (both planes' bytes crossed), and `eegs_tex_missing()` is `texmiss=0` in `[GSTS]` (no uploaded sheet drawing flat) -- and a third the plan did not anticipate, `drop=42`, turned out to be the one that mattered, and is closed there too |
| 6 | the pointer and window decorations as GS sprites over the GS-rendered layer, because they are drawn *after* the canvas today and would otherwise be buried | the user's eyes on the screen -- this is the increment whose correctness cannot be established headlessly |
| 7 | *landed 2026-10-12*: the geometry kernel on COP1, because with the pixels on the GS the float calls *are* the app frame: `gl_vertex_transform()` (`vertex.c:157`) and `gl_transform_to_viewport_vertex_c()` (`:135`) move verbatim out of `src/gl/tinygl/vertex.c` into one new TU, `src/gl/tinygl/vertex_stage.c`, exported as `tgl_vertex_transform(GLVertex*)` and `tgl_vertex_to_viewport(GLVertex*)` -- a pointer in, nothing out, so no float crosses the boundary and every other unit keeps `-msoft-float` -- and that TU takes the substitution `ps2_builtins.ps2.o` already takes (the `PS2_FP` block above the `%.pbench.o` rules, keyed on `PS2_FP`), registered through `TINYGL_SRCS` so the one insertion reaches `GL_SRCS`, the PS2 image, `TEST_FBORDER_SRCS` and `TEST_XMB_TGL_SRCS`.  It is a subtraction and not a rewrite: 460,861 libcalls in the counted frame 2 -- 373,820 of them add/mul/div/compare at the measured 28 cycles, and 15 of those cycles are the `jal` and its prologue -- against a present path that no longer rasterises.  Two facts of this CPU bound what may move, both measured before this row was written and both in the note below it: `(GLint)` compiles to `trunc.w.s`, which the EE does not have and whose silence leaves the destination register holding the previous answer, and the lighting branch's `gl_V3_Norm_Fast()` is `sqrt(double)`, so `.d` would enter the image at 21 instructions | `draw=` and the census's `S=` column, which is one instrument and its own attribution: a counted frame's `S=` falls by exactly the calls the kernel stopped making, so the same run gives the win and the share of the frame it came from (premise, both from this image: counted frame 2 `draw=84312 us S=460861`, uncounted `draw=74489 us`).  The gate's two FPU rows change meaning rather than number -- the COP1-ownership row (57) becomes a two-object list, and the illegal-op row (58) plus its byte-scan of `0x4600010d` (59) now guard the kernel, which is the row that convicts anyone who writes a plain `(GLint)` cast into it -- and two new rows read that the kernel emits `mul.s`/`add.s` (the flag reached the file rather than being silently dropped) and that `glopVertex` has a `jal` into each half of it (the code is used, not linked dead).  Equality with the image it replaces is argued from construction and tested only where a test can reach it: the kernel performs the same single-precision operations on the same operands in the same order as the libcalls it removes -- and the shipped image already ran those operations on COP1, because `btron_fp_on` is set at boot and `FP_PICK` dispatches every single-precision call there -- while its conversions run the same exponent-band truncate the soft path was already running: `ps2_sf_fixsfsi()` moved out of `ps2_builtins.c` into `ps2_fp.h` as `static inline`, so the runtime's `__fixsfsi()` and the kernel's `TGL_TRUNC()` are two readers of one statement rather than a second copy of its arithmetic.  The instrument that could disagree with that argument is the pair of host suites that compile the moved source and look at what it paints, `make test-xmb-tgl` and `make test-fborder`, together with the 78 raster and 139 recorder checks, which sit downstream of the kernel and therefore cannot move unless a vertex moved.  A word-for-word capture of the recorded GIF stream before and after was planned for this row and rejected once its cost was read: the sentinel that would carry it is a flag on `core_ps2.pbench.o` alone, so it cannot be true in `eegs_present.c` where the stream is emitted, and a hash always on in the shipped image is about 0.5 ms a frame spent on the path this row exists to shorten.  **Landing, measured.**  `draw=` 74,047 -> **38,501 us** on the uncounted `PS2_GL=eegs PS2_FP=hw` image, re-run from a full-object rebuild so the number belongs to the artifact on disk (`build/ps2-smooth.log`, frame 3; frame 2 reads 38,587 and frame 1, which pays the load, 40,961), and the steady UI period 166-179 ms -> **129-141 ms**; against the 16,700 us budget that is 4.4x -> 2.3x, the first single change to move this frame by a factor of two.  **Those numbers were withdrawn the same day for a reason this row cannot see from inside: `PS2_FP=hw` was, at that moment, also the encoding bug of** *The compares the EE does not have* **below, so the frames that produced 38,501 us clipped all 5,406 of their faces and filled none of them -- `drawn=0`, `black=384000`.  The same dial, re-measured on the fixed image, reads frame 3 `draw=62,882 us` with `out=210 drawn=4968` and a steady period of 155-166 ms, against frame 1's 65,664 us; the 74,047 -> 38,501 ratio is therefore a comparison of a kernel that skipped the fills against a kernel that skipped the fills less, not the win this row claimed.  What survives the correction is the attribution, which never depended on the absolute cost: the counted frame's `S=` 460,861 -> 196,089, with `f_ribbon` and `f_menu` down by 85% and 94% and `f_surface` unmoved.**  The attribution came from the instrument this row named rather than from the ratio: counted frame 2's `S=` column 460,861 -> **196,089**, per stage `f_ribbon` 185,204 -> 27,776 and `f_menu` 113,214 -> 7,044, with `f_surface` unmoved at 160,893 -- row 8's premise, not this row's miss.  Gate **74/74** (`make test-ps2 PS2_GL=eegs`), `make -k test` green with both fb-order domains still normalising to the same 31,488 words, `make arm64-elf` untouched, and `foma`'s link break predates this row and is still reported rather than fixed.  Three controls, one edit each, all run: **(C1)** a plain `(GLint)` written back into the viewport map made 58 *and* 59 red -- 9 byte-scan hits against the 9 `trunc.w.s` in the disassembly -- but 59 was **green** on that same image until its pattern was generalised, because the literal it carried (`0d010046`) is one operand pairing while a real cast is `trunc.w.s $f6,$f6` = 0x4600318d; the generalisation, and the two false-positive shapes it had to be found through, are in the note below, and they are the reason this row's own gate needed repairing before it could convict the thing it was written for.  **(C2)** routing the truncate through a `jal __fixsfsi` instead of inlining it made 58 red through four `ldc1`/`sdc1` that no line of source asked for -- the compiler spilling live callee-saved FPR pairs in an O32 `FR=0` hard-float TU -- written up below because it is the one defect in this port that reading the source cannot predict.  **(C3)** `PS2_FP=soft` made 60 and 61 red (the harness exits at the first) and cost **500,386 us** a frame, not the 74 ms this row predicted: that dial is shared with `ps2_builtins.c`, so it withdraws COP1 from the entire soft-float runtime rather than from the kernel, which makes it a different and far larger lever than the absence of row 7.  The honest pre-landing baseline is the 74,047 us measured on this same image with `PS2_FP=hw` and the kernel absent. |

---

## 5c's decisions, written down before the code

<a name="five-c-decisions"></a>

**5c's decisions, written down before the code.**  5c is the first increment of this plan that can
change what a window looks like, so every one of the six below carries its consequence rather than
only its rule.

*Activation installs the pipeline that is linked and names itself.*  `gl_init()`'s `GL_BACKEND_EEGS`
case takes `g_gl = &g_tinygl_ops` and sets the active backend to `GL_BACKEND_EEGS`, because the GS
stage hangs on TinyGL's own `draw_triangle_*` hooks (row 5 refuses a second front end) -- the CPU
rasteriser is still what builds the vertices, clipping, textures and state; only the span loops are
replaced.  The consequence is that every existing test for `gl_active_backend() == GL_BACKEND_TINYGL`
was asking "is the linked CPU front end live?" and not "which rasteriser drew the spans", and once
EEGS is active six of them are wrong: `egl_make_current()` and `egl_destroy_surface()` would take the
virgl arm of a build that has no virgl context, `gl_resize()`/`gl_shutdown()` the same, and
`gl_fill_stats()` would answer a caller's question with zeroes.  They now ask one predicate,
`gl_tinygl_pipeline()`, which is true for TINYGL *and* EEGS; a `|| EEGS` written six times is six
places to forget the seventh.

*The order on the bus is canvas, environment, primitives -- so the stream is staged.*  The recorder
hands each full group to its sink while the application is still drawing, and the composite that
drives the drawing runs *before* `ps2_gs_upload()` of the band that contains the window.  A sink that
went straight to DMAC channel 2 would therefore put every triangle into VRAM and then overwrite it
with the canvas's own (now stale, because the layer is no longer blitted) copy of the same rectangle.
So `src/gl/eegs_present.c` owns a per-frame staging buffer, the sink appends whole groups to it, and
the environment packet and the staged groups go out after the uploads.  The DMA stays one send per
group, because a group is already a whole GIF packet with its own EOP and its own `NLOOP`; the staging
changes *when* a group is sent, never *how*.  Capacity was to be the recorder's own frame measurement,
about 13,700 quadwords for an XMB frame, with a counter for the groups that did not fit -- the same
shape as every other silent failure in this port, which is to say not silent.  That estimate is the one
claim in this section the measurement falsified, and the counter is what said so: see 5c's results
below.

*One layer, and it is a property of the window.*  `EGL_SURFACE` is the only thing that knows both the
`WND` and which backend answered `gl_init()`, so `egl_create_window_surface()` marks the window
`gl_layer` when the GS is live.  `redraw_all_windows_clip()` then does two different things for that
window: it still calls its paint (that is what records the stream) and it no longer blits its client
pixmap into the canvas.  The second half has a consequence the first does not: the canvas stops
holding a picture of the layer at all, so any band that presents over the layer's rect must have the
layer re-recorded underneath it.  Hence the rule and its two halves -- `wnd_damage_needs_paint()`
answers TRUE when a layer window intersects the damage (otherwise a banded pass picks the
`blit_only` composite and the layer is never re-recorded), and the walk re-runs the layer window's
paint even when its `img_valid` is TRUE.

*What is drawn on top of the layer has to be re-uploaded after it, one rect at a time.*  The panel
and its gold bar, the bottom test bars, an open deskbar or tracker dropdown and the IME candidate
window are all composited into the canvas and uploaded with the band, which the primitive stream now
follows -- so wherever the layer's rect overlaps them, the GS buries them.  The compositor therefore
registers, in `wnd_gl_cover_add()`, each canvas region it writes *after* the window walk, and the
present re-uploads every registered rect intersected with the layer after the stream.

Two things fell out of writing that down.  The first is that the accumulator cannot be a union: the
panel is rows 0-28 and the bars are the last 40, so their union is the whole canvas, and re-uploading
a union ∩ layer would replace the entire layer with the canvas's stale copy of it.  It is a bounded
list -- eight entries, with a drop count in `[GSTS]` because a dropped rect is a buried overlay with
no evidence attached -- and the cost is one small BITBLT per producer per frame (the panel is 5,600
quadwords against a whole-canvas upload's 120,000), which the band's own upload already paid for
everywhere *outside* the layer, since a primitive cannot reach past `SCISSOR`.  That is also why the
present does not upload the complement of the layer: those pixels are already on the bus.

The second is a limit on who may register.  A cover rect is re-uploaded from the canvas, and the
canvas holds no picture of the layer any more -- that is the other half of this increment -- so a
producer may register only what it paints *opaquely over its whole rect*.  The panel, the bars, the
dropdowns and the candidate window all do.  The pointer sprite does not: it writes its own 60-odd
white and black pixels and leaves the rest of its 16x16 box as the canvas had it, so uploading that
box over the layer would replace the layer's own pixels with the desktop background from before it.
Hence the sprite is not a cover producer, and the pointer is invisible while it is inside a GS
window -- the one visible thing increment 5c does not close, and not a thing the cover mechanism can
close.  The fix is not another canvas upload but a primitive: the GS's own blend law already says how.
With `ALPHA` at source-over, `(Cs - Cd) * As + Cd` returns `Cd` untouched wherever the source pixel's
alpha is 0 and paints `Cs` wherever it is 255, so a 16x16 CT32 plane holding the cursor bitmap
(alpha 255 on the arrow, 0 elsewhere) and one textured quad at the pointer position draw the pointer
*through* the layer -- clipped to it by the same `SCISSOR`, and outside it the canvas keeps drawing
the pointer exactly as today.  That is increment 5d's first job.

*The environment's four negotiation words have one answer each, and only `ALPHA` needed a fact.*
`FRAME` names the page the PCRTC already reads -- page 0, `FBW` 13, CT32, `FMSK` 0 -- because a
primitive and a blit that disagree about where a pixel lives have no way to say so.  `ZBUF` sits at
the last 8 KB page of VRAM, above every plane the cursor has handed out, with `ZMSK` 1 while
`TEST.ZTEN` is 0: nothing reads it and nothing writes it, so its only duty is not to be someone
else's address, and choosing it without the allocator leaves increment 3's and 5b's hand literals
where they are.  `TEST` is 0 -- no alpha test, no depth test, every fragment passes.  `ALPHA` is
`gs_pack_alpha(BLEND_COLOR_SOURCE, BLEND_COLOR_DEST, BLEND_ALPHA_SOURCE, BLEND_COLOR_DEST, 0)`, the
selects the GS's own equation `color = (c1 - c2) * a >> 7 + c3` names, which is
`(Cs - Cd) * As + Cd` -- TinyGL's `TGL_BLEND_ALPHA_ADD` and the XMB's `GL_SRC_ALPHA` /
`GL_ONE_MINUS_SRC_ALPHA` ask, spelled in the hardware's terms.  The field codes are why
`ps2_gs_pack.h` declined to name them at increment 2: position 3 is an *alpha* select where 0 means
`As`, while the other three are *colour* selects where 0 means `Cs`, so the same numeral 0 does not
mean the same thing in the four places.  They are named now, in the header, because a gate pins
them.  `CLAMP`/`TEX0`/`TEX1` go in as zero: the env packet names no plane, because
`eegs_texture()` emits the trio as an AD group before any vertex that samples, and `need_plane()`
will not let `TME` be set without one.

*What 5c does not close.*  Four things, all visible, none of them silent.  The pointer and any
overlay that is not an opaque rectangle: inside a GS layer's rect the pointer is gone, for the reason
in the paragraph above, and 5d's cursor plane and textured quad is the cure -- outside that rect the
canvas still draws it as before.  `glClear()` is TinyGL's
own framebuffer fill and is not a primitive, so the layer's background is the canvas's pixels showing
through wherever a triangle does not land -- invisible for the XMB, whose mesh covers all 768x500 of
its client area (`box=0,0..767,499` is that census), and a real gap for any app that clears and draws
less.  The GS has one drawing environment and one frame buffer, so a second GL window opened while a
layer exists draws into the *first* window's rect; glgears is the case, and the plan's answer to it has
always been that it stays on the CPU (fact 2 of this row).  And `D2_QWC` is a 12-bit field while the
canvas upload has been chunking at 8,000 quadwords since before this plan existed -- a fact about
every Host->Local transfer on real hardware, found here because 5c had to state the bound, and left
alone because changing it moves every upload number in this file.

---

## 5c's measured results, 2026-10-12

<a name="five-c-results"></a>

**5c's measured results, 2026-10-12.**

*The image gate is 72/72* (`make test-ps2 PS2_GL=eegs`), five rows past the 67 increment 5b left: 50
and 51 are the rewritten dial pair, 52 is the build default now that 3 is a legal answer, and 53-56 are
this increment's -- 53 the present brackets the composite with the recorder (it reads `(1 begin, 1 end,
1 send)`), 54 both of the layer's facts come from the window manager rather than a copy of them
(`wnd_gl_layer_take`, `wnd_gl_cover_take`), 55 the bus order as three call-site addresses inside one
function, 56 the present linked *and read*.  `ps2_paint_region` is static and `-O2` folds it into its
caller, so the slice is taken over both spellings and every one of those counts is measured in
`<ps2_paint_bands>`.

Six controls, one edit each, all of them on the shipped image:

- moving the band's `ps2_gs_upload()` below the present block -- the exact regression the ordering law
  exists for, and the one a later edit could write by accident -- fails **only 55**, with 53 and 54
  holding.  That is the evidence 55 measures an order and not a presence;
- deleting `eegs_present_end()` fails 53, which prints what it read instead of what it hoped:
  `(1 begin, 0 end, 1 send)`;
- replacing the take with `n_cover = 0` fails **only 54**;
- deleting the `eegs_present_get_stats()` call failed **nothing at all**, and this is the control that
  changed a gate rather than the code.  Row 56 counted `<eegs_present_*>` *definitions*, so a present
  that stages, sends and never reads its own counters still passed -- and the bug it would have passed
  is the one this port has chased twice: a `[GSTS]` row of zeros read as "the frame drew nothing" when
  the frame drew fine and nobody looked at the counters.  The row now requires the read as well, and
  the same mutation reports `(4 eegs_present_* functions, 0 stats read) [FAIL]`;
- `(GL_BACKEND)7` as the build default fails 52 with `GLDEF_RET=7` while 50 and 51 hold, which is what
  row 52 is for once the name is legal: a value the link cannot install is still a defect;
- putting the pre-5c placeholder arm back in `gl_init()` -- EEGS resolving through
  `gl_backend_software_default()` instead of installing the linked pipeline -- fails 50, reading
  `tinygl_backend_init 1 times`.  The retired law's replacement bites in the same direction the
  retired law did, which is the way to tell a retirement from an abandonment.

*The host gate is 64 checks, 0 failures, no warnings*, three controls: `ALPHA`'s destination select
written as `Cs` instead of `Cd` costs **2** -- `[FAIL] ALPHA is (Cs - Cd) * As + Cd
got=0x0000000000000004 want=0x0000000000000044` plus the hand-filled whole-packet row; neutering the
stage sink's copy loop costs **3** (`staged group 0/1 is the recorded one, word for word`, and the tag
check beside them); applying the environment after the group loop instead of before costs **29 of the
64**, starting `[FAIL] the first is the environment packet` -- and that ratio is the suite's own answer
to how much of it is one instrument rather than six independent ones, because the ordering is most of
what it asserts.

*What the machine said, and the defect it said it in.*  The first image to present through the GS
printed, on every frame of the XMB:

```
[GSTS] groups=9  qw=20478 verts=7434 prims=2478 gs=1074 us | drop=42 unsplit=0 texsw=22 texmiss=0 | cover=4 coverdrop=0
```

`drop=42` is the counter doing precisely what it was added to do.  A frame records 7,434 vertices, and
7,434 x 4 quadwords plus the 51 group tags those vertices are sorted into is **29,787 quadwords**, not
the ~13,700 this section's capacity estimate was built from -- an XMB frame is 2.2x that model, because
the model counted `f_bg`, `f_ribbon` and `f_menu` and not the icon cells and the rest of the walk.  The
stage held the first nine groups in recorded order and refused the other forty-two, so what would have
reached the screen was the beginning of a frame.  `EEGS_STAGE_QW_CAP` is 65,536 now (1 MiB of `.bss`,
which costs no image bytes against the budget in section 2), and the same frame reads

```
[GSTS] groups=51 qw=29853 verts=7434 prims=2478 gs=1148 us | drop=0 unsplit=0 texsw=22 texmiss=0 | cover=4 coverdrop=0
```

with `drop`, `unsplit`, `texmiss` and `coverdrop` all zero and 22 mid-stream texture switches for the
two sheets the XMB binds.  The suite's overflow row counts its triangles from the cap rather than from
a constant, so raising it again cannot quietly turn that row into a pass that tests nothing.

*One bench row changed meaning, and its pair says so.*  Under the GS the census reads
`xmb surface frame 3 : 768x500 black=384000 other=0 first_colour=0x00000000 box=-1,-1..-1,-1`, and
`black=384000` is not the black-XMB defect of the plan's earlier rows coming back: `ps2_bench_xmb_surface()`
walks the window's own device pixels, and the code that wrote those pixels was TinyGL's span loops --
the exact thing 5a replaced and 5c installed.  The same bench on the same sources with
`PS2_GL=tinygl` reads `black=0 other=384000 first_colour=0xff014c9d box=0,0..767,499` and prints no
`[GSTS]` row at all, which is the pair that says the row is a dial indicator rather than a broken
instrument: under EE/GS, "did this frame draw?" is answered by `[GSTS]`'s counts and by the screen, and
not by the surface.

*Timings, because 5c was also a speed claim.*  The same three frames, dial against dial: **31 / 200 /
366 ms** folded on the GS, **31 / 321 / 608 ms** on the CPU rasteriser -- the fill has left the
Emotion Engine, and `scripts/ps2_smooth.sh` puts the same fact in its own column: the XMB's `draw=`
goes from 176.5 ms to **74.047 ms**, 2.4x.  The cost of what replaced it is `gs=`, one number for the
whole post-upload phase (environment, 51 groups, then the cover re-uploads): **1,148 us** on a frame
that re-recorded the layer, **211 us** on one that did not.  The canvas rows are identical between the
two builds to the microsecond the bench prints -- `total=56.789 ms render=55969 us upload=820 us
swap=0 us` -- which is the direct evidence that 5c touches the present of the layer and nothing in the
compositor's own path.  `draw_us` is still 4.4x over its 16.7 ms gate and the three whole-canvas rows
still FAIL theirs; those are the pre-existing budgets and they are recorded rather than moved.

*One thing found outside the increment's own files.*  `make all` was at five of seven targets, and not
because of this port: `include/gl/math.h`'s two bit-twiddling helpers, `btron_dexp()` and
`btron_ldexp()`, sat inside the *no-x87* half of the freestanding branch while the `btron_log()` and
`btron_exp()` that call them sit below the architecture split -- so every x86 bare-metal target, UEFI
and PC-98 both built `--target=i686-none-elf`, died three TUs deep on `call to undeclared function
'btron_dexp'`.  The helpers now sit above the split, which changes no arithmetic on either branch and
no number in this file; uefi and pc98 build again.  `foma`'s link break is untouched and still
reported rather than fixed, per the standing decision: `clu.foma.o` wants `open_xmb_window`,
`open_glgears_window` and `open_lilcu64_demo_window`, and `FOMA_SRCS` lists none of those files.

*The screen said yes, and named the next job.*  The report from the user on the `PS2_GL=eegs` image is
that the EE/GS backend works -- the layer is drawn by the GS -- and that the 3D wave surface and the
menu animations are still slow.  Both halves are this increment's gate, and in different currencies:
the first is the claim nothing above could establish, and the second is `draw=`'s 74 ms against the
16.7 ms budget, which is what row 7 is written for.  The pointer inside the layer's rect (row 6) is
not answered by that report and stays open.

---

## Row 7's premises, measured before it was written

<a name="row-7-premises"></a>

**Row 7's premises, measured before it was written.**  `vertex.c` was compiled the way the kernel will
be compiled -- `--target=mipsel-unknown-elf -march=mips2 -mabi=32 -mhard-float`, the substitution
`ps2_builtins.ps2.o` already takes, and the same `PS2_CFLAGS` the image uses -- and the object reads
68 `mul.s`, 53 `add.s`, 2 `div.s`, 1 `neg.s`, 107 `lwc1` and 43 `swc1`, which is the win: arithmetic
that costs 28 cycles as a call today (15 of them the `jal` and its prologue) costs its own latency
plus the load.  It also reads 9 `trunc.w.s` and 21 double-precision instructions, and all thirty of
those are inside `glopVertex`, which at `-O2` absorbs both of the functions the row moves.  Three
consequences, each of which decides a piece of the shape:

*`(GLint)` cannot be written in a hard-float TU on this CPU.*  The nine `trunc.w.s` are the viewport
map's nine casts -- three position, four colour, two texture, counted from the source and matching
the instruction count exactly, which is how the row knows whose casts they are.  `ps2_builtins.c`'s
note above `fp_hw_cvt_s_w` records what happens when the EE is asked for that instruction: the
self-test made PCSX2 log `Unrecognized FPU/COP1 op 4600010d` and `$f4` kept the *previous* result,
so the illegal form is not slow but silently wrong, and its encoding family is what row 59 now
byte-scans `.text` for -- a family rather than that one word, which is written up below because the
word alone turned out to scan clean over an image full of the instruction.  The ISA level is not the
lever either: `-march=mips3` gives an identical instruction mix and `-march=mips1` does not compile
at all, so "make clang use `cvt.w.s`" is not on the menu.  The kernel therefore routes its
conversions through the truncate the port already owns, passing the float's *bits* in a general
register -- `ps2_builtins.c`'s own convention, crossing no ABI -- but it reaches it **inline rather
than by call**, and that half the plan got wrong: `ps2_sf_fixsfsi()`, the 47-cycle exponent-band
shift, moved out of that .c file into `ps2_fp.h` as `static inline`, so the runtime's `__fixsfsi()`
and the kernel's `TGL_TRUNC()` are two readers of one statement instead of two copies of its
arithmetic.  Conversion prices do not improve and were never the target: 53,041 of the frame's calls
are `float -> int` and cannot go to COP1 at all, while 373,820 are add/mul/div/compare and can.

*Calling for the truncate was measured and rejected, and the reason is not the call.*  Control C2: a
kernel doing `jal __fixsfsi` nine times gained two `sdc1` in its prologue and two `ldc1` in its
epilogue -- four double-width FP accesses no line of source asked for, on a CPU that swallows them
without answering.  The mechanism is the O32 `FR=0` convention this file compiles under:
callee-saved FPRs are **even/odd pairs**, so preserving one live across a call is a single `sdc1` of
the pair, which makes the law stronger than the source suggested -- a hard-float TU on this CPU may
not make *any* call with a live float.  `-mfp64`, the 64-bit-FPR convention that would break the
pairing, is refused by clang on this target.  So the kernel makes one `jal` in total, into the `V3*`
wrapper that stays in the soft-float TU, and converts inline: the object reads 9 `mfc1`, 1 `jal`, 0
`ldc1`/`sdc1`, 0 `.d`, 0 `trunc.w.s`.  Row 58 is what catches a regression here, its mnemonic list
naming `ldc1`/`sdc1` outright; row 59's byte scan would not see it, its patterns being the trunc and
unordered-compare encodings -- which is why the spill is recorded in prose and not only in a row.

---

## Row 59's byte scan, and what it took to give it teeth

<a name="row-59-teeth"></a>

**Row 59's byte scan, and what it took to give it teeth.**  The row shipped as a grep for one word,
`0d010046`, over the image's hex, and control C1 showed it could not see the thing it existed for:
the plain cast came back as nine `trunc.w.s` in the disassembly and **zero** hits on that literal,
because the encoding carries its operand registers -- `trunc.w.s $f4, $f4` is the pairing the FPU
self-test hand-writes, while a C cast becomes `trunc.w.s $f6, $f6` = 0x4600318d.  `trunc.w.s` is
`cop1 | 16 | rt=0 | fs | fd | 0x0d`, which little-endian lays out as `[048c]d ?? 00 46` with `fd`
even for the same 32-bit-FPR reason above, so the row now scans that family plus `c.un.s`'s
`31000246`.  Two false shapes were measured on the way, and both are how a byte scan silently stops
discriminating: reading windows at every byte offset matched 10 places where the disassembly held 9
instructions *and* one place on the clean image, so alignment is the law rather than a refinement,
and scanning the file rather than the section counts bytes that are pixels or literals.  The row
therefore takes `.text` through `readelf --hex-dump`, which is 4-byte aligned by construction, and
asserts the dump is at least 1,000,000 hex chars before a zero counts as a pass -- the shipping
image prints its own length in the row's description (2,540,192 hex chars today), so a renamed
section fails loudly instead of passing quietly.  Re-measured both ways: 9 hits against 9
instructions on control C1, 0 on the shipping image.

*The lighting branch stays behind a call, because it is double-precision.*  The 21 `.d` instructions
are `gl_V3_Norm_Fast()` inlined at `vertex.c:187` -- the `normalize_enabled` arm of the transform --
reaching `sqrt`'s double series in `include/gl/math.h`.  The census proves that arm does not run on
this app (`D=0` in every stage of the eegs frame), and the gate does not care what runs: a row that
forbids an instruction forbids it in the image.  So the kernel calls a `V3*`-taking wrapper that
stays in the `-msoft-float` TU, which keeps the doubles where they are today at the measured 536
cycles and keeps the kernel's op set inside what the EE answers.

*"Bit-identical" here means identical to the image it replaces, and the header says why that is the
right claim.*  `ps2_fp.h` records the measured relationship between the two engines: COP1's
`add.s/sub.s/mul.s` land one representable step from the bit engine on a minority of ordinary pairs,
never wider and never across zero, while `div.s`, the compares and `cvt.s.w` match it on every clean
pair.  The shipped image already runs the hardware today, because `btron_fp_on` is set at boot and
every single-precision libcall dispatches through `FP_PICK`.  The kernel is the same instruction on
the same operands in the same order as the calls it removes -- so the comparison the row makes is
against the eegs image's own recorded words, not against an oracle that image already stepped over.

*What the row gives up, deliberately.*  `btron_fp_on` exists so that, in its header's words, "a machine
that does not hand over a working FPU costs cycles rather than pixels".  A hard-float TU cannot honour
that at runtime without a second codegen of the same source -- a fallback layer whose only branch no
machine in this port's loop has ever executed -- so row 7 moves the valve from run time to build time
instead of duplicating the text: the kernel takes `-mhard-float` only under the existing `PS2_FP=hw`
condition, and `make ps2 PS2_FP=soft` rebuilds it `-msoft-float`, which is exactly the code that ships
today, one flag away.  If a real console ever boots with the FPU off, the boot row says so and that is
the escape hatch; the alternative accepted cost is that such a machine is not in the loop yet.

*The trap that will make the row look like a pass when it is a no-op.*  The substitution is a
per-object flag, so changing `PS2_FP` does not rebuild the kernel object -- the same disease
`PS2_GL` had, which is why `Makefile:1109-1124` compares a stamp's *contents* and deletes the objects
that read the value.  The law now covers this second dial too: `PS2_FP_STAMP` does the same content
comparison and `rm -f`s both objects that read `PS2_FP`, so a bare `make ps2 PS2_FP=soft` is honest.
It inherited the first stamp's defect and that is worth recording rather than smoothing over, because
the comparison runs in a `$(shell ...)` while make is *parsing*: `make -n PS2_GL=tinygl <object>`
executed it, rewrote `.build/ps2-gl.stamp` and deleted `gl_backend_choice.ps2.o`, `gl_dispatch.ps2.o`
and `egl_surface.ps2.o`.  A dry run is not a shield.  The next build with the intended value restores
all three, and the rule is to pass the dial on every make invocation, dry runs included.

*The estimate, and the instrument that replaces it.*  The counted frame 2 splits as 373,820
add/mul/div/compare + 53,041 `fs2i` + 34,000 `fi2s` = 460,861 calls.  What the kernel holds per vertex
is about 24 mul/add in the no-lighting projection, about 7 in `gl_clipcode`, and 22 mul/add/div plus
the 9 conversions in the viewport map -- against f_menu's measured 49.5 `fmul` per vertex (71,068 over
1,436 verts) and f_ribbon's 56.5 (112,053 over 1,984), so roughly half of the frame's arithmetic by
that per-vertex reading.  That is *inferred* from the op counts, not measured, and the row's own
instrument settles it: a counted frame's `S=` column falls by exactly the calls that stopped being
calls, per stage, so the same run that prices the win attributes it.  The next TU in the same shape is
`clip.c:17`'s `gl_transform_to_viewport_clip_c()` -- pointer-only signature, three casts, same rule --
and if the `S=` delta says the ribbon's other half is elsewhere, that is where the same treatment
goes next rather than a second guess at the split.

---

## Row 8's landing: the lattice, the register file, and which build each number came from

<a name="row-8-landing"></a>

**Row 8 landed on 2026-10-12 and its evidence is two different kinds of thing, so it is kept apart
here.**  The file's own instruction counts are a property of one translation unit read off its
object; the frame's microseconds are a property of an image run under PCSX2, and two such images ran
that evening whose `draw=` columns are not comparable to each other.  Both name their build the same
way (`[CFG] this image: PS2_FP=hw:cop1  PS2_GL=eegs`), and they differ in exactly one dial:
`build/ps2-bench.log`, build Oct 12 2026 20:52:52, prints **zero** `census` rows -- the uncounted
image -- and `build/ps2-smooth.log`, build Oct 12 2026 22:44:20, prints eighteen -- the counted one.
Row 7 measured what the census itself costs: 84,312 us counted against 74,489 uncounted for the same
frame.  So each triple below is differenced only against its own kind, per the specification's
reading rule R3.

*The app frame, uncounted, before and after.*  Frames 1/2/3 of the 20:52:52 image read `draw=`
65,859 / 63,576 / 63,132 us.  The post-row-8 uncounted run survives in `build/ps2-smooth-history.tsv`
as the archived row `2026-10-12T19:37:31Z` -- `frame1_us` 49,175, `draw_us` 46,273, and no census
columns, which is what marks it as the same kind of build.  63,132 -> 46,273 us is 26.7% off a steady
frame from one file; against row 7's own landing reading of 62,882 us the pair agrees to 4%, which is
the run-to-run spread of this harness and the reason a single frame is never quoted as a triple.

*The app frame, counted, and what moved inside it.*  Frames 1/2/3 of the 22:44:20 image read `draw=`
51,816 / 49,228 / 48,659 us, and its float census is the attribution:

```
[XMBT] f_surface     8470 us
[XMBT] census f_surface  fmul=5163 fdiv=39 fcmp=1432 fs2i=1027 fi2s=1 S=7662 dmul=930 dgen=775 D=1705
[XMBT] hash 160 of 8192 lattice points computed
...
[XMBT] f_surface     5738 us
[XMBT] census f_surface  fmul=1084 fdiv=39 fcmp=129 fs2i=1028 fi2s=6 S=2286 dmul=0 dgen=0 D=0
[XMBT] hash 160 of 16384 lattice points computed
```

`f_surface` entered row 8 at 160,893 (row 7's landing, the same instrument) and leaves it at 2,286 --
a 70x drop, and the largest single-column change in this port's history.  The remaining 2,286 is not
the lattice: 2,048 of it is xmb.c's own motion instrument, the first-six-paints probe at
`src/apps/xmb.c:2861-2866`, which walks all 1,024 grid points twice over with `* 1024.0f` and an
`(int)` cast -- 1,024 `fmul` + 1,024 `fs2i`.  Read the census against that: `fmul` 1,084 = 1,024
instrument + 60, `fs2i` 1,028 = 1,024 instrument + 4, and the whole residue is 2,286 - 2,048 = 238.
The instrument lives in the `-msoft-float` unit, so its arithmetic is still libcalls and still
counted, while the wave unit's own casts -- one `float -> int` per cell where the shader used twelve,
`src/apps/xmb_wave.c:214-227` -- are now inline COP1 and therefore *invisible to this census*.  That
is why the column fell so far and why 238, not 0, is the floor for as long as the probe is compiled.
Frame 1 reads `D=1705` and 7,662 single calls because the lattice's first fill misses the sine cache
(160 of 8,192 points new); frame 2 reads `D=0` with the same 160 misses out of 16,384 visits, which is
the cache working, and it is the second frame's number the handbook quotes.

*The register file, which is the part the source does not show.*  Row 8 is the second `-mhard-float`
translation unit in this image, so it met the O32 `FR=0` callee-save disease on its own terms: a
working set over ten floats, a float live across a call, or a hoisted literal is enough for the greedy
allocator to pick `$f20-$f31`, and on this CPU the `sdc1` that "preserves" it does nothing at all.  The
ladder, measured on `src/apps/xmb_wave.c` and recorded in `scripts/test_ps2.sh` above the
register-file row: **52** pair stores in the first four-pass arrangement, **36** in the eight passes,
**18** after the trig and the root were outlined, **4** with the allocator simply discouraged from
choosing a CSR, and **0** once the shading pass's eight live values were split into two passes.  The
control that makes the last number an instrument rather than a coincidence: compile this same source
without `-mllvm -regalloc-csr-first-time-cost=1000000000` and 14 pair stores plus 32 CSR references
come back.  The flag is attached to the three objects that read `PS2_FP` (`Makefile:1237-1256`:
`ps2_builtins.ps2.o`, `vertex_stage.ps2.o`, `xmb_wave.ps2.o`), and an unknown `-mllvm` argument fails
the compile, so a toolchain that renames it stops the build instead of reopening the hole quietly.  The
gate is the assertion named *No callee-saved single ($f20-$f31) is referenced anywhere in the image
(sdc1 cannot preserve one on this CPU)* -- the register file itself, not the opcode, because a function
that merely *reads* a CSR is already broken without ever storing one.

*What row 8 did not touch, recorded because it is row 9's premise.*  The same counted frame reports
`compositor: bg=9923(480000 px) frame=13075 paint=0 blit=30755(613440 px) comp=56128` and
`[GSTS] groups=51 qw=29721..30165 verts=7401..7512 prims=2467..2504 gs=212..1142 us | drop=0 unsplit=0
texsw=22 texmiss=0 | cover=2..4 coverdrop=0`.  Not one of those columns moved: row 8 changed the EE's
arithmetic and the GS stream is byte-for-byte the same shape it was.  The composite's 56.1 ms and the
blit's 30.8 ms over 613,440 pixels are still EE work, and the steady period for this build reads
144,075 / 141,338 / 151,696 us against the uncounted pre-row-8 build's 157,890 / 155,637 / 166,395 --
two ranges whose difference is not attributable, for the counted/uncounted reason above, and which are
therefore printed side by side rather than subtracted.

*Gates at landing and at re-read.*  The image suite was green at both dials on the landing day and
re-read on 2026-10-13 at **91 rows, 0 failures**; `make arm64-elf` is untouched by row 8, which never
edited a file the Pi 400 compiles except `src/apps/xmb_wave.c`, whose guard (`-DBTRON_PS2_FP_HW`) is
read only under the PS2 flag.

---

## Row 9a's landing: two builds, one refused rect, and the instruments that watch the premise

<a name="row-9a-landing"></a>

**Row 9a landed on 2026-10-13, and its evidence spans two builds, so each row below is attributed
before it is read.**  The lane is a refusal -- `draw_retro_window_frame()` declines to fill the body
plate under a window whose client the Graphics Synthesizer owns -- and a refusal's claim is about a
number that *stops* moving, which needs a before and an after from two different images.  Both are
`PS2_GL=eegs PS2_FP=hw` bench images, and the one difference between them is the lane:

*The build that only measured.*  It carries the `body=` column and not the refusal, which is exactly
the state a premise is taken in.  Its three XMB cycles, verbatim:

```
comp bg=9937 frame=20380 paint=49185 blit=30795 panel=1835 bars=576 total=112711 bfn=0 wins=4/4 body=1109568px
comp bg=9919 frame=20336 paint=46740 blit=30732 panel=1825 bars=576 total=110146 bfn=0 wins=4/4 body=1109568px
comp bg=9897 frame=20314 paint=46308 blit=30728 panel=1803 bars=569 total=109627 bfn=0 wins=4/4 body=1109568px
```

Every column of these three rows is a range, not a number, and the ranges are the reason the handbook
quotes a marginal rate rather than a per-frame one: `frame=` spans 20,314-20,380 µs over a constant
`body=1109568px`, so the walk's own run-to-run spread is 0.3% of the stage.  The regression's first
point is the same build's whole-canvas desktop walk, which that log printed as
`compositor: bg=9924(480000 px) frame=13069(715360 px) paint=0 blit=30755(613440 px) comp=56121 bfn=0
wins=3/3` -- no `skip=` column, because the refusal did not exist yet.  Both points are therefore from
one image, which is what makes them a line rather than a comparison of two builds.  That log no longer
exists on disk; its successor does, and it repeats the desktop row exactly -- line 365 of the current
`build/ps2-bench.log` reads `frame=13069(715360 px skip=0)` with the same `bg=9924`, `blit=30755` and
`comp=56121`.  Every number in that row is unchanged across the two builds, because the lane cannot
reach a walk with no layer window: that is the control, and it is also why the withdrawn log can be
quoted from memory rather than re-measured.

*The build that refused.*  Lines 475 / 490 / 507 of the current `build/ps2-bench.log` (mtime Oct 13
03:41), verbatim:

```
[XMBT] cycle 0 … | comp bg=9937 frame=13833 paint=49429 blit=30796 panel=1834 bars=576 total=106404 bfn=0 wins=4/4 body=725568px skip=384000px
[XMBT] cycle 1 … | comp bg=9913 frame=13828 paint=46850 blit=30716 panel=1832 bars=563 total=103713 bfn=0 wins=4/4 body=725568px skip=384000px
[XMBT] cycle 2 … | comp bg=9888 frame=13785 paint=46514 blit=30696 panel=1790 bars=569 total=103251 bfn=0 wins=4/4 body=725568px skip=384000px
```

Pairing them by cycle index rather than by taking the largest of each: 6,547 / 6,508 / 6,529 µs off
`frame=`, and 112,711 / 110,146 / 109,627 → 106,404 / 103,713 / 103,251 µs off `total=`.  Over the
384,000 px each cycle refuses, that is 17.05 / 16.95 / 17.00 ns/px against the 18.55 ns/px the two
points give as their marginal rate.  **The 8% shortfall is the interesting half of the row, so it is
written out instead of being smoothed**: the refusal removes the plate's *inner* write and not the
walk's bookkeeping around it.  `fill_body_part()` is called four times with the buried rect as a
horizontal or vertical strip, the four strips together being the body minus the client, so the
border, the inset arithmetic, the `RECT` clamp against the body and the four call sites all still
run -- and 394,208 px of that arithmetic is what `body=725568px` still counts for the XMB's ring
(394,208 body − 384,000 client = 10,208 px).  A poster lane would take the strips too; a refusal
cannot, which is the whole difference between 17.0 and 18.55 and the reason the two rates are printed
against each other rather than averaged.

*Instrument 1 -- `body=` / `skip=`, and why they are decision counts.*  `s_frame_body_px` and
`s_frame_body_skip_px` in `src/window/wnd.c` are reset at the top of the walk and published through
`btron_render_stat_max()` into `g_render_stats.frame_body_px` / `frame_body_skip_px`.  They count what
the walk *decided*, not what it wrote: an ordinary window's body is added whole whether or not a band
clips it, and a layer window's client is subtracted whole whether or not the damage reaches it.  That
is what makes the pair usable as the arithmetic above -- 1,109,568 − 725,568 = 384,000 exactly, the
client area of one 768×500 window -- and it is what makes the pair unable to certify anything about a
single pixel, which is why the set equality lives in a host test rather than in a log column.

*Instrument 2 -- `gs_rec_bbox_first()`, `b0=` and `plate=`.*  The refusal is sound only while the
stream covers the whole rect it buries, and the recorder now answers that question about itself.
`src/drivers/ps2/ps2_gs_rec.c` accumulates the span of the **first vertex group** only -- four floats,
one `s_span_done` that turns the accumulation off the moment that group closes -- and freezes it into
`s_span0[]` with `span_ceil()` on the two low edges and `span_floor()` on the two high ones, so the
span is rounded **inward**: a vertex at 767.4 reaches 767 and never 768.  `core_ps2.c` compares that
span against the environment packet's own clip and prints `clip=768x500 b0=0,0,767,499 plate=covered`
in `[GSTS]`; `SHORT` means the span does not reach all four edges, and `none` means the frame recorded
no vertex group at all.  Why the first group and not the union of all of them, since a union is the
obvious instrument: a union would answer "does some pixel of the stream land in the rect", which is not
the premise; the premise is that the stream plates the rect, and only the first group can be required
to do that, because it is the group the app's own background fill forms.  The cost of that choice is
stated with the instrument: it is a *necessary* condition, not a sufficient one -- edge-reaching says
nothing about interior fragment coverage, which is a rasteriser property and row 6's gate, i.e. the
user's eyes.  Every `[GSTS]` row of the landing run reads `plate=covered` (lines 476 / 491 / 508) and
no `[GSPLATE]` line appears anywhere in the log; the warning is one-shot per session, armed only when
`has_bbox && !plate_covers`, and its text names the consequence ("a layer's body fill stays required")
so that a run which shows it is a run whose 9a must be reverted, not a run to be explained.

*The host gate, because a refusal's set equality is not visible on a screen.*
`verify/tests/test_desktop_clip.c`'s `layer_plate_yields_the_stream_rect()` paints one 800×600 canvas
four ways over one 292×164 client rect and pins the difference set from both directions: the windowless
reference, a records pass with `gl_layer` set (the refusal), the same pass with `blit_only` set (no
stream, so the plate stays), and the records pass with the flag cleared (the Pi 400's picture).  It
asserts the plate's hole, the window's `client` and the stream's clip are one and the same rectangle
before comparing anything -- 204,152,496,316, 47,888 px -- then that no pixel outside the hole differs,
that every pixel inside it is the desktop and never the plate's LTGRAY, that the flag-cleared pass
returns the blitted client art, that the counters read 47,888 skipped and 40,192 filled, and that a
32×32 damage box over the hole's corner reports the whole rect while writing only its own 1,024 px.
The last case is the grip: `WND_ATTR_RESIZE`'s `fill_rec()` and three hatch lines sit *inside* the
client rect, so with the attribute on, 100 px of the hole are canvas-drawn and still drawn -- which is
why the count stays the rect's 47,888 even though the walk writes 100 of those px, and why the test
asserts the grip's pixels rather than trusting the count.  **42/42 checks, 1 SKIP**, and the SKIP is the
system panel, whose `global_menu_render_bar` is a stub in that harness.

*Gates at landing.*  `make test-ps2` **91/91**, `make arm64-elf` builds, `make -k test` exits 0 with
the host suites at 100%, `vram_left=1081344` unchanged (9a claims no plane, and a plane count that
moves under a refusal lane would mean the lane grew into 9b mid-change), `[GSTS]` keeps
`drop=0 unsplit=0 texmiss=0 coverdrop=0` with `groups=51` where it was, and the `bg=`/`paint=`/`blit=`
/`panel=`/`bars=` columns stay inside their own run-to-run spread while `frame=` alone falls.  What 9a
does *not* buy is still priced in the handbook: 715,360 px of desktop-window plates, `bg=` 9.9 ms,
`blit=` 30.7 ms.

---

## The softfloat differential: one count, three controls, and why the count is dated

<a name="softfloat-controls"></a>

**`make test-ps2-softfloat` (`.build/test_ps2_softfloat`) is a differential, not a suite of
assertions.**  It includes `src/drivers/ps2/ps2_builtins.c` textually -- the helpers are file-static,
so there is no other way to reach them -- drives the runtime with random bit patterns mixed with
special values, a power-of-two sweep, named ties, subnormal cancellation and overflow, and compares
every answer to the host's own FPU bit-for-bit.  Its reading on **2026-10-13** is
**18,384,493 comparisons, failures=0**.  The total is quoted with its date because it is a property of
the suite, not of the runtime: two earlier readings, 15,222,125 and 15,225,197, were both in the
handbook at once until they were dated, and the difference between them is cases that were added
between the runs.  The invariant that does not move is `failures=0`.

*Control 1 -- the rounding carry-out.*  `ps2_builtins.c:200` is
`if (kept >> (frac + 1)) { kept >>= 1; E += 1; }`: when rounding a 53-bit significand up pushes it out
of range, the exponent must be incremented on the way back.  Deleting the increment reports **1,488
failures**, and the `[FAIL]` rows name `thirds` and `truncdfsf2`.  Re-run 2026-10-13; restored and
re-gated to zero before control 2.

*Control 2 -- the 64x64 multiply's middle word.*  `ps2_builtins.c:289` is
`const uint64_t S = (uint32_t)M + (L >> 32);`: the four 32x32 partial products are recombined by
32-bit windows, and a missing window is a wrong carry, not a wrong bit pattern.  Removing the 32-bit
truncation reports **450,068 failures** naming `muldf3`.  Re-run the same day, one control per command,
and the clean source restored afterwards (`git diff --stat` empty, `failures=0`).

*Control 3 -- the truncate's band edge, which belongs to a different date.*  The exponent-band
truncate `ps2_sf_fixsfsi()` (`src/drivers/ps2/ps2_fp.h:236-250`) saturates at `be >= 158`, and the
differential checks it over every exponent field x the three mantissas that can land on a band edge x
both signs -- 3,072 comparisons -- plus 200,000 random patterns.  Moving the saturation line to 159
made **694 comparisons fail**, including ones the older random pools never touched, and it was then
moved back.  That control was run when the line was written, i.e. the day the multi-word
`uint64_t` truncate was replaced by the two-shift form; it is recorded with that date rather than
merged into the 2026-10-13 re-run, because a control that was not re-run is not evidence that it was.

*Why all three matter to this port specifically.*  The runtime is the only libm and the only
`-fpack-struct`-free arithmetic this image has; there is no second implementation to diff against on
the console, and no display to notice a wrong bit on.  A green total that has never been shown to be
capable of turning red proves nothing about 18 million comparisons -- so the rule this doc follows is
that every claim of bit-exactness in `doc/md/PS2.md` names the control that can falsify it, and each
control is attributed to a routine (`thirds`, `truncdfsf2`, `muldf3`) rather than to the suite as a
whole.

---

## Row 9's pre-gate, RAM-to-RAM: the control that retired the handbook's own price row

<a name="ram-to-ram-control"></a>

**What was withdrawn, and the sentence it was inside.**  `PS2.md`'s price list billed *a pixel copied
RAM to RAM* at **14.8 cycles**, and the third column drew the conclusion that made row 9 look
one-sided: "613,440 px in 30,755 us, and 50.2 ns/px on every row from 768 px to 613,440 px -- three
orders of magnitude of size at one price, so it is the DRAM, not the loop".  That reading is what made
the EE-side half of row 9 look already shut.  The withdrawal is not that the
number was mis-measured; the row was reading real columns.  It is that the *argument* was not
falsifiable: a per-iteration byte tail is size-flat too, so flatness cannot separate a bandwidth from
a loop, and nothing in this tree had ever moved the same bytes at two instruction counts.  The same
inference was live in two places outside the handbook, and both were rewritten the same day: this
bench's own phase header, which had been arguing the DRAM case in its own commentary, and
`src/apps/xmb.c`'s `f_bg` block, whose consequence ("a copy is not much cheaper than a fill -- both
are DRAM-bound") is now replaced by the measured 4.6-cycle floor and the image-budget gate that
actually stops the cache.

**The control, and how it answered.**  `ps2_bench_ram_ops()` in `src/cores/core_ps2.c` prices one band
at four instruction costs on the same destination and the same memory.  The first phase, run the same
day *before either leaf shipped*, came back 96.4 ns/px through the byte tail, 21.2 peeled to the
shared 8-byte phase, 16.9 at 16 bytes, 16.1 at 32 -- a 6x spread on identical bytes.  **Those four
rungs are recorded here rather than re-greppable**: the landing overwrote `build/ps2-bench.log`, and
the 96.4 rung is not reproducible from the current image without un-landing the peel.  The aligned
twin of the old header cost 27.8 at the body width it used to carry -- the same loop as the peeled
one at 1.31x the price, because clang inlines the header's body into the compositor's row loop with
two `addu`s per iteration instead of the two `addiu`s it compiles when the body stands in a function
of its own, 11 instructions per 8 bytes against 9.  Loop *shape* as well as loop alignment is what the
pixel is charged for; that is the finding no reasoning about bandwidth could have produced.

**The instrument's own bug, because that is why `chk=` exists.**  The dispatcher in `ps2_bench_ram()`
read `mode >= 2` where it meant `mode == 2`, so the 16- and 32-byte *copy* rungs fell into the fill
branch and printed `fill_rec()`'s price under a copy tag.  Failure count: **three rows in a row, in
each of the first four runs**, all three carrying the fill rows' fold `sum=0xf9f6d730` while the real
copy rows beside them folded `0xf91c64b4` / `0xf68ac30c`.  Nothing in the log said so -- the tags
disagreed with the branches, and the only field that could show it was not being read.  The fix is
the field: every row that moves bytes now folds its destination *and* its source band and prints
`chk=ok` or `chk=MISMATCH`, and a fill prints `chk=fill` because it has no source to fold.  The
positive control is reintroducing `>=`, which must turn three copy rows red rather than silent.

**The landed run, verbatim** (`build/ps2-bench.log`, 2026-10-13, `PS2_GL=eegs PS2_FP=hw`; lines 363-396
and 400):

```
[BENCH] RAM-to-RAM, the loop or the DRAM -- same bytes at two instruction costs
[BENCH] copy even      400x  16     6400px/rep    102 us/rep max    114 us  15.96 ns/px  sum=0xf91c64b4 chk=ok
[BENCH] copy odd       399x  16     6384px/rep    103 us/rep max    115 us  16.17 ns/px  sum=0xf68ac30c chk=ok
[BENCH] copy odd+peel  399x  16     6384px/rep    135 us/rep max    147 us  21.23 ns/px  sum=0xf68ac30c chk=ok
[BENCH] copy even+16   400x  16     6400px/rep    107 us/rep max    119 us  16.74 ns/px  sum=0xf91c64b4 chk=ok
[BENCH] copy even+32   400x  16     6400px/rep    101 us/rep max    113 us  15.79 ns/px  sum=0xf91c64b4 chk=ok
[BENCH] copy odd+16    399x  16     6384px/rep    108 us/rep max    120 us  16.93 ns/px  sum=0xf68ac30c chk=ok
[BENCH] copy odd+32    399x  16     6384px/rep    103 us/rep max    115 us  16.14 ns/px  sum=0xf68ac30c chk=ok
[BENCH] copy odd+16b   399x  16     6384px/rep    108 us/rep max    120 us  16.91 ns/px  sum=0xf68ac30c chk=ok
[BENCH] copy odd+32b   399x  16     6384px/rep    102 us/rep max    115 us  16.12 ns/px  sum=0xf68ac30c chk=ok
[BENCH] fill even      400x  16     6400px/rep     60 us/rep max     73 us  9.49 ns/px  sum=0xf9f6d730 chk=fill
[BENCH] fill odd       399x  16     6384px/rep     61 us/rep max     74 us  9.65 ns/px  sum=0xf9f6d730 chk=fill
[BENCH] fillunr even   400x  16     6400px/rep     66 us/rep max     79 us  10.40 ns/px  sum=0xf9f6d730 chk=fill
[BENCH] fillunr8 even  400x  16     6400px/rep     57 us/rep max     71 us  8.99 ns/px  sum=0xf9f6d730 chk=fill
[BENCH] fillunr odd    399x  16     6384px/rep     66 us/rep max     80 us  10.43 ns/px  sum=0xf9f6d730 chk=fill
[BENCH] fillunr8 odd   399x  16     6384px/rep     58 us/rep max     72 us  9.17 ns/px  sum=0xf9f6d730 chk=fill
[BENCH] blit scan      400x  16     6400px/rep    255 us/rep max    266 us  39.86 ns/px  sum=0xf91c64b4 chk=ok
[BENCH] blit wide      400x  16     6400px/rep    234 us/rep max    245 us  36.57 ns/px  sum=0xf91c64b4 chk=ok
[BENCH] blit odd scan  399x  16     6384px/rep    256 us/rep max    267 us  40.11 ns/px  sum=0xf68ac30c chk=ok
[BENCH] blit odd wide  399x  16     6384px/rep    235 us/rep max    246 us  36.87 ns/px  sum=0xf68ac30c chk=ok
[BENCH]   -- size sweep, even phase, load+store and store-only
[BENCH] copy 768px      48x  16      768px/rep     15 us/rep max     28 us  19.74 ns/px  sum=0xff234350 chk=ok
[BENCH] copy 12800px   800x  16    12800px/rep    200 us/rep max    212 us  15.65 ns/px  sum=0xed17d04b chk=ok
[BENCH] copy 12800+32  800x  16    12800px/rep    200 us/rep max    211 us  15.65 ns/px  sum=0xed17d04b chk=ok
[BENCH] blit 12800     800x  16    12800px/rep    507 us/rep max    516 us  39.68 ns/px  sum=0xed17d04b chk=ok
[BENCH] blit 12800w    800x  16    12800px/rep    464 us/rep max    473 us  36.28 ns/px  sum=0xed17d04b chk=ok
[BENCH] copy 80000px   800x 100    80000px/rep   1254 us/rep max   1265 us  15.68 ns/px  sum=0x777bf3e1 chk=ok
[BENCH] copy 240000px  800x 300   240000px/rep   3767 us/rep max   3772 us  15.69 ns/px  sum=0xc993ec1b chk=ok
[BENCH] fill 768px      48x  16      768px/rep     14 us/rep max     27 us  18.41 ns/px  sum=0xff234350 chk=fill
[BENCH] fill 12800px   800x  16    12800px/rep    112 us/rep max    125 us  8.76 ns/px  sum=0xf4ca6b10 chk=fill
[BENCH] fill 80000px   800x 100    80000px/rep    704 us/rep max    710 us  8.80 ns/px  sum=0xb9f11d24 chk=fill
[BENCH] fill 480000px  800x 600   480000px/rep   4221 us/rep max   4225 us  8.79 ns/px  sum=0x5ba6aed8 chk=fill
[BENCH] fillunr 480k   800x 600   480000px/rep   4960 us/rep max   4966 us  10.33 ns/px  sum=0x5ba6aed8 chk=fill
[BENCH] fillunr8 480k  800x 600   480000px/rep   4350 us/rep max   4357 us  9.06 ns/px  sum=0x5ba6aed8 chk=fill
[BENCH]           compositor: bg=7490(480000 px) frame=7192(715360 px skip=0) paint=0 blit=24426(613440 px) comp=40961 bfn=0 wins=3/3
```

Two instrument limits are inside that block and are recorded rather than smoothed.  There is **no
480,000 px copy row**: a copy needs a source band as large as its destination and the two may not
overlap, so 800x300 = 240,000 px is as far as a copy can be taken on this canvas, and the bench prints
`ram: rect <w>x<h> does not fit the canvas, skipped` instead of losing the row quietly -- which is how
a sweep would otherwise look flat because it stopped early.  And the odd/even pair is the whole
alignment defect in one character: on an 800-px canvas an even destination column is 8-aligned in
bytes and an odd one is 4-mod-8.

**The two leaves the ladder bought.**  32 bytes per iteration went into
[`include/btron/fast_blit.h`](file:///Users/tonpa/depot/bitedits/btron/include/btron/fast_blit.h)
(generic half, so the PS2 compiles it; the aarch64 half already peeled) on `copy even+32` at 15.79
against `+16` at 16.74, repeated as `+32b` 16.12 / `+16b` 16.91, and on the sweep's `copy 12800px`
reading 15.58 in the landing run and 15.65 in this re-run -- 0.4% apart, which is the row-to-row drift
rather than a second result, and the size `bg=` actually bills.  Eight stores per row went into `src/graphics/dp_core.c`'s `fill_rec()` on `fillunr8` 8.99 /
9.17 against `fillunr` 10.40 / 10.43, which the landed leaf then matched at 8.76-8.80 across the
sweep.  Both were gated on `make test-ps2` (91/91) and `make arm64-elf` building, because the fill
leaf is shared with the Pi 400.

**What the flat row had really been pricing.**  `blit=` is not a copy; it is a scan *and* a copy.
`src/window/wnd.c:703-719` reads every pixel of the row to find the first transparent one -- a
transparent pixel must leave whatever is below it on the canvas -- and then the copy reads the band
again.  `blit scan` 39.86 against `copy even` 15.96 leaves 23.9 ns/px, 7.1 cycles, for the decision;
`blit wide` at 36.57, which makes the same decision two pixels per 64-bit load, says 3.3 ns/px of even
that is loop.  Production agrees with the bench from the other side: the `[BENCH] compositor:` row of
the measuring build read `blit=30755(613440 px)` = 50.2 ns/px, and the same row of this build reads
`blit=24426(613440 px)` = 39.8 -- the same pixel count, 20.6% fewer microseconds, no plane claimed and
no primitive added.  The two cross-checks are this build's own columns: `bg=7490(480000 px)` = 15.60
ns/px against the copy floor of 15.65, and `blit=24426` = 39.82 against `blit scan`'s 39.86.

**What it changed downstream, and where to look.**  `PS2.txt` gained section 10.1 and 1.5's ratio pair;
`PS2.md`'s price rows are now 4.6 / 2.6 / 11.8 cycles instead of one 14.8 row, and *The ratio that
decides where work goes* is a two-run pair with 29x retired as 23x -- it moved because the EE's loop
moved.  Handbook consequence 2 (the static background) was **opened** rather than closed by this: at
4.6 cycles a cached blit of the baked gradient is 6.0 ms against `f_bg`'s 28.5 ms, so the representation
was never the thing that made caching uninteresting -- the image is, because the XMB's own 768x500
client is 1,536,000 B against the ~1.5 MiB of headroom the link leaves under `ps2.ld`'s ASSERT.
Lane 9's Close and Gate cells in `PS2.txt` ch. 16 are stated as ns/px against 15.7 / 8.8, not against
50.

**What is NOT claimed.**  `blit wide` is a bench prototype: `src/window/wnd.c` still scans one pixel at
a time, so the 3.3 ns/px (2.0 ms of a full-canvas walk) is priced and *not taken*.  The floors are
PCSX2's -- an R5900 with real RDRAM may put them elsewhere, in which case the ladder's shape still
refutes a bandwidth argument and only its numbers move (task #17 remains the outstanding check).  And
the 96.4 / 21.2 / 16.9 / 16.1 ladder predates the landing, so it cannot be re-run from this image; the
rows that can be re-run are the ones quoted above, and `chk=` is the field that says whether they are
still measuring what their tag claims.

---
