#!/usr/bin/env bash
# scripts/ps2_eegs_repro.sh -- one pinned, gated, repeatable measurement of the eegs backend
#
# Copyright 2026 Synrc Research Center. MIT License.
#
# Why this exists as a file rather than a command line: on 2026-10-17 the census said the XMB's
# client rect was painted (in_win=383,951 of 384,000) and the operator said the window was empty,
# and both were true of the same image.  The disagreement was only answerable once the run could be
# named -- same dials, same ELF bytes, same launch route -- so that a row quoted twice means the
# same measurement twice.  This script therefore does three things the bare bench call does not:
#
#   1. pins and records the environment (make resolves PS2_MSX from PS2_GL, so the file's default
#      is not the value in force; it is read back from `make ps2-dials`, and the ELF's hash, size
#      and mtime go in the manifest beside it, plus a dirty-tree fingerprint because this port is
#      worked on with uncommitted edits);
#   2. runs the route the user takes -- `xmb` typed at the Terminal, which the bench's g_term verb
#      phase already drives through shell_execute_cmd() -- with the live-page census on top of it
#      (BENCH_APP=1 BENCH_GS=2), twice, so a number that moves between runs is visible as moving;
#   3. grades the deciding rows instead of dumping them, and says SKIP for any assertion the image
#      did not actually exercise.
#
# Exit status is 0 only when every graded row passed.  A FAIL here is a measurement that contradicts
# the claim it grades, not (necessarily) a regression: the survival rung FAILs while the present order
# is being worked on, and that is its job.
#
# Usage:
#   scripts/ps2_eegs_repro.sh                 # eegs, MSX parked, two identical runs
#   RUNS=3 scripts/ps2_eegs_repro.sh          # three, to see the spread
#   PS2_MSX=1 scripts/ps2_eegs_repro.sh       # the falsifying control: OOM rows are the answer

set -euo pipefail
cd "$(dirname "$0")/.."

RUNS="${RUNS:-2}"
BENCH_TIMEOUT="${BENCH_TIMEOUT:-460}"
# REGRADE=<an existing build/repro/... directory> re-grades the logs already sitting in it: no build, no
# emulator.  The grading block below is a program over a log, so a change to it has to be testable against
# a saved measurement before it is tested against a new one -- and a mutation control only needs the rows,
# which is why this mode exists.  The manifest in that directory still names the image the rows came from.
OUTDIR="${REGRADE:-build/repro/$(date -u +%Y%m%dT%H%M%SZ)}"
REGRADED=0
if [ -n "${REGRADE:-}" ]; then REGRADED=1; fi
mkdir -p "$OUTDIR"

PCSX2_BIN="${PCSX2_BIN:-/Applications/PCSX2.app/Contents/MacOS/PCSX2}"
MANIFEST="$OUTDIR/manifest.txt"

# Pin the dial this environment is named for, rather than trusting the Makefile default: today
# `PS2_GL ?= eegs`, and `PS2_MSX` is derived from it (0 under eegs), which is exactly the operator's
# `make run-ps2 PS2_GL=eegs PS2_MSX=0`.  PS2_MSX is deliberately NOT exported: passing it would change
# its `origin` and skip the derivation, so the control run has to name it on the command line.
# If PS2_GL's default ever moves, this script keeps measuring the eegs environment and the manifest
# says so.
export PS2_GL="${PS2_GL:-eegs}"

# ── 1. what the build resolved to ────────────────────────────────────────────────────────────
# `make ps2-dials` rather than `grep PS2_MSX Makefile`: the value is derived, and a derived default
# read out of the file is a claim about the file, not about the image.  The bench dials are passed in
# here as well, so the manifest records the same values the bench object is about to be compiled with.
if [ "$REGRADED" = "1" ]; then
    [ -s "$OUTDIR/dials.txt" ] || { echo "REGRADE: $OUTDIR carries no dials.txt -- nothing pins MSX for these rows" >&2; exit 2; }
else
    BENCH_APP="${BENCH_APP:-1}" BENCH_GS="${BENCH_GS:-2}" make -s ps2-dials >"$OUTDIR/dials.txt" 2>&1
fi
GL=$(awk -F= '/^PS2_GL=/{print $2}' "$OUTDIR/dials.txt")
MSX=$(awk -F= '/^PS2_MSX=/{print $2}' "$OUTDIR/dials.txt")

