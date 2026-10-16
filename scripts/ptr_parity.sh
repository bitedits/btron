#!/usr/bin/env bash
# scripts/ptr_parity.sh -- derive the PS2 pointer's gain from the emulator's own window
#
# Copyright 2026 Synrc Research Center. MIT License.
#
# Why this is a script and not a paragraph in a book: this port has exactly one
# pointer multiplier (doc/txt/PS2.txt LAW 13b/13e), and the number it should carry is
# not arithmetic the guest can reach -- it is the width of the emulator's render area
# on this machine, in the units PCSX2 counts cursor movement in.  Writing that number
# into core_ps2.c by hand makes it a second copy of a fact that lives in the ini, and
# the copy goes stale the moment the window is resized.  So it is derived here, from
# the same file PCSX2 writes, and PS2_EMU_MULT_FP is only the last value this printed.
#
# What it reads:
#   [UI] RenderToSeparateWindow   -- which of the two geometries is the render area
#   [UI] MainWindowGeometry /
#   [UI] DisplayWindowGeometry    -- a QByteArray of a saved Qt window geometry:
#                                   big-endian int32 header, stream version, then
#                                   x, y, w, h, then the same four again for the
#                                   frame geometry (offsets 16 and 20 are w and h)
#   [EmuCore/GS] AspectRatio      -- whether the canvas fills the widget (Stretch)
#                                   or is letterboxed inside it (4:3, 16:9, ...)
#
# What it prints: the per-axis parity multiplier, 8.8 like the port's own, and the
# nearest stop on the dial the port actually ships.  Horizontal and vertical are
# printed separately on purpose -- if the window's aspect is not the PS2's, one
# constant cannot be parity on both axes at once, and that difference is a real
# reading, not a rounding error.
#
# The header and the two quads are checked, not assumed: a PCSX2 that changes its
# geometry format gets an error rather than a plausible number.
#
# Read-only.  Nothing here writes the ini or a source file.
#
# Usage: scripts/ptr_parity.sh [ini]     BTRON_PTR_CHROME=<pt> subtracts the main
# window's menu/tool/status bar height, which the ini does not record.

set -euo pipefail
cd "$(dirname "$0")/.."

INI="${1:-pcsx2/PCSX2/inis/PCSX2.ini}"
CHROME="${BTRON_PTR_CHROME:-0}"
CANVAS_W=800
CANVAS_H=600
GEOM_MAGIC=31051979              # 0x01DBD0C3, the stream header Qt writes

if [ ! -f "$INI" ]; then
    echo "[ERROR] no ini at $INI"
    exit 1
fi

# PCSX2's ini is written by QSettings, which puts spaces around the '=' -- so a
# reader that looks for `Key=` finds nothing and reports the key as absent.
ini_get() {
    sed -n -E "s/^[[:space:]]*$1[[:space:]]*=[[:space:]]*(.*)$/\1/p" "$INI" | head -1 | tr -d '[:space:]'
}

b64_decode() {
    if printf 'aGk=' | base64 -D >/dev/null 2>&1; then
        base64 -D
    else
        base64 --decode
    fi
}

# One geometry key -> "x y w h frame_w frame_h", read as big-endian int32s.
geom_quads() {
    local key="$1" line
    line="$(ini_get "$key")"
    [ -n "$line" ] || return 1
    printf '%s' "$line" | b64_decode | od -An -tu1 -v | tr -s ' \n' ' ' | awk '
        { for (i = 1; i <= NF; i++) b[n + i] = $i; n += NF }
        END {
            if (n < 40) { print "SHORT"; exit }
            for (o = 0; o < 40; o += 4) {
                v = 0
                for (i = 0; i < 4; i++) v = v * 256 + b[o + i + 1]
                printf "%d ", v
            }
            printf "\n"
        }'
}

# Validate before using: the magic, a sane size, and the widget quad agreeing with
# the frame quad (they differ only when a window has native decorations of its own).
# The stream is header, version, then x, y, w, h, then the same four for the frame
# rect, so the fields are 5 and 6, not 4 and 5.
geom_wh() {
    local key="$1" quads magic w h fw fh
    quads="$(geom_quads "$key")" || return 1
    set -- $quads
    magic="$1"; w="$5"; h="$6"; fw="$9"; fh="${10}"
    if [ "$magic" != "$GEOM_MAGIC" ]; then
        echo "[ERROR] $key stream header is $magic, not $GEOM_MAGIC -- PCSX2 changed its geometry format; this script's offsets are stale" >&2
        return 2
    fi
    if [ "$w" -lt 64 ] || [ "$h" -lt 64 ]; then
        echo "[ERROR] $key decodes to ${w}x${h}, which is not a window" >&2
        return 2
    fi
    echo "$w $h $fw $fh"
}

separate="$(ini_get RenderToSeparateWindow)"
aspect="$(ini_get AspectRatio)"
[ -n "$separate" ] || separate="false"

if [ "$separate" = "true" ]; then
    key="DisplayWindowGeometry"; what="the separate display window"
else
    key="MainWindowGeometry"; what="the main window (render is embedded)"
fi

if ! wh="$(geom_wh "$key")"; then
    echo "[ERROR] $INI has no readable $key -- run the emulator once so Qt can save the window"
    exit 1
    fi
win_w="$(echo "$wh" | cut -d' ' -f1)"; win_h="$(echo "$wh" | cut -d' ' -f2)"
frame_w="$(echo "$wh" | cut -d' ' -f3)"; frame_h="$(echo "$wh" | cut -d' ' -f4)"

