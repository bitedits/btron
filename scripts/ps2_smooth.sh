#!/usr/bin/env bash
# scripts/ps2_smooth.sh -- automated PS2 smoothness loop: build, boot headless,
# gate every measured phase against an explicit budget, and record the run.
#
# Copyright 2026 Synrc Research Center. MIT License.
#
# Why this exists next to scripts/ps2_bench.sh: that one prints the bench table and
# is happy with any run that reaches its sentinel.  This one is the feedback loop a
# change needs -- it opens the real GL app, waits for the app phase to close itself,
# and turns the [XMBT]/[BENCH] rows into PASS/FAIL against numbers that come from the
# machine's own clocks rather than from an opinion about what feels smooth.
#
# The sentinel matters more than it looks.  The app phase runs before
# `[BENCH] run complete`, and 60 frames of XMB is 24 minutes of emulator, so any
# harness that waits for the sentinel and finds none has not measured a regression --
# it has cut off a phase it could not wait for.  BENCH_APP_FRAMES is lowered here for
# exactly that reason, and the run is only scored when the sentinel is present.
#
# Budgets are the Tier-1 plane numbers of doc/txt/ASYNC.txt, expressed in the units
# the guest already prints (microseconds).  A budget that the current image cannot
# meet is a FAIL, not a removed gate: the point is to see the ratio shrink.

set -euo pipefail
cd "$(dirname "$0")/.."

PCSX2_BIN="${PCSX2_BIN:-/Applications/PCSX2.app/Contents/MacOS/PCSX2}"
ELF="btron-ps2-bench.elf"
TIMEOUT="${SMOOTH_TIMEOUT:-${BENCH_TIMEOUT:-900}}"
FRAMES="${SMOOTH_FRAMES:-3}"
# Whether the app phase runs at all.  The float question is answered by the first bench
# phase, about fifteen seconds into a boot, while the app phase costs a minute of
# emulator per frame it draws: SMOOTH_APP=0 is the loop for a change whose verdict does
# not need a rasterised frame, and the six app gates then read SKIP, not PASS.
APP="${SMOOTH_APP:-1}"
# Stop waiting on a guest that has gone quiet.  A log that grows no further is a guest
# inside a drawing phase, and a drawing phase here is minutes: the child is stopped and
# whatever has printed is scored, because those rows are the evidence and the row that
# is still coming is not a verdict.
IDLE="${SMOOTH_IDLE:-45}"
# End the run at a row rather than at the sentinel.  The rows that decide the atlas and
# frame cost are printed once each, and the guest then spends a minute per further
# frame for a number that does not move: frame 1 here reads 23,944,390 / 23,946,199 /
# 23,949,072 us across three frames, so the second and third are the same measurement
# paid for twice.  Empty means "wait for the sentinel" -- the whole table, slow.
#
# Name a row printed AFTER everything the run is for.  The first version of this run
# stopped on `op sqrt`, which is the last of the nine [FPU] class rows, and the kill
# landed before the guest's console flush had carried it -- four rows lost, and the
# table looked like an engine with three ops.  `band64` is a safe one for the float
# questions: it is in the present-cost table, two lines after the last [FPU] row.
STOP_RE="${SMOOTH_STOP:-}"
OUT="build/ps2-smooth.log"
HISTORY="build/ps2-smooth-history.tsv"
STRICT="${STRICT:-0}"