if [ "$REGRADED" = "0" ]; then
GIT_REV=$(git rev-parse --short HEAD 2>/dev/null || echo unknown)
# The tree is dirty by design here, so the fingerprint is of the tracked contents, not of HEAD: two
# runs with different source cannot be compared, and this is the only cheap way to know.
GIT_TREE=$(git status --porcelain 2>/dev/null | shasum -a 1 | cut -c1-12)

make ps2 >"$OUTDIR/build.log" 2>&1
ELF=btron-ps2.elf
ELF_SHA=$(shasum -a 1 "$ELF" | cut -c1-40)
# The raw hash is not an identity: the image embeds __DATE__ " " __TIME__ twice (src/cores/core_ps2.c
# 5160 and 9598), so two builds of the SAME source differ -- measured 2026-10-17: relinking twice with
# nothing touched moved exactly 3 bytes, all of them inside " 2026 23:08:38" vs " 2026 23:12:58".
# Masking HH:MM:SS collapses those two builds to one hash, so the field below can tell "the source
# changed" from "the clock changed" -- which is the difference between a rerun and a new measurement.
ELF_SHA_NOCLOCK=$(perl -0777 -pe 's/\d{2}:\d{2}:\d{2}/00:00:00/g' "$ELF" | shasum -a 1 | cut -c1-40)
ELF_SIZE=$(stat -f '%z' "$ELF")
ELF_MTIME=$(stat -f '%Sm' "$ELF")
# The emulator is identified by its bytes, not by asking it: PCSX2 has no --version that does not
# open a window, and a measurement script that pops a GUI is not a measurement script.
PCSX2_ID="$(stat -f '%z B  mtime=%Sm' "$PCSX2_BIN" 2>/dev/null || echo missing)  $PCSX2_BIN"

{
    echo "eegs repro -- $(date -u +%FT%TZ) UTC"
    echo "git          : $GIT_REV  tree=$GIT_TREE"
    echo "ps2-dials    : $(tr '\n' ' ' <"$OUTDIR/dials.txt")"
    echo "elf          : $ELF  $ELF_SIZE B  sha1=$ELF_SHA  sha1-noclock=$ELF_SHA_NOCLOCK  mtime=$ELF_MTIME"
    echo "pcsx2        : $PCSX2_ID"
    echo "runs         : $RUNS  BENCH_APP=1 BENCH_GS=2 BENCH_TIMEOUT=$BENCH_TIMEOUT"
} >"$MANIFEST"

# ── 2. the runs ──────────────────────────────────────────────────────────────────────────────
for i in $(seq 1 "$RUNS"); do
    echo "=== run $i of $RUNS: PS2_GL=$GL PS2_MSX=$MSX, \`xmb\` typed at the Terminal ==="
    BENCH_APP=1 BENCH_GS=2 BENCH_TIMEOUT="$BENCH_TIMEOUT" scripts/ps2_bench.sh \
        >"$OUTDIR/run$i.stdout" 2>&1 && rc=0 || rc=$?
    cp build/ps2-bench.log "$OUTDIR/run$i.log"
    echo "run $i: bench exit $rc, log $OUTDIR/run$i.log"
done
fi
ELF=btron-ps2.elf

# ── 3. the grades ────────────────────────────────────────────────────────────────────────────
LOG="$OUTDIR/run${RUNS}.log"
if [ ! -f "$LOG" ]; then
    # A regrade directory may hold fewer runs than RUNS defaults to; take the highest one it has.
    LOG=$(ls -1 "$OUTDIR"/run*.log 2>/dev/null | tail -1 || true)
    [ -n "$LOG" ] || { echo "no run*.log in $OUTDIR -- nothing to grade" >&2; exit 2; }
    RUNS=$(basename "$LOG" .log | tr -dc '0-9')
fi
fails=0

grade() {  # grade <name> <PASS|FAIL|SKIP> <deciding row, quoted verbatim>
    printf '  [%-4s] %s\n        %s\n' "$2" "$1" "$3"
    [ "$2" = "FAIL" ] && fails=$((fails + 1))
    return 0
}

sentinel=$(grep -c '\[BENCH\] run complete' "$LOG" || true)
grade "sentinel" "$([ "$sentinel" -ge 1 ] && echo PASS || echo FAIL)" \
      "$(grep -m1 '\[BENCH\] run complete' "$LOG" || echo 'no "[BENCH] run complete" -- the run is truncated, every grade below is unread')"

