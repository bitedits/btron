#!/usr/bin/env bash
#
# scripts/test_m68k.sh — Automated CI / Regression Test for B-System M68K Kernel
#
# Tests bare-metal Motorola 68040 Macintosh Quadra 800 execution on QEMU
# in headless CI mode, validating MacFB DAFB video, ADB, VIA1 and SCSI subsystems.
#
set -euo pipefail

QEMU_BIN="qemu-system-m68k"
if ! command -v "$QEMU_BIN" > /dev/null 2>&1; then
    echo "[ERROR] qemu-system-m68k not found in PATH."
    exit 1
fi

ELF_PATH=".build/btron-m68k.elf"
if [ ! -f "$ELF_PATH" ]; then
    echo "[CI-TEST] Building $ELF_PATH..."
    make m68k > /dev/null 2>&1
fi

LOG_FILE=$(mktemp /tmp/qemu_m68k_XXXXXX)
trap 'rm -f "$LOG_FILE"' EXIT

echo "[CI-TEST] Running QEMU ($QEMU_BIN) for $ELF_PATH on Macintosh Quadra 800 (q800)..."

# Launch QEMU in background writing to log file
("$QEMU_BIN" \
    -M q800 \
    -cpu m68040 \
    -m 128M \
    -kernel "$ELF_PATH" \
    -serial stdio \
    -display none > "$LOG_FILE" 2>&1) &
QEMU_PID=$!

# Allow boot sequence and desktop initialization (4 seconds is plenty for m68k)
sleep 4

# Gracefully terminate QEMU
kill -TERM "$QEMU_PID" 2>/dev/null || true
wait "$QEMU_PID" 2>/dev/null || true

echo "=========================================================="
echo " QEMU M68K Bare-Metal Boot Output (Macintosh Quadra 800):"
echo "=========================================================="
cat "$LOG_FILE"
echo "=========================================================="

# Verify all critical hardware & kernel subsystems initialized
MARKERS=(
    "B-System/BTRON3 3.20 (m68k-q800) Fumihiko Itagaki — uITRON 3.0"
    "Machine: Apple Macintosh Quadra 800  NuBus Slot 9 DAFB  ADB  VIA1/VIA2"
    "Cleanroom uITRON 3.0 / BTRON 3.20 Fumihiko Itagaki Engine (m68k-q800)"
    "Quadra 800 Physical Memory Map (128 MB RAM):"
    "NuBus Slot 9 DAFB VRAM"
    "NCR 53C96 ESP SCSI Storage Interface: INIT  [OK]"
    "Initializing NuBus Slot 9 MacFB / DAFB video adapter"
    "Framebuffer ready: 800x600 @ 8-bpp"
    "Initializing MOS 6522 VIA1 Timer & ADB Controller"
    "B-System Workbench Live on Macintosh Quadra 800!"
)

PASS=0
FAIL=0

for marker in "${MARKERS[@]}"; do
    if grep -qF "$marker" "$LOG_FILE"; then
        echo "  [PASS] Marker found: '$marker'"
        PASS=$((PASS + 1))
    else
        echo "  [FAIL] Missing marker: '$marker'"
        FAIL=$((FAIL + 1))
    fi
done

echo "----------------------------------------------------------"
echo " M68K Quadra 800 Test Results: $PASS Passed, $FAIL Failed"
echo "----------------------------------------------------------"

if [ "$FAIL" -gt 0 ]; then
    echo "[CI-TEST] FAILED — Missing expected boot markers."
    exit 1
fi

echo "[CI-TEST] SUCCESS — All $PASS markers verified!"
exit 0