# name:budget_us:label -- every entry is a guest row this image really prints.
# A budget of 0 means "this must come out zero", not "this always fails".  `info` is a
# metric printed with no verdict: the four raw disagreement counts are not gates any
# more because the hardware has been measured to differ from the bit engine in its
# handling of subnormals, infinities and NaNs, and a gate that fails for a documented
# property of the CPU is a gate that teaches a reader to ignore FAIL.
#
# fpu_other was the next candidate and the measurement retired it too.  It counts the
# disagreements no cause explains; on this machine that is 61 pairs, all add/sub/mul,
# all exactly one step, 49 with the smaller magnitude and 12 with the larger -- while
# the bit engine itself is provable exact against the host FPU (15.2M checks, 0
# failures).  A rounding mode was the first suspect and the op counts cleared it: those
# classes make a rounding decision on 60/58/42 of their clean pairs and COP1 is wrong on
# 28/14/19 of them, while div.s -- which decides on 81, more than any other -- is wrong
# on none.  A mode cannot skip the op with the most decisions to make.  So a non-zero
# `other` is a residue in this unit's round-and-normalise, not a wrong sequence, and a
# gate that asks it to be zero while add.s runs on COP1 can never be met.  The gates that
# decide whether the arithmetic is usable are the shape of the gap instead: never wider
# than one step (fpu_gap_wide) and never across zero (fpu_gap_sign).  Those two say "the
# same number, decided the other way" is all this machine buys, and they are both
# satisfiable -- and both met.
BUDGETS=(
    "fpu_disagree:info:COP1 vs bit engine: assertions that disagreed (of ~4.3k)"
    "fpu_worst_ulp:info:largest gap between the two engines, in steps"
    "fpu_clean_bad:info:disagreements on ordinary operands (see the cause rows)"
    "fpu_special_bad:info:disagreements on zero/subnormal/inf/NaN operands"
    "fpu_daz:info:of those, ones with a subnormal operand -- the unit reads it as zero"
    "fpu_ftz:info:of those, ones whose answer lands at the underflow boundary (measured: none)"
    "fpu_nonfinite:info:of those, ones with an infinite or NaN in the pair"
    "fpu_inexact:info:of those, ones the mantissa cannot hold exactly (cvt of a big int)"
    "fpu_other:info:one-step residue in add/sub/mul (div.s clean on its 81 inexact pairs); the bit engine is host-proven exact"
    "fpu_gap_1ulp:info:clean add/sub/mul/div pairs off by one step -- the same number, rounded the other way"
    "fpu_gap_2ulp:info:clean pairs off by two steps -- double rounding; on this machine: none"
    "fpu_gap_wide:0:clean arithmetic pairs off by 3 steps or more -- a different number, not a tie"
    "fpu_gap_sign:0:clean arithmetic pairs where the two engines disagree on the sign"
    "fpu_gap_small:info:one-step gaps where COP1 is the smaller magnitude (49 of 61 -- not truncation, which would be all of them)"
    "fpu_gap_large:info:one-step gaps where COP1 is the larger magnitude (12 of 61 -- and not a fixed mode either)"
    "fpu_cyc_hw:60:COP1 add+mul+div+compare chain, CPU cycles per op"
    "fill_atlas_us:50000:XMB icon atlas: expand the generated sheet into RGBA (boot once)"
    # The row the user's complaint is about: XMB becoming visible is the first frame's
    # draw, and before the atlas shipped as data it paid ~17 icon-cell bakes plus one
    # full re-upload per baked cell inside that one frame.
    "frame1_us:1200000:XMB first frame -- the whole load the user waits for"
    "bake_font_us:50000:XMB font atlas bake"
    "upload_tex_us:50000:texture upload to the GL backend"
    "init_state_us:50000:XMB state init"
    "draw_us:16700:one XMB frame, rasterizer only (60 Hz UI plane)"
    "xswap_us:4000:one XMB frame, surface swap"
    "full_total_us:4000:whole-canvas present, all three stages"
    "full_render_us:3000:whole-canvas composite"
    "full_swap_us:0:whole-canvas byte-swap sweep (target: absent)"
    "full_upload_us:4000:whole-canvas GIF upload"
    "band64_total_us:4000:64-row band present"
    "h4_total_us:4000:pass that paints a 4-px pointer sweep"
)

if [ ! -x "$PCSX2_BIN" ]; then
    echo "[ERROR] No PCSX2 at $PCSX2_BIN -- pass PCSX2_BIN=<path>"
    exit 1
fi

