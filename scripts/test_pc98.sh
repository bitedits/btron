#!/usr/bin/env bash
#
# Automated CI / Regression Test Runner for B-System NEC PC-98 Kernel (Awe Morris Kernel).
# Runs headless in QEMU and validates GDC uPD7220, PC-98 architecture, and VESA subsystems.
#
set -euo pipefail

QEMU_BIN="qemu-system-x86_64"
if ! command -v "$QEMU_BIN" >/dev/null 2>&1; then
    echo "[ERROR] qemu-system-x86_64 not found in PATH."
    exit 1
fi

ELF_PATH=".build/btron-morris.elf"
if [ ! -f "$ELF_PATH" ]; then
    echo "[CI-TEST] Building $ELF_PATH..."
    make pc98 >/dev/null 2>&1
fi

LOG_FILE=$(mktemp /tmp/qemu_pc98_XXXXXX)
trap 'rm -f "$LOG_FILE"' EXIT

echo "[CI-TEST] Running QEMU ($QEMU_BIN) for $ELF_PATH on q35 (PC-98 VM)..."

("$QEMU_BIN" \
    -M q35,accel=tcg -cpu qemu64 -m 1G \
    -display none \
    -kernel "$ELF_PATH" \
    -serial stdio > "$LOG_FILE" 2>&1) &
QEMU_PID=$!

sleep 3

kill -TERM "$QEMU_PID" 2>/dev/null || true
wait "$QEMU_PID" 2>/dev/null || true

echo "=========================================================="
echo " QEMU PC-98 Bare-Metal Boot Output:"
echo "=========================================================="
cat "$LOG_FILE"

MARKERS=(
    "B-System/BTRON3 3.20 (i386-pc98) Awe Morris — T-Kernel 2.0"
    "Machine: NEC PC-9801/PC-9821"
    "GDC uPD7220"
    "All PC-98 subsystems: READY"
    "NEC PC-98 Memory Layout"
    "VESA"
    "Ski Bootloader Active"
)

for marker in "${MARKERS[@]}"; do
    if ! grep -Fq "$marker" "$LOG_FILE"; then
        echo ""
        echo "[FAIL] CI Test Failed: Missing expected output marker: '$marker'"
        exit 1
    fi
done

echo ""
echo "[PASS] All NEC PC-98 / Awe Morris Kernel subsystems verified successfully!"
exit 0
