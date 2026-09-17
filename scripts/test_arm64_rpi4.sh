#!/usr/bin/env bash
#
# Automated CI / Regression Test Runner for B-TRON on QEMU Raspberry Pi 4B (AArch64).
# Pure Bash / POSIX toolchain implementation.
#
set -euo pipefail

QEMU_BIN="qemu-system-aarch64"
if ! command -v "$QEMU_BIN" >/dev/null 2>&1; then
    echo "[ERROR] qemu-system-aarch64 not found in PATH."
    exit 1
fi

ELF_PATH="btron-aarch64-baremetal.elf"
if [ ! -f "$ELF_PATH" ]; then
    echo "[CI-TEST] Building $ELF_PATH..."
    make "$ELF_PATH" >/dev/null 2>&1 || make arm64-elf
fi

LOG_FILE=$(mktemp /tmp/qemu_arm64_rpi4_XXXXXX)
trap 'rm -f "$LOG_FILE"' EXIT

echo "[CI-TEST] Running QEMU ($QEMU_BIN) for $ELF_PATH on raspi4b..."

# Launch QEMU in background writing to log file
("$QEMU_BIN" \
    -M raspi4b \
    -m 2G \
    -device usb-kbd -device usb-mouse \
    -display none \
    -kernel "$ELF_PATH" \
    -serial stdio > "$LOG_FILE" 2>&1) &
QEMU_PID=$!

# Allow boot sequence and subsystem initialization (6 seconds is plenty)
sleep 6

# Gracefully terminate QEMU
kill -TERM "$QEMU_PID" 2>/dev/null || true
wait "$QEMU_PID" 2>/dev/null || true

echo "=========================================================="
echo " QEMU AArch64 Bare-Metal Boot Output (Raspberry Pi 4B):"
echo "=========================================================="
cat "$LOG_FILE"

# Verify all critical hardware & kernel subsystems initialized on Pi 4B
MARKERS=(
    "B-System/BTRON3 3.20 (aarch64-bcm2711) Takanori Yokoyama — T-Kernel 2.0"
    "Machine: Raspberry Pi 4B / BCM2711  AArch64 Cortex-A72  T-Kernel 2.0"
    "BCM2711 Physical Memory Map (Pi 4B, 2 GB / 4 GB RAM):"
    "Yokoyama T-Kernel 2.0 Engine (AArch64)"
    "ScreenDrv: OK"
    "KbPdDrv: OK"
    "LowKbPdDrv: OK"
    "USB Subsystem ready."
    "Stage 1: Terminal Console Active"
    "btron-pi400#"
)

for marker in "${MARKERS[@]}"; do
    if ! grep -Fq "$marker" "$LOG_FILE"; then
        echo ""
        echo "[FAIL] CI Test Failed: Missing expected output marker: '$marker'"
        exit 1
    fi
done

echo ""
echo "[PASS] All Bare-Metal Raspberry Pi 4B AArch64 Subsystems Verified Successfully!"
exit 0