oom=$(grep -c 'out of memory asking' "$LOG" || true)
atlas=$(grep -c 'no memory for the icon atlas' "$LOG" || true)
zb=$(grep -c 'ZB_open failed' "$LOG" || true)
ask=$(grep -E '\[HEAP\] \+(1500|750|576) kB' "$LOG" | tail -3 | sed -E 's/^\[[^]]*\] //' | tr '\n' '|' || true)
# Every distinct failed ask, named with the row that follows it, because "an OOM happened" is not an
# answer -- on the 2026-10-17 pinned run the three planes a window needs are all served and a FOURTH
# ask (the 1 MB texture sheet the atlas is uploaded from) still fails, and the log only says which one
# by the stage row printed immediately after it.
oomrows=$(grep -A1 'out of memory asking' "$LOG" | sed -E 's/^\[[^]]*\] //' | tr '\n' ' ' || true)
if [ "$MSX" = "1" ]; then
    # The control's answer IS the OOM: it grades as PASS only because the claim being graded is
    # "MSX-on starves the XMB planes", and the rows below are that claim measured.
    grade "heap (control: MSX-on starves the planes)" "$([ "$atlas" -ge 1 ] && echo PASS || echo FAIL)" \
          "atlas=$atlas zb=$zb -- $ask"
else
    grade "the three planes a GL window asks for" \
          "$([ "$atlas" -eq 0 ] && [ "$zb" -eq 0 ] && echo PASS || echo FAIL)" \
          "asks served: $ask  (atlas rows=$atlas, ZB rows=$zb)"
    grade "no OTHER allocation fails while the window is up" "$([ "$oom" -eq 0 ] && echo PASS || echo FAIL)" \
          "${oomrows:-no 'out of memory' row: every ask the image makes was served}"
fi

planes=$(grep -m1 '\[BENCH\] gs planes' "$LOG" || true)
reg=$(echo "$planes" | grep -oE 'registered=[0-9]+' | cut -d= -f2 || true)
pend=$(echo "$planes" | grep -oE 'pending=[0-9]+' | cut -d= -f2 || true)
# `pending=0` alone was the original grade, and it passed on an image that had registered nothing:
# a plane table with zero entries has zero pending entries.  The two shipped sheets -- the 512x512
# icon atlas and the font sheet -- are the floor, because a texture that never registered is the
# picture the operator sees as flat blocks where the icons belong.
grade "both shipped sheets reached the GS" \
      "$([ "${reg:-0}" -ge 2 ] && [ "${pend:-1}" = "0" ] && echo PASS || echo FAIL)" \
      "${planes:-no 'gs planes' row in this build}  (need registered>=2, pending=0)"

# The content rung, and the one that was missing on 2026-10-17: every grade above counts pixels in
# the client rect, and the operator's screen disagreed with all of them.  A textured primitive
# whose plane has no VRAM address is demoted and drawn FLAT -- ink in the rectangle, wrong ink --
# so `texmiss=` is the column that separates "the window painted" from "the window painted its
# icons".  `plate=none` is its twin: a frame that recorded no vertex group at all.
texmiss=$(grep -oE 'texmiss=[0-9]+' "$LOG" | cut -d= -f2 | sort -n | tail -1 || true)
gsts=$(grep -c '\[GSTS\] groups=' "$LOG" || true)
grade "no textured primitive was demoted to flat ([GSTS] texmiss=)" \
      "$([ -n "$texmiss" ] && [ "$texmiss" -eq 0 ] && echo PASS || echo FAIL)" \
      "worst texmiss=${texmiss:-?} over $gsts [GSTS] frame rows"

plate_none=$(grep -c 'plate=none' "$LOG" || true)
grade "every app frame recorded a primitive group (plate=)" \
      "$([ "${plate_none:-0}" = "0" ] && [ "$gsts" -gt 0 ] && echo PASS || echo FAIL)" \
      "$(grep -oE 'plate=[A-Za-z]+' "$LOG" | sort | uniq -c | tr '\n' ' ' || true)  plate=none rows=${plate_none:-0} of $gsts"

