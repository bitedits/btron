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
    grep -E '\[BENCH\]|\[PS2\] whole-canvas|\[PSTAT\]|\[FATAL\]' "$LOG"
    echo "----------------------------------------------------------"
    echo "[PS2-BENCH] full log: $OUT ($elapsed s)"
    exit 0
fi

echo "[ERROR] the log has no bench rows; last lines of the emulator output:"
tail -30 "$LOG"
exit 1