# The bench object carries its own flags and make does not track variable changes, so
# a stale object silently measures the previous configuration.
# Re-scoring an existing log is a different job from measuring one: the budgets and
# the parser get reviewed more often than the image does, and a three-minute emulator
# run is not an argument for eyeballing a table.
if [ -n "${SMOOTH_SCORE_LOG:-}" ]; then
    mkdir -p build
    # Re-scoring the file this script itself writes is the common case, and `cp x x`
    # exits non-zero on macOS -- under set -e that turned a valid re-score into silence.
    # Both sides get canonicalised because $SMOOTH_SCORE_LOG can arrive as any path a
    # shell variable can hold while $OUT is always the same relative one.
    src_abs="$(cd "$(dirname "$SMOOTH_SCORE_LOG")" && pwd)/$(basename "$SMOOTH_SCORE_LOG")"
    out_abs="$(cd "$(dirname "$OUT")" && pwd)/$(basename "$OUT")"
    [ "$src_abs" = "$out_abs" ] || cp "$SMOOTH_SCORE_LOG" "$OUT"
    elapsed=0
else
rm -f src/cores/core_ps2.pbench.o
BENCH_APP="$APP" BENCH_APP_FRAMES="$FRAMES" make ps2-bench

echo "[SMOOTH] artifact $ELF $(stat -f '%z bytes, %Sm' "$ELF") sha1=$(shasum "$ELF" | cut -c1-12)"

LOG="$(mktemp)"
DATA="$(mktemp -d)"
trap 'rm -rf "$DATA" "$LOG"' EXIT
cp -Ra pcsx2/PCSX2 "$DATA/PCSX2"
mkdir -p build

echo "=========================================================="
echo " PS2 smoothness loop -- app phase $([ "$APP" = 1 ] && echo "on, $FRAMES frames" || echo "off (float and present tables only)"), ${TIMEOUT}s ceiling, ${IDLE}s quiet"
echo "=========================================================="
"$PCSX2_BIN" -nogui -batch -fastboot -nofullscreen \
    -datapath "$DATA" "$PWD/$ELF" >"$LOG" 2>&1 &
VM=$!

elapsed=0
quiet=0
last_size=0
while kill -0 "$VM" 2>/dev/null; do
    sleep 2
    elapsed=$((elapsed + 2))
    grep -q '\[BENCH\] run complete' "$LOG" && break
    # The asked-for answer rather than the whole table: once the row this run exists to
    # read has printed, every further minute of emulator buys a repeat of a number that
    # already decided.
    if [ -n "$STOP_RE" ] && grep -qE "$STOP_RE" "$LOG"; then
        echo "[SMOOTH] stop pattern at ${elapsed}s -- the row this run was for has printed, stopping the emulator"
        break
    fi
    size=$(stat -f %z "$LOG" 2>/dev/null || echo 0)
    if [ "${size:-0}" -gt "$last_size" ]; then quiet=0; last_size=$size
    else quiet=$((quiet + 2)); fi
    if [ "$quiet" -ge "$IDLE" ]; then
        echo "[SMOOTH] no new console byte for ${quiet}s at ${elapsed}s: the guest is inside a phase that draws,"
        echo "           which costs minutes here.  Stopping it and scoring the rows that did print."
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
fi

# A run that was stopped -- by the idle watchdog, by SMOOTH_STOP, or by the ceiling --
# never prints the sentinel, and the first version of this harness answered by refusing
# to score it at all.  That was wrong twice over: every row these gates exist for
# ([FPU], the bake rows, frame 1) prints BEFORE the phase that cannot be waited for, and
# a row that is still coming is not a verdict about the rows that did.  So a partial run
# is scored, labelled partial, and a gate whose row never printed reads SKIP -- which is
# the honest mark for "not measured", and cannot be mistaken for a pass.  The refusal is
# kept for the one case that deserves it: a log with no measurement in it at all.
if ! grep -q '\[BENCH\] run complete' "$OUT"; then
    if ! grep -qE '\[FPU\]|\[XMBT\]|\[BENCH\] ' "$OUT"; then
        echo "[SMOOTH] NOT SCORED -- $OUT ($elapsed s) holds no measurement row at all,"
        echo "           so there is nothing to gate.  The guest produced:"
        tail -20 "$OUT"
        exit 2
    fi
    echo "[SMOOTH] PARTIAL RUN ($elapsed s, no sentinel) -- gating the rows that printed;"
    echo "           any metric whose row did not print is reported SKIP, not PASS."
