#!/usr/bin/env bash
#
# scripts/test_mips.sh — Automated CI / Regression Test for B-System MIPS Kernel
#
# Tests bare-metal MIPS Malta execution on QEMU in headless CI mode.
#
set -euo pipefail

QEMU_BIN=""
if command -v qemu-system-mipsel >/dev/null 2>&1; then
    QEMU_BIN="qemu-system-mipsel"
elif command -v qemu-system-mips64el >/dev/null 2>&1; then
    QEMU_BIN="qemu-system-mips64el"
else
    echo "[ERROR] Neither qemu-system-mipsel nor qemu-system-mips64el found in PATH."
    exit 1
fi

ELF_PATH="btron-mips.elf"
if [ ! -f "$ELF_PATH" ]; then
    echo "[CI-TEST] Building $ELF_PATH..."
    make mips >/dev/null 2>&1
fi

LOG_FILE=$(mktemp /tmp/qemu_btron_mips_XXXXXX)
trap 'rm -f "$LOG_FILE"' EXIT

echo "[CI-TEST] Running QEMU ($QEMU_BIN) for $ELF_PATH..."

# Launch QEMU in background writing to log file
("$QEMU_BIN" \
    -M malta \
    -cpu 24Kf \
    -m 256M \
    -kernel "$ELF_PATH" \
    -nographic \
    -monitor none > "$LOG_FILE" 2>&1) &
QEMU_PID=$!

# Allow boot sequence to output banner and enter interactive prompt
sleep 2

# Gracefully terminate QEMU
kill -TERM "$QEMU_PID" 2>/dev/null || true
kill -9 "$QEMU_PID" 2>/dev/null || true
wait "$QEMU_PID" 2>/dev/null || true

echo "=========================================================="
echo " QEMU MIPS Bare-Metal Boot Output:"
echo "=========================================================="
cat "$LOG_FILE"
echo "=========================================================="

# Verify critical markers
MARKERS=(
    "B-System / BTRON3 3.20 (Bare-Metal MIPS Architecture)"
    "Cleanroom TRON Kernel [Target 9: mips / QEMU Malta & Magnum]"
    "CPU: MIPS-III / MIPS64 (Little-Endian)"
    "Memory: 256 MB RAM (KSEG0 Mapped)"
    "Console: NS16550 UART @ 0x180003F8"
    "CP0 Status & Interrupts configured"
    "RTOS scheduler ready"
    "B-System Executive running"
)

PASS=0
FAIL=0

for m in "${MARKERS[@]}"; do
    if grep -qF "$m" "$LOG_FILE"; then
        echo "  [PASS] Marker found: '$m'"
        PASS=$((PASS + 1))
    else
        echo "  [FAIL] Missing marker: '$m'"
        FAIL=$((FAIL + 1))
    fi
done

echo "----------------------------------------------------------"
echo " MIPS Test Results: $PASS Passed, $FAIL Failed"
echo "----------------------------------------------------------"

if [ "$FAIL" -gt 0 ]; then
    echo "[CI-TEST] FAILED — Missing expected boot markers."
    exit 1
fi

echo "[CI-TEST] SUCCESS — All $PASS markers verified!"
exit 0
