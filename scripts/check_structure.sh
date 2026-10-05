#!/bin/sh
# B-System structure checks — the invariants whose absence made a new CLU app
# break seven targets one link error per push.
#
# Rule 1  one main per executable: no target's source list may define main()
#         twice. A library module that also ships as a standalone tool must put
#         its main() behind an *_STANDALONE guard (see src/clu/tv/tv.c).
# Rule 2  one libc mapping: including <string.h>/<stdlib.h> on a freestanding
#         target is decided in include/btron/libc_shim.h and src/kernel/libstr.c
#         alone. Any source that #defines a stdio/stdlib name onto tkl_*/Imalloc
#         restates that mapping and must not exist. Testing __STDC_HOSTED__ (or
#         BTRON_HOSTED) to gate POSIX code is fine and not checked here.
#         Offenders are a ratchet in scripts/structure_baseline.txt: they may be
#         removed, never added.
#
# Run from the repo root: scripts/check_structure.sh

set -u
cd "$(dirname "$0")/.." || exit 1

fail=0

# ── Rule 1: one main() per target ───────────────────────────────────────
# Count main() definitions that are not inside an *_STANDALONE conditional.
# A main() inside a conditional that mentions *_STANDALONE is a tool entry
# point, not part of any desktop/kernel target, so it is not counted.
count_mains() {
    awk '
	/^#[ \t]*if/    { depth++; cond[depth] = ($0 ~ /STANDALONE/) ? 1 : 0; guarded += cond[depth] }
	/^#[ \t]*endif/ { if (depth > 0) { guarded -= cond[depth]; depth-- } }
	/(^|[^A-Za-z0-9_])(int|void)[ \t]+main[ \t]*\(/ && guarded == 0 { n++ }
	END { print n + 0 }
    ' "$1" 2>/dev/null
}

for var in POSIX_SRCS QEMU_SRCS TKERNEL_SRCS FOMA_SRCS UEFI_SRCS PC98_SRCS \
           ARM32_BAREMETAL_SRCS ARM64_BAREMETAL_SRCS M68K_SRCS PS2_SRCS MIPS_SRCS; do
    sources=$(make -s "list-$var" 2>/dev/null)
    if [ -z "$sources" ]; then
	echo "check_structure: warning: \$($var) is empty or unknown"
	continue
    fi
    mains=""
    for src in $sources; do
	[ -f "$src" ] || continue
	if [ "$(count_mains "$src")" -gt 0 ]; then
	    mains="$mains $src"
	fi
    done
    # shellcheck disable=SC2086
    num=$(echo $mains | wc -w | tr -d ' ')
    if [ "$num" -gt 1 ]; then
	echo "ERROR: target sources \$$var defines main() $num times:$mains"
	echo "       a library module must guard its main() with #ifdef <MODULE>_STANDALONE"
	fail=1
    fi
done

# ── Rule 2: the libc mapping lives in exactly one place ─────────────────
# Offender fingerprint: #define <name> onto tkl_*/Imalloc/Icalloc/Ifree.
# That is a hand-rolled copy of include/btron/libc_shim.h, and every copy is
# one more file to patch when a new target or a new libc call appears.
# Legitimate owners of the mapping:
#   include/btron/libc_shim.h  - THE mapping
#   src/kernel/libstr.c        - implements the tkl_* functions
#   include/libstr.h           - declares them (uppercase MEMCPY/STRCPY macros)
#   include/gl/{stdio,stdlib,string}.h - header override for third-party code
#     (TinyGL, Quake, stb) that includes <string.h> by its real name and cannot
#     be edited to include the shim instead.
MAP_FINGERPRINT='^[[:space:]]*#[[:space:]]*define[[:space:]]+[a-z_0-9]+(\([^)]*\))?[[:space:]]+(tkl_[a-z_0-9]+|I(malloc|calloc|free))([[:space:]]|$)'
current=$(grep -rlE "$MAP_FINGERPRINT" src include 2>/dev/null \
    | grep -v -e '^src/kernel/libstr.c$' -e '^include/libstr.h$' \
              -e '^include/gl/' -e '^include/btron/libc_shim.h$' \
    | sort | tr '\n' ' ')
baseline=$(sed -n '/^[^#]/p' scripts/structure_baseline.txt 2>/dev/null | tr '\n' ' ')

for offender in $current; do
    case " $baseline " in
    *" $offender "*) ;;
    *)
	echo "ERROR: $offender hand-rolls the libc mapping"
	echo "       include <btron/libc_shim.h> and test BTRON_HOSTED instead;"
	echo "       add missing functions to libc_shim.h + src/kernel/libstr.c once"
	fail=1
	;;
    esac
done

for offender in $baseline; do
    if [ ! -f "$offender" ]; then
	echo "check_structure: baseline entry $offender no longer exists - remove it"
    elif ! grep -qE "$MAP_FINGERPRINT" "$offender"; then
	echo "check_structure: baseline entry $offender no longer maps libc - remove it"
    fi
done

# Informational: testing hostedness is fine (POSIX-only code), restating the
# libc mapping is not. Nothing to fix here, just a count of what is guarded.
hosted=$(grep -rl '__STDC_HOSTED__' src include 2>/dev/null | wc -w | tr -d ' ')
echo "check_structure: $hosted files still test __STDC_HOSTED__ directly (POSIX capability checks)"

if [ "$fail" -eq 0 ]; then
    echo "check_structure: OK (one main per target, libc shim not duplicated)"
fi
exit "$fail"
