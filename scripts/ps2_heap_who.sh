#!/bin/sh
# ps2_heap_who.sh -- name the holder behind a `[HEAP]` census address.
#
#   scripts/ps2_heap_who.sh 0x1467ec 0x1c57a8
#   scripts/ps2_heap_who.sh build/ps2-bench.log
#   scripts/ps2_heap_who.sh --elf btron-ps2.elf 0x1467ec
#
# The guest stamps only an address: `Imalloc`/`Icalloc`/`malloc`/`calloc` write the caller's return
# address into the block header (`src/cores/core_ps2.c`, ps2_blk.ra), the census prints
# `0xADDR holds N kB in M block(s)`, and a failed ask prints `asking N kB from 0xADDR`.  Nothing on
# the box needs a symbol table, so this script is the other half of the instrument.
#
# Resolution is the nearest text symbol at or below the address.  That is a GUESS until you see the
# call, so the script also disassembles a few instructions around the address and prints them: a
# stamped site is the instruction AFTER a `jal`, so seeing `jal <Icalloc>` just below the address is
# what turns a name into a fact.  When the offset from the symbol is large the name is probably a
# static that the symtab does not carry, and the row says so instead of lying quietly.
#
# Addresses only mean something for the image that printed them: btron-ps2.elf and
# btron-ps2-bench.elf are different links, so a bench row resolved against the ship ELF names the
# wrong function.  The script picks from the log it was given and always says which ELF it used.

set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
SHIP="$ROOT/btron-ps2.elf"
BENCH="$ROOT/btron-ps2-bench.elf"
ELF=""
LOG=""
ADDRS=""

NM=$(command -v llvm-nm 2>/dev/null || true)
OD=$(command -v llvm-objdump 2>/dev/null || true)
for d in /opt/homebrew/opt/llvm/bin /usr/local/opt/llvm/bin; do
    [ -n "$NM" ] || NM="$d/llvm-nm"
    [ -n "$OD" ] || OD="$d/llvm-objdump"
done
[ -x "$NM" ] || NM=""
[ -x "$OD" ] || OD=""

while [ $# -gt 0 ]; do
    case "$1" in
        --elf)  ELF="${2:-}"; shift 2 ;;
        --log)  LOG="${2:-}"; shift 2 ;;
        -h|--help) sed -n '2,30p' "$0"; exit 0 ;;
        0x*|0X*) ADDRS="$ADDRS $1"; shift ;;
        *.elf)   ELF="$1"; shift ;;
        *)       [ -f "$1" ] && LOG="$1" || { echo "unknown argument: $1" >&2; exit 2; }
                 shift ;;
    esac
done

if [ -n "$LOG" ] && [ -f "$LOG" ]; then
    # Every address the census and the failed-ask rows print, biggest-holder rows first because
    # that is the order the console prints them in.
    la=$(grep -oE '\[HEAP\][^ ]* +(holder census|0x[0-9a-f]+ holds|out of memory asking [0-9]+ kB from) 0x[0-9a-f]+|from 0x[0-9a-f]+|0x[0-9a-f]+ holds' "$LOG" 2>/dev/null \
         | grep -oE '0x[0-9a-f]+' | sort -u || true)
    for a in $la; do ADDRS="$ADDRS $a"; done
    if [ -z "$ELF" ]; then
        case "$LOG" in *bench*) ELF="$BENCH" ;; *) ELF="$SHIP" ;; esac
        grep -q '\[BENCH\]' "$LOG" 2>/dev/null && ELF="$BENCH"
    fi
fi

if [ -z "$ELF" ]; then
    for cand in "$SHIP" "$BENCH"; do [ -f "$cand" ] && { ELF="$cand"; break; }; done
fi
if [ -z "$ELF" ] || [ ! -f "$ELF" ]; then
    echo "no ELF to resolve against: build one (make ps2 / make ps2-bench) or pass --elf PATH" >&2
    exit 1
fi
if [ -z "$ADDRS" ]; then
    echo "usage: $0 <address ...> | <log file> [--elf PATH]" >&2
    exit 2
fi
if [ -z "$NM" ]; then
    echo "llvm-nm not found (tried PATH, /opt/homebrew/opt/llvm/bin, /usr/local/opt/llvm/bin)" >&2
    exit 1
fi