# The instrument must not travel with the product: rung 6b and rung 6c live in core_ps2.c under
# BTRON_PS2_BENCH, and if their strings ever appear in btron-ps2.elf then what the operator runs is the
# measurement, not the desktop.  Both rungs are named so a half-compiled census cannot pass.
ship_hits=$(grep -ac 'survival verdict' "$ELF" 2>/dev/null || true)
ship_6c=$(grep -ac 'eraser author' "$ELF" 2>/dev/null || true)
ship_20x=$(grep -ac 'eraser 20x' "$ELF" 2>/dev/null || true)
BENCH_ELF=btron-ps2-bench.elf
bench_hits=$(grep -ac 'survival verdict' "$BENCH_ELF" 2>/dev/null || true)
bench_6c=$(grep -ac 'eraser author' "$BENCH_ELF" 2>/dev/null || true)
bench_20x=$(grep -ac 'eraser 20x' "$BENCH_ELF" 2>/dev/null || true)
if [ "$REGRADED" = "1" ]; then
    grade "the census is bench-only" SKIP \
          "REGRADE: the ELF bytes on disk now are not the image these rows came from -- $OUTDIR/manifest.txt names that one"
else
    grade "the census is bench-only" \
          "$([ "${ship_hits:-0}" = "0" ] && [ "${ship_6c:-0}" = "0" ] && [ "${ship_20x:-0}" = "0" ] && \
              [ "${bench_hits:-0}" -ge 1 ] && [ "${bench_6c:-0}" -ge 1 ] && [ "${bench_20x:-0}" -ge 1 ] && echo PASS || echo FAIL)" \
          "ship: 'survival verdict'=${ship_hits:-0} 'eraser author'=${ship_6c:-0} 'eraser 20x'=${ship_20x:-0} || bench: 'survival verdict'=${bench_hits:-?} 'eraser author'=${bench_6c:-?} 'eraser 20x'=${bench_20x:-?}"
fi

dropmax=$(grep -oE '\| drop=[0-9]+' "$LOG" | grep -oE '[0-9]+' | sort -n | tail -1 || true)
grade "gif delivery ([GSTS] drop=)" "$([ -n "$dropmax" ] && [ "$dropmax" -eq 0 ] && echo PASS || echo FAIL)" \
      "worst drop=$dropmax over $(grep -c '\[GSTS\] groups=' "$LOG" || true) frame rows"

# The census runs once per app frame, and the FIRST one legitimately reads a window that has not
# painted yet -- on this image census #1 reports 1,578 of 384,000 px and census #2 the whole rect.
# So the grades read the LAST census and the BEST in_win, and both are named, because a grade that
# silently took census #1 says "the XMB draws nothing" and a grade that took the max says when the
# picture arrived.
live=$(grep '\[GSLIVE\] page:' "$LOG" | tail -1 || true)
ctrl=$(echo "$live" | grep -oE 'ctrl=[0-9]+' | cut -d= -f2 || true)
sent=$(echo "$live" | grep -oE 'sent=[0-9]+' | cut -d= -f2 || true)
grade "read certified (control 1024, no SENT words)" \
      "$([ "${ctrl:-0}" = "1024" ] && [ "${sent:-1}" = "0" ] && echo PASS || echo SKIP)" \
      "${live:-no [GSLIVE] page row: BENCH_GS=2 did not compile in, or the census SKIPped}"

censuses=$(grep -c '\[GSLIVE\] boxes:' "$LOG" || true)
box=$(grep '\[GSLIVE\] boxes:' "$LOG" | tail -1 || true)
inwin=$(echo "$box" | grep -oE 'in_win=[0-9]+' | cut -d= -f2 || true)
best=$(grep -oE 'in_win=[0-9]+' "$LOG" | cut -d= -f2 | sort -n | tail -1 || true)
winpx=$(grep '\[GSLIVE\] win:' "$LOG" | tail -1 | grep -oE 'px=[0-9]+' | cut -d= -f2 || true)
grade "ink arrives in the client rect" \
      "$([ "${best:-0}" -gt $(( ${winpx:-0} / 2 )) ] && echo PASS || echo FAIL)" \
      "best in_win=${best:-?} of ${winpx:-?} px over $censuses census(es); last one -- $box"