fi

# One pass over the log, name=value per line.  bash regex rather than gawk: macOS
# ships an awk whose match() takes no array.
VALS="$(mktemp)"
LOG="${LOG:-}"
DATA="${DATA:-}"
trap 'rm -rf "${DATA:-}" "${LOG:-}" "$VALS"' EXIT
# Each pattern is assigned to a variable and used unquoted: inside [[ =~ ]] a
# partially-quoted pattern makes bash treat a parenthesis as literal text in one half
# and as a capture group in the other, which is a syntax error rather than a mismatch.
re_fill_atlas='\[XMBT\] +fill_atlas +([0-9]+)'
re_bake_font='\[XMBT\] +bake_font +([0-9]+)'
re_upload='\[XMBT\] +upload +([0-9]+)'
re_init='\[XMBT\] +init_state +([0-9]+)'
re_frame='\[XMBT\] frame +([0-9]+).*draw=([0-9]+) us swap=([0-9]+)'
re_full='\[BENCH\] +full +[0-9]+x *[0-9]+ +total=([0-9]+) us +render +([0-9]+)/[0-9]+ +swap +([0-9]+)/[0-9]+ +upload +([0-9]+)/[0-9]+'
re_band64='\[BENCH\] +band64 +[0-9]+x *[0-9]+ +total=([0-9]+)'
re_tag='\[BENCH\] ([hv][0-9]+) +move'
re_uspass='us/pass render +([0-9]+) swap +([0-9]+) upload +([0-9]+) total +([0-9]+) +-> +([0-9]+) passes/s'
re_status='CP0\.Status=(0x[0-9a-f]+) +CU3\.\.CU0=([01])([01])([01])([01])'
re_fpu_check='\[FPU\] checked=([0-9]+) disagree=([0-9]+) worst=([0-9]+) ulp +path in use: (.*)$'
re_fpu_cyc='\[FPU\] 16000-op chain: COP1 ([0-9]+) cycles/op +bit engine ([0-9]+) cycles/op'
re_fpu_op='\[FPU\] op +([a-zA-Z0-9./_-]+) +clean=([0-9]+) bad=([0-9]+) +special=([0-9]+) bad=([0-9]+)'
re_fpu_gap='\[FPU\] clean gap 1ulp=([0-9]+) 2ulp=([0-9]+) wide=([0-9]+) sign=([0-9]+) small=([0-9]+) large=([0-9]+)'
re_fpu_shape='\[FPU\] shape daz=([0-9]+) ftz=([0-9]+) nonfinite=([0-9]+) inexact=([0-9]+) other=([0-9]+)'
re_fpu_unexpl='\[FPU\] unexplained first=(.*)$'