echo "ELF: $ELF"
SYMS=$(mktemp); trap 'rm -f "$SYMS"' EXIT
# Code symbols, sorted by address, as:  <dec-addr> <dec-size> <name>
#
# The type filter matters more than it looks.  Text symbols are not all `t`/`T`: a function that is
# weak (a C++-style inline, or one the port marks W) is `W`, and MIPS emits `$`-prefixed mapping
# symbols between functions.  Filtering to [tT] alone dropped `open_gterm_window_rect` on
# 2026-10-17, which made the census's second-biggest holder (2,631 kB) resolve to the function
# *before* it and print `[OUTSIDE the symbol's own size]` -- a wrong name that at least said so.
"$NM" --print-size --defined-only "$ELF" 2>/dev/null | perl -ne '
    chomp; my @f = split /\s+/; next if @f < 3;
    my ($a, $s, $t, $n) = @f == 4 ? ($f[0], $f[1], $f[2], $f[3]) : (undef, undef, $f[-2], $f[-1]);
    next unless defined $t && $t =~ /^[tTWwi]$/;
    next if $n =~ /^\$/;                       # MIPS mapping symbols: $Ltmp0, $a, $r ...
    my ($aa, $ss) = (hex($a), $s ? hex($s) : 0);
    print "$aa $ss $n\n";' | sort -n > "$SYMS"
if [ ! -s "$SYMS" ]; then
    echo "no code symbols in $ELF -- is it stripped?" >&2
    exit 1
fi

for A in $ADDRS; do
    perl -e '
        my ($q, $syms, $elf, $od) = @ARGV;
        my $addr = hex($q);
        my @s;
        open(my $F, "<", $syms) or die "cannot read $syms: $!";
        while (<$F>) { my ($a, $sz, $n) = split; push @s, [$a, $sz, $n]; }
        my ($best, $next);
        for my $e (@s) {
            next if $e->[0] > $addr;
            $best = $e if !$best || $e->[0] > $best->[0];
        }
        for my $e (@s) { if ($e->[0] > $addr) { $next = $e; last } }
        if (!$best) { printf("%s  BELOW the first text symbol -- not a code address for this image\n", $q); exit 0 }
        my $off = $addr - $best->[0];
        my $span = ($next ? $next->[0] - $best->[0] : 0);
        my $note = "";
        if ($best->[1] && $addr >= $best->[0] + $best->[1]) { $note = "  [OUTSIDE the symbol'"'"'s own size]" }
        elsif ($span && $off > $span) { $note = "  [past the next symbol]" }
        elsif ($off > 0x800) { $note = "  [far from the symbol start: a local without a symtab entry is likely]" }
        printf("%s  %s +0x%x%s\n", $q, $best->[2], $off, $note);
        printf("          symbol at 0x%x, size %s, next %s\n", $best->[0],
               ($best->[1] ? sprintf("0x%x", $best->[1]) : "unknown"),
               ($next ? sprintf("%s@0x%x", $next->[2], $next->[0]) : "(end)"));
    ' "$A" "$SYMS" "$ELF" "$OD"

    # The call that proves the name: print the instruction at the stamped address and the two above
    # it, so a `jal` a few bytes back is visible rather than implied.
    if [ -n "$OD" ]; then
        lo=$(perl -e 'printf("%d", hex($ARGV[0]) - 12)' "$A")
        hi=$(perl -e 'printf("%d", hex($ARGV[0]) + 4)' "$A")
        "$OD" -d --start-address="$lo" --stop-address="$hi" "$ELF" 2>/dev/null \
            | grep -E "^ *$(printf '%x' $((0x${A#0x})))" | sed 's/^/          at: /' || true
        "$OD" -d --start-address="$lo" --stop-address="$hi" "$ELF" 2>/dev/null \
            | grep -E '^\s+[0-9a-f]+:' | sed 's/^/          /' | tail -4 || true
    fi

    # Where that function is defined, as a hint -- first match, so a declaration can win the race.
    # src/tools/ is excluded: the screen-capture host tools define stubs with the same names as the
    # real windows (open_xmb_window() there is `return cab_no_win();`), and naming the stub would be
    # a wrong answer that looks right.
    name=$(perl -e '
        my ($q, $syms) = @ARGV; my $addr = hex($q); my $best = "";
        open(my $F, "<", $syms) or exit;
        while (<$F>) { my ($a, $sz, $n) = split; if ($a <= $addr) { $best = $n } else { last } }
        print $best;' "$A" "$SYMS")
    if [ -n "$name" ]; then
        grep -rn "^[A-Za-z_].*[ *]${name}(" "$ROOT/src" --include='*.c' 2>/dev/null \
            | grep -v 'src/tools/' | grep -vE ';$' | head -1 | sed "s|$ROOT/||; s/^/          defined: /" || true
    fi
done