# Rung 20z's pixel law, stated as the page's own invariant: outside the layer's client rect the only
# writer is the canvas upload, so every pixel there must READ BACK as the canvas it was sent from.
# `diff_all` is the whole page's count of pixels that differ from the canvas, `in_win` the client's own
# share, and the gap between them is pixels that left the canvas's reach somewhere the app cannot draw.
# It is read on the LAST census only: the earlier ones still carry the census's own strips, whose
# bounding box legitimately reaches the page's corner, and rung 20z's subject is the steady-state pass,
# not the instrument's warm-up.  The control that makes this row worth having (2026-10-18): the staged
# narrow upload's gather was mutated to advance one pixel short of a canvas row per row, and the honest
# pairing 285,890 = 285,890 became 298,194 != 285,890 -- 12,304 px of the page stopped matching the
# canvas, all of them outside the client, which is exactly where a mis-strided gather puts them.
diff_all=$(echo "$live" | grep -oE 'diff_all=[0-9]+' | cut -d= -f2 || true)
last_inwin=$(echo "$box" | grep -oE 'in_win=[0-9]+' | cut -d= -f2 || true)
outside_gap=SKIP
if [ -n "$diff_all" ] && [ -n "$last_inwin" ]; then
    outside_gap=$([ "$diff_all" = "$last_inwin" ] && echo PASS || echo FAIL)
fi
grade "nothing outside the client differs from the page (rung 20z's staged upload is pixel-exact)" \
      "$outside_gap" \
      "diff_all=${diff_all:-?} in_win=${last_inwin:-?} -- outside-the-client diff = $(( ${diff_all:-0} - ${last_inwin:-0} )) px"

# Rung 6b's raw question was "does the ink the census sees stay on the page?", and on 2026-10-17 it
# answered erased=96,220 while the operator's own typed route answered erased=0 -- because the LAST
# census pair belongs to the bench's click phase, which leaves the global menu OPEN.  Rung 6c names the
# author, so the grade now reads the pair the claim is actually about (the typed route, no overlay) and
# treats an attributed hole as accounted-for rather than as a pass: a cover upload that re-lays the
# canvas inside the client after the stream is the present-order law working (LANE 5c), while a hole no
# cover upload explains is the bug.
svd=$(grep '\[GSLIVE\] survival verdict:' "$LOG" | tail -1 || true)
svi=$(grep '\[GSLIVE\] survival: ink' "$LOG" | tail -1 || true)
# The pair that can decide the claim is the first one whose BEFORE read is a painted window: census #1
# legitimately reads a window that has not drawn yet, and a hold-test on ink that never arrived proves
# nothing.  On the 2026-10-17 pinned runs that is the census #2 -> #3 pair, inside the typed-`xmb` route.
sv1=""
while IFS= read -r line; do
    then_ink=$(printf '%s' "$line" | grep -oE 'ink [0-9]+ ->' | grep -oE '[0-9]+' || true)
    if [ "${then_ink:-0}" -gt $(( ${winpx:-0} / 2 )) ]; then sv1="$line"; break; fi
done < <(grep '\[GSLIVE\] survival: ink' "$LOG" || true)
erased=$(echo "$svi" | grep -oE 'erased=[0-9]+' | cut -d= -f2 || true)
erased1=$(echo "$sv1" | grep -oE 'erased=[0-9]+' | cut -d= -f2 || true)
cover_row=$(grep -E '\[GSLIVE\] eraser: [0-9]+ cover upload' "$LOG" | tail -1 || true)
# The author row is printed at every census, and only the one beside the hole says who.  Prefer the row
# that actually names an OPEN overlay; fall back to the first so an unopened run still shows the getters.
author=$(grep '\[GSLIVE\] eraser author:' "$LOG" | grep 'global_menu=' | grep -m1 '(OPEN' || true)
[ -n "$author" ] || author=$(grep -m1 '\[GSLIVE\] eraser author: global_menu=' "$LOG" || true)
pair_verdicts=$(grep -c 'eraser verdict: FALSIFIED\|eraser verdict: PARTLY' "$LOG" || true)
attributed=$(grep -c 'eraser verdict: THE COVER UPLOADS ACCOUNT' "$LOG" || true)

# Rung 20x, which is what the survival claim rests on now.  The 6c classifier above counts COVER uploads
# only, so it calls a hole authored by a plain canvas pass "PARTLY accounted" even when that pass had no
# GL layer window at all -- the window closed, or had not opened yet -- which is desktop by contract, not
# an eraser.  The 20x row names the producer of every no-stream canvas upload, and `the cut failed on N`
# counts only the producers that are bugs: a live layer whose rect was not cut, a layer whose rect could
# not be computed, and a staged stream that turned out to hold nothing.  Highest over every census,
# because one erasing census anywhere is the failure.
row20x=$(grep '\[GSLIVE\] eraser 20x:' "$LOG" | tail -1 || true)
row20w=$(grep '\[GSLIVE\] eraser 20w:' "$LOG" | tail -1 || true)
verdict20x=$(grep '\[GSLIVE\] 20w verdict:' "$LOG" | tail -1 || true)
cutmax=$(grep -oE 'the cut failed on [0-9]+' "$LOG" | grep -oE '[0-9]+' | sort -n | tail -1 || true)