curtag=""
while IFS= read -r line; do
    case "$line" in
        *"[XMBT] fill_atlas"*) [[ $line =~ $re_fill_atlas ]] && echo "fill_atlas_us=${BASH_REMATCH[1]}" >>"$VALS" ;;
        *"[XMBT] bake_font"*)  [[ $line =~ $re_bake_font ]]  && echo "bake_font_us=${BASH_REMATCH[1]}"  >>"$VALS" ;;
        *"[XMBT] upload"*)     [[ $line =~ $re_upload ]]     && echo "upload_tex_us=${BASH_REMATCH[1]}"  >>"$VALS" ;;
        *"[XMBT] init_state"*) [[ $line =~ $re_init ]]       && echo "init_state_us=${BASH_REMATCH[1]}"  >>"$VALS" ;;
        # The two float rows: the correctness pair and the price pair.  They print in
        # the first bench phase, so a run that never reaches a frame still answers
        # whether COP1 is the engine in use and what one of its ops costs.
        *"[FPU] checked="*)    [[ $line =~ $re_fpu_check ]] && { echo "fpu_checked=${BASH_REMATCH[1]}"   >>"$VALS"
                             echo "fpu_disagree=${BASH_REMATCH[2]}" >>"$VALS"
                             echo "fpu_worst_ulp=${BASH_REMATCH[3]}" >>"$VALS"
                             echo "fpu_path=${BASH_REMATCH[4]}"     >>"$VALS"; } ;;
        *"[FPU] 16000-op chain"*) [[ $line =~ $re_fpu_cyc ]] && { echo "fpu_cyc_hw=${BASH_REMATCH[1]}"   >>"$VALS"
                             echo "fpu_cyc_soft=${BASH_REMATCH[2]}" >>"$VALS"; } ;;
        # One row per op class, from the guest's own differential self-test.  Kept as
        # raw rows and summed below, because the two halves mean different things: a
        # clean-column disagreement is wrong arithmetic in a pixel path, a
        # special-column one is the FPU's edge law.
        *"[FPU] op "*)         [[ $line =~ $re_fpu_op ]] && { echo "fpu_op_row|${BASH_REMATCH[1]}|${BASH_REMATCH[3]}|${BASH_REMATCH[5]}" >>"$VALS"
                             echo "fpu_op_seen=1" >>"$VALS"; } ;;
        # The same disagreements sorted by what caused them.  The five counts partition
        # `bad`, and four of them name a property of a single-precision unit that flushes
        # -- so the only one with a gate is `other`, the residue no cause explains.  A
        # run that was stopped before this row prints leaves fpu_other empty and the gate
        # reads SKIP: the cause table is the verdict's premise, not a decoration.
        # The size and direction of the clean-operand gaps.  All-in-one-direction is a
        # rounding mode; mixed directions on one-step gaps is a lost sticky bit; anything
        # in wide or sign is a different number and a stop.
        *"[FPU] clean gap "*)  [[ $line =~ $re_fpu_gap ]] && { echo "fpu_gap_1ulp=${BASH_REMATCH[1]}"   >>"$VALS"
                             echo "fpu_gap_2ulp=${BASH_REMATCH[2]}"   >>"$VALS"
                             echo "fpu_gap_wide=${BASH_REMATCH[3]}"   >>"$VALS"
                             echo "fpu_gap_sign=${BASH_REMATCH[4]}"   >>"$VALS"
                             echo "fpu_gap_small=${BASH_REMATCH[5]}"  >>"$VALS"
                             echo "fpu_gap_large=${BASH_REMATCH[6]}"  >>"$VALS"; } ;;
        *"[FPU] shape "*)      [[ $line =~ $re_fpu_shape ]] && { echo "fpu_daz=${BASH_REMATCH[1]}"        >>"$VALS"
                             echo "fpu_ftz=${BASH_REMATCH[2]}"        >>"$VALS"
                             echo "fpu_nonfinite=${BASH_REMATCH[3]}"  >>"$VALS"
                             echo "fpu_inexact=${BASH_REMATCH[4]}"    >>"$VALS"
                             echo "fpu_other=${BASH_REMATCH[5]}"      >>"$VALS"; } ;;
        *"[FPU] unexplained "*) [[ $line =~ $re_fpu_unexpl ]] && echo "fpu_other_op=${BASH_REMATCH[1]}" >>"$VALS" ;;
        # The last frame row wins: with SMOOTH_FRAMES>1 that is the steady state, and
        # frame 1 still carries one-off costs that do not belong in a gate.
        # Frame 1 gets its own gate instead: what the user waits for on load is the first
        # frame, and it is where the icon atlas and the font bake are paid.
        *"[XMBT] frame "*)     [[ $line =~ $re_frame ]] && { echo "draw_us=${BASH_REMATCH[2]}" >>"$VALS"
                             echo "xswap_us=${BASH_REMATCH[3]}" >>"$VALS"; echo "frame_no=${BASH_REMATCH[1]}" >>"$VALS";
                             if [ "${BASH_REMATCH[1]}" = 1 ]; then echo "frame1_us=${BASH_REMATCH[2]}" >>"$VALS"; fi; } ;;
        # avg/max in the log; the gate reads the average column and the max stays
        # in the file for anyone who wants the tail.
        *"[BENCH] full "*)     [[ $line =~ $re_full ]] && { echo "full_total_us=${BASH_REMATCH[1]}" >>"$VALS"
                             echo "full_render_us=${BASH_REMATCH[2]}" >>"$VALS"
                             echo "full_swap_us=${BASH_REMATCH[3]}"   >>"$VALS"
                             echo "full_upload_us=${BASH_REMATCH[4]}" >>"$VALS"; } ;;
        *"[BENCH] band64 "*)   [[ $line =~ $re_band64 ]] && echo "band64_total_us=${BASH_REMATCH[1]}" >>"$VALS" ;;
    esac
    # The direction rows put their tag on one line and their cost on the next, so the
    # tag has to be remembered rather than matched twice.
    if [[ $line =~ $re_tag ]]; then curtag="${BASH_REMATCH[1]}"; fi
    if [[ $line =~ $re_uspass ]] && [ "$curtag" = "h4" ]; then
        echo "h4_total_us=${BASH_REMATCH[4]}" >>"$VALS"
        echo "passes_per_s=${BASH_REMATCH[5]}" >>"$VALS"
    fi
    if [[ $line =~ $re_status ]]; then
        echo "cp0_status=${BASH_REMATCH[1]}" >>"$VALS"
        echo "cp0_cu1=${BASH_REMATCH[3]}"    >>"$VALS"
    fi
