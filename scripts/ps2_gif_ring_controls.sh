#!/usr/bin/env bash
# scripts/ps2_gif_ring_controls.sh — LAW 16d positive controls for LANE 10's host gates
#
# Copyright 2026 Synrc Research Center. MIT License.
#
# LANE 10 (doc/txt/PS2.txt 11.6) makes seven assertions about the GIF writer ring: that
# channel 2 never gets two starts, that a full ring refuses and counts the refusal, that a
# drained descriptor comes off the head, that reap() empties the queue rather than taking one
# step, that a stats row is a profile of one window, that the capacity bounds the transfers the
# ring owns (queued + in flight) rather than the queue alone, and that the presentation yield
# reaps the previous frame.
#
# A gate that stays GREEN with the defect present proves nothing, so each of those seven is
# turned into a defect here: one line is changed in the source, the relevant gate is rebuilt
# from scratch and rerun, and the file is put back from a backup taken before the edit.  Every
# control must come back RED with at least one [FAIL] row.  The last block restores the tree and
# requires both gates GREEN again, which is also the check that the backups landed.
#
# Usage: scripts/ps2_gif_ring_controls.sh          (no arguments, no env vars)
#        The two gates it rebuilds are `make test-ps2-gif-ring` and `make test-eegs-present`;
#        object files are purged before each run, because a control edits a source file and a
#        gate that reused the previous .o would read GREEN for the wrong reason.
#
# This script edits sources under src/ for the length of one gate run and restores them
# afterwards.  Do not run it while another build is in flight, and run it from a clean tree so
# `git diff` can confirm the restore (see doc/txt/PS2.txt chapter 22's control law).

set -u

cd "$(dirname "$0")/.." || exit 1

RING=src/drivers/ps2/ps2_gif_ring.c
PRES=src/gl/eegs_present.c
BAK=/tmp/ps2_gif_ring_controls.$$.c
OUT=/tmp/ps2_gif_ring_controls.$$.out

cleanup() { rm -f "$BAK" "$OUT"; }
trap 'cp "$BAK" "$LAST_FILE" 2>/dev/null; cleanup' EXIT INT TERM

LAST_FILE=

run_gate() {          # $1 = gate make target, $2 = test object stem
    rm -f ".build/$2" "verify/tests/$2.test.o" >/dev/null 2>&1
    if make "$1" >"$OUT" 2>&1; then
        printf 'GREEN  '
    else
        printf 'RED    '
    fi
    grep -m1 'checks=' "$OUT" || sed -n '1,4p' "$OUT"
    printf '       failing rows: %s\n' "$(grep -c '\[FAIL\]' "$OUT")"
}

failed=0

control() {           # $1 file, $2 perl expr, $3 gate target, $4 object stem, $5 claim
    LAST_FILE="$1"
    cp "$1" "$BAK"
    perl -pi -e "$2" "$1"
    printf '%s\n' "$5"
    run_gate "$3" "$4"
    cp "$BAK" "$1"
    # A GREEN here means the suite never exercised the claim, so the book may not cite it.
    grep -q '\[FAIL\]' "$OUT" || { printf '       ^ CONTROL DID NOT FAIL: the gate is not an instrument\n'; failed=$((failed + 1)); }
    echo
}

# 1. the guard that keeps channel 2 from getting two starts at once.
control "$RING" 's/if \(ps2_gif_hw_busy\(\)\) break;/;/' test-ps2-gif-ring test_ps2_gif_ring \
    "CONTROL 1 drain() starts a second transfer while the first is still on the bus"

# 2. the refusal receipt.
control "$RING" 's/^\s+s_gif_st\.refused\+\+;/;/' test-ps2-gif-ring test_ps2_gif_ring \
    "CONTROL 2 a full ring loses the packet without counting it"

# 3. which cursor a drained descriptor comes from.
control "$RING" 's/gif_desc_t \*d = &s_gif_ring\[s_gif_head\];/gif_desc_t *d = \&s_gif_ring[s_gif_tail];/' \
    test-ps2-gif-ring test_ps2_gif_ring \
    "CONTROL 3 drain() takes from the tail, i.e. newest-first"

# 4. reap() reduced to one drain -- the reading of "non-blocking" the lane rejects.
control "$RING" 's/if \(s_gif_n == 0u && !s_gif_in_flight\) break;/break;/' \
    test-ps2-gif-ring test_ps2_gif_ring \
    "CONTROL 4 reap() drains once and calls itself done"

# 5. the profiling window stops being a window.  (`/pat/d` is not perl's delete -- the `d` is a
#    tr/// modifier and the line stays put, which is how this control read GREEN once.  Deleting
#    the line is what the control means, so it assigns the empty string.)
control "$RING" '$_ = "" if /s_gif_st\..*= 0u;/' test-ps2-gif-ring test_ps2_gif_ring \
    "CONTROL 5 stats_take() reports lifetime totals"

# 6. capacity counted on queued descriptors only, so the ring owns CAP+1 buffers.
control "$RING" 's/if \(s_gif_n \+ s_gif_in_flight >= PS2_GIF_RING_CAP\)/if (s_gif_n >= PS2_GIF_RING_CAP)/' \
    test-ps2-gif-ring test_ps2_gif_ring \
    "CONTROL 6 the cap bounds the queue, not the transfers the ring owns"

# 7. the yield stops reaping, so the recorder overwrites buffers channel 2 is reading.
control "$PRES" 's/^\s+\(void\)ps2_gif_reap\(\);/;/' test-eegs-present test_eegs_present \
    "CONTROL 7 eegs_present_begin() does not reap the previous frame"

echo "RESTORE  both gates must be GREEN on the restored tree"
run_gate test-ps2-gif-ring test_ps2_gif_ring
run_gate test-eegs-present test_eegs_present

if [ "$failed" -ne 0 ]; then
    echo
    echo "$failed control(s) left a gate GREEN -- see doc/txt/PS2.txt LAW 16d"
    exit 1
fi
echo
echo "all 7 controls failed their gate, and the restored tree passes both: PASS"