if [ -z "$row20x" ]; then
    grade "the cut holds: no canvas upload erases a live layer" SKIP \
          "no '[GSLIVE] eraser 20x' row -- this bench object predates rung 20x (rebuild src/cores/core_ps2.pbench.o)"
else
    grade "the cut holds: no canvas upload erases a live layer" \
          "$([ "${cutmax:-1}" = "0" ] && echo PASS || echo FAIL)" \
          "$(echo "$row20x" | sed -E 's/^\[[^]]*\] //')  ||  $(echo "$verdict20x" | sed -E 's/^\[[^]]*\] //')"
fi

if [ -z "$svi" ]; then
    grade "ink SURVIVES to the next present" SKIP \
          "no survival row -- rung 6b is not in this bench object (rebuild src/cores/core_ps2.pbench.o)"
elif [ -z "$sv1" ]; then
    grade "ink SURVIVES to the next present" SKIP \
          "no survival pair reads a painted window (best in_win=${best:-?} of ${winpx:-?} px) -- the hold test has nothing to hold"
elif [ "${cutmax:-0}" -gt 0 ]; then
    # The one reading that is a bug rather than a contract: desktop pixels put inside the layer's rect by
    # a pass that had a live layer and no cut, or that staged a stream which turned out to hold nothing.
    not_hold=$(grep -m1 'THE CUT IS NOT HOLDING' "$LOG" || true)
    [ -n "$not_hold" ] || not_hold="$verdict20x"
    grade "ink SURVIVES to the next present" FAIL \
          "$(echo "$row20x" | sed -E 's/^\[[^]]*\] //')  ||  $(echo "${not_hold:-no 20w verdict row}" | sed -E 's/^\[[^]]*\] //')  ||  $(echo "$sv1" | sed -E 's/^\[[^]]*\] //')"
elif [ "${erased1:-1}" != "0" ]; then
    # A hole on the decided pair while the cut holds: name its author from the rows printed beside THAT
    # pair, not from the last census -- the last one is the bench's click phase and a different story.
    # Lost rows that all sit inside no-stream uploads whose only producer is 'no layer window', or that
    # the cover uploads account for in full, are desktop by contract: there is no eraser to find.
    sv1_line=$(grep -n -F "$sv1" "$LOG" | head -1 | cut -d: -f1 || true)
    pair=$(sed -n "${sv1_line:-1},$(( ${sv1_line:-1} + 30 ))p" "$LOG")
    pair_cover=$(printf '%s\n' "$pair" | grep -m1 'eraser: .* cover upload' || true)
    pair_verdict=$(printf '%s\n' "$pair" | grep -m1 'eraser verdict:' || true)
    pair_20w=$(printf '%s\n' "$pair" | grep -m1 'eraser 20w:' || true)
    pair_20x=$(printf '%s\n' "$pair" | grep -m1 'eraser 20x:' || true)
    pair_verdict20=$(printf '%s\n' "$pair" | grep -m1 '20w verdict:' || true)
    ns_in=$(printf '%s\n' "$pair_20w" | sed -nE 's/.*; ([0-9]+) of ([0-9]+) lost row.*no-stream.*/\1/p')
    ns_all=$(printf '%s\n' "$pair_20w" | sed -nE 's/.*; ([0-9]+) of ([0-9]+) lost row.*no-stream.*/\2/p')
    pair_nolayer=$(printf '%s\n' "$pair_20x" | grep -oE 'no-layer=[0-9]+' | grep -oE '[0-9]+' || true)
    by_contract=0
    if [ -n "$ns_all" ] && [ "${ns_in:-0}" = "$ns_all" ] && [ "${pair_nolayer:-0}" -ge 1 ]; then
        by_contract=1
    fi
    if [ "$by_contract" = "1" ] || printf '%s' "$pair_verdict" | grep -q 'THE COVER UPLOADS ACCOUNT'; then
        grade "ink SURVIVES to the next present" PASS \
              "typed-route pair erased=${erased1}, hole has a named author -- ${pair_verdict20:-$(echo "$verdict20x" | sed -E 's/^\[[^]]*\] //')}  ||  ${pair_20x:-no 20x row beside this pair}  ||  ${pair_cover:-no cover row}  ||  $(echo "$sv1" | sed -E 's/^\[[^]]*\] //')"
    else
        grade "ink SURVIVES to the next present" FAIL \
              "the hole has no named author -- $(echo "$sv1" | sed -E 's/^\[[^]]*\] //')  ||  ${pair_verdict:-no 6c verdict beside this pair}  ||  ${pair_20x:-no 20x row}  ||  ${pair_cover:-no cover-upload row}"
    fi