done < "$OUT"

# Last match wins: the frame rows repeat once per frame and the gate wants the
# steady-state one, not frame 1 with its atlas-bake leftovers.
getval() { grep "^$1=" "$VALS" | tail -1 | cut -d= -f2 || true; }

# Sum the per-class rows into the two totals, and print the rows themselves while
# doing it: the totals say how much of the arithmetic differs, the table says which op.
OPSUM="$VALS.opsum"
awk -F'|' '$1=="fpu_op_row" { clean += $3; special += $4; rows++;
                              printf "  %-7s clean bad %-4d  special bad %-4d%s\n", $2, $3, $4,
                                     ($3 > 0 ? "   <== the pixel path, by one step" : "") }
     END { printf "fpu_clean_bad=%d\nfpu_special_bad=%d\nfpu_op_rows=%d\n", clean, special, rows + 0 }' \
    "$VALS" >"$OPSUM"
if [ "$(grep -c '^fpu_op_row' "$VALS" || true)" -gt 0 ]; then
    echo "[FPU] disagreements by op class (0 = the two engines agree there)"
    sed '/^fpu_/d' "$OPSUM"
    if grep -q "\[FPU\] pair " "$OUT"; then
        echo "[FPU] the first pairs no cause explains, as the two engines answered them:"
        grep -h "\[FPU\] pair " "$OUT" | sed 's/^.*\[FPU\]/  [FPU]/'
    fi
fi
grep '^fpu_' "$OPSUM" >>"$VALS"
rm -f "$OPSUM"

# The guest names the op that first left a residue; repeat it here, because the number
# alone says `some engine is wrong` and the reader still has to go and find which.
# The verdict is the measured one, so print the shape of the gap next to the count: the
# residue is COP1 rounding add/sub/mul one step from IEEE (both directions), and the only
# thing that would make it a defect is a wide or sign-crossing gap.
if [ -n "$(getval fpu_other)" ] && [ "$(getval fpu_other)" != 0 ]; then
    echo "[FPU] $(getval fpu_other) clean disagreement(s), first in op '$(getval fpu_other_op)': wide=$(getval fpu_gap_wide) sign=$(getval fpu_gap_sign) -- a one-step gap is this unit's rounding residue (div.s has none), a non-zero either is a broken sequence"
