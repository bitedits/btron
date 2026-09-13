#!/usr/bin/env bash
#
# Automated CI / Regression Test Runner for B-TRON on QEMU Raspberry Pi 2B.
# Pure Bash / POSIX toolchain implementation.
#
set -euo pipefail

QEMU_BIN=""
if command -v qemu-system-arm >/dev/null 2>&1; then
    QEMU_BIN="qemu-system-arm"
elif command -v qemu-system-aarch64 >/dev/null 2>&1; then
    QEMU_BIN="qemu-system-aarch64"
else
    echo "[ERROR] Neither qemu-system-arm nor qemu-system-aarch64 found in PATH."
    exit 1
fi

ELF_PATH="btron-arm-baremetal.elf"
if [ ! -f "$ELF_PATH" ]; then
    echo "[CI-TEST] Building $ELF_PATH..."
    make "$ELF_PATH" >/dev/null 2>&1 || make arm-elf
fi

LOG_FILE=$(mktemp /tmp/qemu_btron_XXXXXX)
trap 'rm -f "$LOG_FILE"' EXIT

echo "[CI-TEST] Running QEMU ($QEMU_BIN) for $ELF_PATH..."

# Launch QEMU in background writing to log file
("$QEMU_BIN" \
    -M raspi2b \
    -m 1G \
    -usb -device usb-kbd -device usb-mouse \
    -display none \
    -kernel "$ELF_PATH" \
    -serial stdio > "$LOG_FILE" 2>&1) &
QEMU_PID=$!

# Allow boot sequence and subsystem initialization
# ARM QEMU emulation is slow for GIF icon decode during desktop init;
# 12 seconds is sufficient for a full boot + initial render pass.
sleep 12

# Gracefully terminate QEMU
kill -TERM "$QEMU_PID" 2>/dev/null || true
wait "$QEMU_PID" 2>/dev/null || true

echo "=========================================================="
echo " QEMU Bare-Metal Boot Output:"
echo "=========================================================="
cat "$LOG_FILE"

# Verify all critical hardware & kernel subsystems initialized
MARKERS=(
    "B-System/BTRON3 3.20 (armv7-bcm2836-yoko) Takahiro Yokobayashi — T-Kernel 2.0"
    "Sakamura T-Kernel 2.0 Real-Time OS Engine"
    "Initializing Video Display Framebuffer"
    "Initializing Sakamura T-Kernel 2.0 Subsystems"
    "ScreenDrv"
    "KbPdDrv"
    "LowKbPdDrv"
    "Initializing BCM283x Hardware Keyboard & Pointing Device (Mouse) Drivers..."
    "Live Multi-Window Desktop & Pointer initialized in Video VRAM"
    "Sakamura B-System 3.0 Interactive Keyboard & Mouse Active"
)

for marker in "${MARKERS[@]}"; do
    if ! grep -Fq "$marker" "$LOG_FILE"; then
        echo ""
        echo "[FAIL] CI Test Failed: Missing expected output marker: '$marker'"
        exit 1
    fi
done

echo ""
echo "[PASS] All Bare-Metal T-Kernel 2.0 / DWC2 USB / VRAM subsystems verified successfully!"
exit 0