echo "=========================================================="
echo " PS2 pointer parity -- derived from $INI"
echo "=========================================================="
echo " window    : $key = ${win_w} x ${win_h} pt ($what)"
if [ "$frame_w" != "$win_w" ] || [ "$frame_h" != "$win_h" ]; then
    echo " frame     : ${frame_w} x ${frame_h} pt (the decorated rect; the widget is the line above)"
fi
echo " aspect    : AspectRatio = ${aspect:-<unset>}"

# The render area inside the widget.  Stretch fills it; a fixed ratio letterboxes to
# the PS2's own 4:3 inside it.
#
# Chrome first, and only ever on the height: the ini records the WINDOW, and under the
# embedded render the canvas fills the widget below the menu/tool/status bar.  A caller
# who knows that height passes BTRON_PTR_CHROME=<pt>; without it the vertical figure is
# an upper bound, which is printed as a caveat rather than quietly accepted.
if [ "$separate" != "true" ] && [ "${CHROME:-0}" -gt 0 ] 2>/dev/null; then
    echo " chrome    : ${CHROME} pt taken off the window height (BTRON_PTR_CHROME)"
    win_h=$(( win_h - CHROME )); [ "$win_h" -lt 64 ] && win_h=64
fi

r_w="$win_w"; r_h="$win_h"
case "$aspect" in
    4:3|PAL|NTSC) ar="4:3" ;;
    16:9)         ar="16:9" ;;
    16:10)        ar="16:10" ;;
    5:4)          ar="5:4" ;;
    7:5)          ar="7:5" ;;
    2:1)          ar="2:1" ;;
    *)            ar="" ;;
esac
if [ -n "$ar" ]; then
    ar_n="${ar%%:*}"; ar_d="${ar##*:}"
    echo "           : ${ar} letterboxes inside the widget"
    r_h=$(( (win_w * ar_d + ar_n / 2) / ar_n ))
    if [ "$r_h" -gt "$win_h" ]; then
        r_h="$win_h"; r_w=$(( (win_h * ar_n + ar_d / 2) / ar_d ))
    fi
fi

# Parity: the guest moves this many pixels for the width the host cursor crosses.
# 8.8 fixed point, so 256 * canvas / render is the multiplier a count is worth.
p8_x=$(( 256 * CANVAS_W / r_w ))
p8_y=$(( 256 * CANVAS_H / r_h ))

# The dial's stops, from the same table the port ships (src/graphics/dp_accel.c).
stops="0 64 96 128 160 192 256"
nearest() {
    local want="$1" best=0 bd=99999 s d
    for s in $stops; do
        [ "$s" -eq 0 ] && continue
        d=$(( s - want )); [ "$d" -lt 0 ] && d=$(( -d ))
        if [ "$d" -lt "$bd" ]; then bd="$d"; best="$s"; fi
    done
    echo "$best"
}
near_x="$(nearest "$p8_x")"; near_y="$(nearest "$p8_y")"

# 8.8 -> "0.67", rounding the hundredths so a stop reads the way btron_ptr_gain_label()
# prints it rather than as a truncated /256 fraction.
dec() {
    local v="$1"
    echo "$(( v / 256 )).$(printf '%02d' $(( ((v % 256) * 100 + 128) / 256 )))"
}

echo " render  : ${r_w} x ${r_h} pt for the ${CANVAS_W}x${CANVAS_H} canvas"
echo ""
echo " parity x: $p8_x/256 = $(dec "$p8_x") guest px per host px   -> nearest stop $near_x/256"
echo " parity y: $p8_y/256 = $(dec "$p8_y") guest px per host px   -> nearest stop $near_y/256"
echo ""

if [ "$near_x" = "$near_y" ]; then
    echo " [OK] one constant serves both axes at this geometry: PS2_EMU_MULT_FP = $near_x"
else
    d=$(( p8_x - p8_y )); [ "$d" -lt 0 ] && d=$(( -d ))
    big="$p8_x"; [ "$p8_y" -gt "$big" ] && big="$p8_y"
    echo " [NOTE] the two axes differ by $d/256 ($(( d * 100 / big ))%), because"
    echo "        ${CANVAS_W}x${CANVAS_H} is not the render area's aspect.  One multiplier cannot be"
    echo "        parity on both axes while AspectRatio is ${aspect:-<unset>}; the smaller of"
    echo "        the two never reaches the far wall, the larger reaches it before the host does."
    echo "        Either set AspectRatio = 4:3 (letterboxes, then x and y agree) or accept the"
    echo "        larger stop and let the wall re-sync the axis that overshoots."
fi

echo ""
echo " Caveats this script cannot close, stated rather than assumed:"
echo "  - Qt reports cursor movement in logical points; this derives parity in points."
echo "    On a Retina display a native-pixel count would be twice this, and one sweep"
echo "    with \`ptgain <host px>\` in a booted session settles which unit PCSX2 uses."
if [ "$separate" != "true" ]; then
    echo "  - Under the embedded render, the canvas fills the render WIDGET, and the ini"
    echo "    records the WINDOW.  A menu/tool/status bar takes some of the height, so the"
    echo "    y figure is an upper bound until BTRON_PTR_CHROME says otherwise; x is not"
    echo "    affected because no chrome is sideways."
fi
echo "  - The geometry is whatever Qt last saved.  Resize the window and re-run this."
echo "  - PS2_EMU_MULT_FP is a compiled default; the session dials (\`sens\`, the panel,"
echo "    the launcher row) all write g_ptr_gain_fp and do not survive a reboot."