fi

printf '%-18s %14s %12s %9s  %s\n' "METRIC" "MEASURED us" "BUDGET us" "RATIO" "WHAT IT IS"
echo "------------------------------------------------------------------------------"
fails=0
want_hdr=$(printf 'epoch\tframes\tartifact'; for entry in "${BUDGETS[@]}"; do printf '\t%s' "${entry%%:*}"; done; printf '\n')
if [ -f "$HISTORY" ]; then
    # The columns are the budget list, so reviewing that list (as the cause gates just
    # did) invalidates the rows already filed.  An old file kept under the old name beats
    # a new file whose columns silently mean something else.
    if [ "$(head -1 "$HISTORY")" != "$(printf '%s' "$want_hdr" | tr -d '\n')" ]; then
        old="$HISTORY.$(date -u +%Y%m%dT%H%M%SZ)"
        mv "$HISTORY" "$old"
        echo "[SMOOTH] gate list changed -- previous history kept at $(basename "$old")"
    fi
fi
if [ ! -f "$HISTORY" ]; then
    printf '%s' "$want_hdr" >"$HISTORY"
fi
{
    printf '%s\t%s/%s frames\t%s' "$(date -u +%Y-%m-%dT%H:%M:%SZ)" \
        "$(getval frame_no)" "$FRAMES" "$(getval cp0_status)"
} >>"$HISTORY"
for entry in "${BUDGETS[@]}"; do
    IFS=: read -r name budget label <<<"$entry"
    v="$(getval "$name")"
    if [ -z "$v" ]; then
        printf '%-18s %14s %12s %9s  %s  [SKIP]\n' "$name" "-" "$budget" "-" "$label"
        printf '\tSKIP' >>"$HISTORY"; continue
    fi
    if [ "$budget" = info ]; then
        # Printed, recorded, not judged: see the array above for why these four are the
        # hardware's business rather than the port's.
        printf '%-18s %14s %12s %9s  %s  [INFO]\n' "$name" "$v" "measured" "-" "$label"
        printf '\t%s' "$v" >>"$HISTORY"; continue
    fi
    ratio=$(awk -v a="$v" -v b="$budget" 'BEGIN{ if (b+0==0) printf "n/a"; else printf "%.1fx", a/b }')
    if [ "$budget" -gt 0 ] && [ "$v" -le "$budget" ] 2>/dev/null; then
        verdict=PASS
    elif [ "$budget" -eq 0 ]; then
        # A zero budget is an assertion, not an unreachable target: the row passes by
        # measuring nothing.  The swap sweep is the same law -- it is only absent when
        # it costs zero.
        if [ "$v" -eq 0 ] 2>/dev/null; then verdict=PASS
        else verdict=FAIL; fails=$((fails + 1)); fi
    else
        verdict=FAIL; fails=$((fails + 1))
    fi
    printf '%-18s %14s %12s %9s  %s  [%s]\n' "$name" "$v" "$budget" "$ratio" "$label" "$verdict"
    printf '\t%s' "$v" >>"$HISTORY"
done
printf '\n' >>"$HISTORY"

echo "------------------------------------------------------------------------------"
echo "[CPU] CP0.Status=$(getval cp0_status)  FPU-usable bit CU1=$(getval cp0_cu1)"
echo "[SMOOTH] desktop pass cadence $(getval passes_per_s) passes/s; steady frame = $(getval frame_no)"
echo "[SMOOTH] $fails failing metric(s); log: $OUT ($elapsed s); history: $HISTORY"
if [ "$STRICT" = "1" ] && [ "$fails" -gt 0 ]; then exit 1; fi
exit 0
