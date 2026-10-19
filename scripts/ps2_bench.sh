#!/usr/bin/env bash
# scripts/ps2_bench.sh — run the PS2 measurement build headless and print its table
#
# Copyright 2026 Synrc Research Center. MIT License.
#
# The bench lives inside the guest (src/cores/core_ps2.c, compiled only with
# -DBTRON_PS2_BENCH=1, which `make ps2-bench` does) and drives synthetic pointer
# reports through the USB driver's own entry point.  It therefore measures this
# port's present cost, band shape and pointer travel with no PCSX2 window, no
# keyboard, and no protocol for a hand to follow -- which is the point: the same
# numbers the [PSTAT] rows give a live session come out of a log file here.
#
# PCSX2 writes the emulated EE console to stdout under -nogui, and -nogui implies
# -batch, so the VM exits when it is shut down.  The guest cannot shut it down, so
# this waits for the "[BENCH] run complete" sentinel and then stops the emulator.
#
# A scratch copy of the datapath is used because PCSX2 rewrites inis/PCSX2.ini when
# it exits, and the repo's copy carries the [USB1] hidmouse binding, the [USB2]
# hidkbd binding and the game list that `make run-ps2` depends on.  A measurement
# run has no business editing the configuration the interactive run needs.
#
# BENCH_GS=1 scripts/ps2_bench.sh asks the second question this build can answer with
# no screen: it boots the census instead of the desktop, draws two solid SPRITE quads
# through the GS path and reads page 0 back (Local->Host), so whether the GS rasterises
# what the port sends is a [GSREAD] row rather than something to be looked at.  The
# census replaces the run rather than joining it -- inside a GUI session the desktop
# presents every frame and its uploads erase the blocks before the read sees them.
#
# BENCH_GS=6 takes the same census branch and adds LANE 21 rung 3a at its tail: one plate, uploaded
# into a PSMCT16 display page and read back three times -- at 32 bits, at 16 with the GS's own
# dither, at 16 without it.  Its verdict row says MEASURED only when the 16-bit page held the same
# picture in the same box at half the quadwords, which is the gate rung 4 builds its second page on.
#
# BENCH_APP=1 BENCH_TEX=1 [BENCH_ORDER=0|1] scripts/ps2_bench.sh is LANE 24's icon A/B, and it JOINS the
# run rather than replacing it: at the app phase's frame 3 it reads the live page and the desktop canvas
# over the same box, plus every registered texture plane, and prints a paired verdict row naming which of
# the three holds the flat rectangles.  ORDER=1 opens `lilcu` first, which is the sequence that made the
# glitch disappear for the operator; ORDER=0 is the control.
#
# It is now a BEFORE/AFTER pair over one event, because that is how the fault was last described: "Icons
# show inititially fine but get corrupted after FPS ticker changed".  The frame hook watches the XMB
# window's caption -- the readout in src/gl/egl_surface.c renames it through wnd_set_title(), which
# damages the tab rect and nothing else -- and takes the census again on the next pass and at +4 passes,
# diffing cell by cell and diffing the texture registry's VRAM spans.  Rows are `[GSBOX] frame N ...`.
#
# The dial that used to be needed is not: one XMB frame costs 75-110 ms of emulator wall (the 78 s figure
# was task #20's, and it was about a bake, not a frame), so the default BENCH_TIMEOUT of 300 s is ample
# and BENCH_APP_FRAMES=12 is enough to cross two FPS windows.  With either LANE 24 dial up the run skips
# the launcher-by-key cycle, which never returns (task #30) and would otherwise stall the run at its own
# budget; the census lives in the app phase that follows it.

set -euo pipefail
cd "$(dirname "$0")/.."

PCSX2_BIN="${PCSX2_BIN:-/Applications/PCSX2.app/Contents/MacOS/PCSX2}"
ELF="btron-ps2-bench.elf"
TIMEOUT="${BENCH_TIMEOUT:-300}"
OUT="build/ps2-bench.log"

if [ ! -x "$PCSX2_BIN" ]; then
    echo "[ERROR] No PCSX2 at $PCSX2_BIN -- pass PCSX2_BIN=<path>"
    exit 1
fi

make ps2-bench

LOG="$(mktemp)"
DATA="$(mktemp -d)"
trap 'rm -rf "$DATA" "$LOG"' EXIT
cp -Ra pcsx2/PCSX2 "$DATA/PCSX2"
mkdir -p build

echo "=========================================================="
echo " PS2 present & pointer bench -- $PCSX2_BIN -nogui"
echo "=========================================================="
"$PCSX2_BIN" -nogui -batch -fastboot -nofullscreen \
    -datapath "$DATA" "$PWD/$ELF" >"$LOG" 2>&1 &
VM=$!

elapsed=0
while kill -0 "$VM" 2>/dev/null; do
    sleep 2
    elapsed=$((elapsed + 2))
    if grep -q '\[BENCH\] run complete' "$LOG"; then
        break
    fi
    if [ "$elapsed" -ge "$TIMEOUT" ]; then
        echo "[ERROR] no sentinel after ${TIMEOUT}s; stopping the emulator"
        break
    fi
done

kill -TERM "$VM" 2>/dev/null || true
sleep 2
kill -KILL "$VM" 2>/dev/null || true
cp "$LOG" "$OUT"

if grep -q '\[BENCH\]' "$LOG"; then
    grep -E '\[BENCH\]|\[GSREAD\]|\[GSLIVE\]|\[GSTS\]|\[GSBOX\]|\[PW\]|\[PS2\] whole-canvas|\[PSTAT\]|\[MSX\]|\[FATAL\]' "$LOG"
    echo "----------------------------------------------------------"
    echo "[PS2-BENCH] full log: $OUT ($elapsed s)"
    exit 0
fi

echo "[ERROR] the log has no bench rows; last lines of the emulator output:"
tail -30 "$LOG"
exit 1
