#!/usr/bin/env bash
#
# Automated CI / Regression Test Runner for B-System x86_64 UEFI SMP (Kota Uchida Kernel).
# Runs headless in QEMU and validates VirtIO-GPU, ACPI 6.5, SMP, and VESA subsystems.
#
set -euo pipefail

QEMU_BIN="qemu-system-x86_64"
if ! command -v "$QEMU_BIN" >/dev/null 2>&1; then
    echo "[ERROR] qemu-system-x86_64 not found in PATH."
    exit 1
fi

ELF_PATH=".build/btron-uchida.elf"
if [ ! -f "$ELF_PATH" ]; then
    echo "[CI-TEST] Building $ELF_PATH..."
    make uefi >/dev/null 2>&1
fi

LOG_FILE=$(mktemp /tmp/qemu_uefi_XXXXXX)
trap 'rm -f "$LOG_FILE"' EXIT

echo "[CI-TEST] Running QEMU ($QEMU_BIN) for $ELF_PATH on q35 (VirtIO-GPU)..."

("$QEMU_BIN" \
    -M q35,accel=tcg -cpu qemu64 -smp cores=4,threads=1,sockets=1 -m 1G \
    -device virtio-vga \
    -display none \
    -kernel "$ELF_PATH" \
    -serial stdio > "$LOG_FILE" 2>&1) &
QEMU_PID=$!

sleep 3

kill -TERM "$QEMU_PID" 2>/dev/null || true
wait "$QEMU_PID" 2>/dev/null || true

echo "=========================================================="
echo " QEMU x86_64 UEFI SMP Bare-Metal Boot Output:"
echo "=========================================================="
cat "$LOG_FILE"

MARKERS=(
    "B-System/BTRON3 3.20 (x86_64-uefi-smp) Kota Uchida — T-Kernel 2.0"
    "Machine: QEMU q35  x86_64 EMT64  SMP  ACPI 6.5"
    "4 cores online: Round-Robin SMP dispatcher active"
    "VirtIO-Block"
    "VIRTIO-GPU"
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
echo "[PASS] All x86_64 UEFI SMP / VirtIO-GPU / VESA subsystems verified successfully!"
exit 0