else
    grade "ink SURVIVES to the next present" PASS \
          "typed-route pair -- $(echo "$sv1" | sed -E 's/^\[[^]]*\] //')  (cut holds everywhere: worst 'the cut failed on' = ${cutmax:-no 20x row}, $attributed census(es) with the cover list accounting for a whole hole, $pair_verdicts where 6c alone could not name it)"
fi

# The LAST census is the bench's click phase, so its hole belongs to an overlay rather than to the typed
# route this claim is about.  It still has to have a name: a cover rect of an overlay that is really open,
# or a pass with no layer window.  Neither name is a pass of the survival claim -- it is the eraser being
# accounted for, which is what rung 6c and rung 20x between them can prove.
if [ -n "$svi" ] && [ "${erased:-1}" != "0" ] && [ "${cutmax:-0}" = "0" ]; then
    if [ -n "$cover_row" ] || [ -n "$author" ] || [ -n "$row20x" ]; then
        last_ns=$(printf '%s\n' "$row20w" | sed -nE 's/.*; ([0-9]+) of ([0-9]+) lost row.*no-stream.*/\1 \2/p')
        last_in=$(echo "$last_ns" | cut -d' ' -f1)
        last_all=$(echo "$last_ns" | cut -d' ' -f2)
        last_contract=0
        if [ -n "$last_all" ] && [ "${last_in:-0}" = "$last_all" ] && \
           [ "$(echo "$row20x" | grep -oE 'no-layer=[0-9]+' | grep -oE '[0-9]+' || echo 0)" -ge 1 ]; then
            last_contract=1
        fi
        hole_named=SKIP
        if [ "${attributed:-0}" -ge 1 ] || [ "$last_contract" = "1" ]; then hole_named=PASS; fi
        grade "the residual hole has an author" "$hole_named" \
              "${cover_row:-no cover-upload row in this log}  ||  ${author:-(no author row)}  ||  ${row20x:-no 20x row}$(if [ "${attributed:-0}" = "0" ] && [ "$last_contract" = "1" ]; then echo '  NAMED BY CONTRACT: a pass with no GL layer window, not a cover rect'; fi)"
    else
        grade "the residual hole has an author" SKIP \
              "last pair erased=${erased:-?}, but this bench object carries no rung-6c eraser rows -- rebuild src/cores/core_ps2.pbench.o to name it"
    fi
fi

verb=$(grep -m1 '\[BENCH\] shell cycle 1' "$LOG" || true)
grade "the typed route opened a window (g_term \`xmb\`)" \
      "$([ -n "$verb" ] && echo PASS || echo SKIP)" "${verb:-the g_term verb phase did not run}"

if [ "$RUNS" -gt 1 ]; then
    # Compared with the emulator's wall clock and the census's own microsecond cost taken out: those
    # two columns are what a rerun is allowed to differ in.  Everything else in the row is the page.
    a=$(grep '\[GSLIVE\] page:' "$OUTDIR/run1.log" | tail -1 | sed -E 's/^\[[^]]*\] //; s/[0-9]+ us [0-9]+ qw//' || true)
    b=$(grep '\[GSLIVE\] page:' "$OUTDIR/run2.log" | tail -1 | sed -E 's/^\[[^]]*\] //; s/[0-9]+ us [0-9]+ qw//' || true)
    grade "run-to-run repeatability (last census page row)" \
          "$([ -n "$a" ] && [ "$a" = "$b" ] && echo PASS || echo FAIL)" \
          "run1: ${a:-none}  ||  run2: ${b:-none}"
fi

echo "----------------------------------------------------------"
cat "$MANIFEST"
echo "  logs : $OUTDIR"
if [ "$fails" -eq 0 ]; then
    echo "  [REPRO] every graded row passed."
else
    echo "  [REPRO] $fails graded row(s) FAIL -- read the deciding rows above; they are quoted verbatim."
    exit 1
fi
