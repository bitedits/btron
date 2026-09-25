#!/usr/bin/env python3
"""
scripts/update_segui_screens.py

Turn the raw framebuffer dumps from `make segui-screens` (the headless
SegUI capture tool) into PNGs under b-system/img/segui/.

A raw frame is [u32 width][u32 height][width*height u32 pixels], where each
pixel is the host's in-memory COLOR word: 0xAARRGGBB stored little-endian, so
the byte order on disk is B, G, R, A.
"""

import glob
import os
import re
import struct
import sys
import zlib

BASE_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
RAW_DIR = "/tmp/segui_raw_screens"
IMG_OUT_DIR = os.path.join(BASE_DIR, "b-system", "img", "segui")
MOBILE_HTML = os.path.join(BASE_DIR, "mobile.html")

MARKER = "<!-- \u00b5BTRON-SEGUI Toolkit UI Showcase -->"
TAIL_MARKER = "<!-- Relation to B-System -->"

# (file, tab/screen, badge, Japanese title, description)
SCREENS = [
    ("segui_demo_home", "\u30c7\u30e2 / SegUI", "4 Buttons + Label",
     "SegUI \u30c7\u30e2\u30fb\u30a6\u30a3\u30f3\u30c9\u30a6",
     "\u69cb\u6210\u306f\u30e9\u30d9\u30eb1\u884c\u3068\u30a2\u30a4\u30b3\u30f3\u4ed8\u304d\u30dc\u30bf\u30f34\u3064\u3060\u3051\u3002\u9078\u629e\u4e2d\u306e\u300cFast\u300d\u306f\u9762\u3092\u6c88\u307e\u305b\u305f\u53cd\u8ee2\u8868\u793a\u3002\u30dc\u30bf\u30f3\u306fBODY\u5e2f\u306e\u5e45\u304b\u3089\u81ea\u52d5\u30674\u5217\u30b0\u30ea\u30c3\u30c9\u306b\u4e26\u3076\u3002"),
    ("segui_demo_widgets", "\u30c7\u30e2 / Widgets", "Widgets",
     "SegUI \u30a6\u30a3\u30b8\u30a7\u30c3\u30c8\u7fa4",
     "\u30dc\u30bf\u30f34\u3001\u30c1\u30a7\u30c3\u30af\u30dc\u30c3\u30af\u30b92\u3001\u30c8\u30b0\u30eb\u30b9\u30a4\u30c3\u30c11\u3002\u30c1\u30a7\u30c3\u30af\u5370\u3082\u30c8\u30b0\u30eb\u306e\u30c4\u30de\u30df\u3082\u30b8\u30aa\u30e1\u30c8\u30ea\u3067\u63cf\u3044\u3066\u304a\u308a\u3001\u30d5\u30a9\u30f3\u30c8\u306e\u6587\u5b57\u30ab\u30d0\u30ec\u30c3\u30b8\u306b\u4f9d\u5b58\u3057\u306a\u3044\u3002"),
    ("segui_demo_theme", "\u30c7\u30e2 / Theme", "Theme Table",
     "\u30c6\u30fc\u30de\u306e\u5207\u308a\u66ff\u3048",
     "3\u3064\u306e\u30e9\u30c3\u30c1\u30dc\u30bf\u30f3\u3067\u8272\u30c6\u30fc\u30d6\u30eb\u3060\u3051\u3092\u5dee\u3057\u66ff\u3048\u308b\u3002\u30ec\u30a4\u30a2\u30a6\u30c8\u30fb\u30bb\u30b0\u30e1\u30f3\u30c8\u6570\u30fb\u30b3\u30fc\u30c9\u306f\u4e00\u5207\u5909\u308f\u3089\u306a\u3044\u3002"),
    ("segui_demo_info", "\u30c7\u30e2 / Info", "List Widget",
     "\u30ea\u30b9\u30c8\u30bb\u30b0\u30e1\u30f3\u30c8",
     "\u898b\u51fa\u3057\u884c\u3068\u5024\u884c\u3092\u6301\u3064\u30ea\u30b9\u30c8\u3002\u53f3\u7aef\u306e\u30c4\u30de\u30df\u306f\u884c\u6570\u306b\u5bfe\u3057\u3066\u6bd4\u4f8b\u3057\u305f\u8868\u793a\u7bc4\u56f2\u3092\u793a\u3059\u3002"),
    ("segui_demo_apps", "\u30c7\u30e2 / Apps", "Tiles",
     "\u30a2\u30d7\u30ea\u30bf\u30a4\u30eb",
     "\u540c\u3058\u30bb\u30b0\u30e1\u30f3\u30c8\u8868\u304b\u3089\u751f\u307e\u308c\u308b\u30a2\u30d7\u30ea\u30bf\u30a4\u30eb\u3002\u30dc\u30bf\u30f3\u6570\u304c\u5897\u3048\u308b\u3060\u3051\u3067\u30ec\u30a4\u30a2\u30a6\u30c8\u306f\u81ea\u52d5\u3067\u7d44\u307f\u66ff\u308f\u308b\u3002"),
    ("segui_phone_contacts", "\u96fb\u8a71 / Contacts", "Phone",
     "\u9023\u7d61\u5148",
     "\u982d\u6587\u5b57\u30c7\u30a3\u30b9\u30af\u3001\u30bb\u30af\u30b7\u30e7\u30f3\u898b\u51fa\u3057\u3001\u73fe\u5728\u9805\u76ee\u306e\u5f37\u8abf\u3002\u9078\u629e\u3057\u305f\u884c\u306f\u305d\u306e\u307e\u307e\u30c1\u30e3\u30c3\u30c8\u3078\u629c\u3051\u308b\u3002"),
    ("segui_phone_chat", "\u96fb\u8a71 / Chat", "LoRa Chat",
     "\u30c1\u30e3\u30c3\u30c8\u30bb\u30b0\u30e1\u30f3\u30c8",
     "\u65b0\u3057\u3044\u884c\u304c\u4e0b\u3001\u53e4\u3044\u884c\u306f\u4e0a\u304b\u3089\u62bc\u3057\u51fa\u3055\u308c\u308b\u3002\u76f8\u624b\u540d\u306f\u30bb\u30b0\u30e1\u30f3\u30c8\u5185\u306e\u56fa\u5b9a\u30d8\u30c3\u30c0\u30fc\u3002"),
    ("segui_phone_people", "\u96fb\u8a71 / People", "Circles",
     "\u4eba\u3005 \u2015 \u30b0\u30eb\u30fc\u30d7\u3068\u6700\u8fd1",
     "\u4eba\u6570\u30d0\u30c3\u30b8\u4ed8\u304d\u306e\u5f79\u5272\u884c\u3002\u30ea\u30b9\u30c8\u30bb\u30b0\u30e1\u30f3\u30c8\u306e\u884c\u7a2e\u5225\u3060\u3051\u3067\u4f5c\u3063\u305f\u753b\u9762\u3002"),
    ("segui_phone_settings", "\u96fb\u8a71 / Settings", "Radio",
     "\u8a2d\u5b9a \u2015 LoRa \u7121\u7dda",
     "\u30c8\u30b0\u30eb\u884c\u3068\u5024\u884c\u306e\u307f\u3067\u69cb\u6210\u3002\u4e0b\u6bb5\u306e\u30bb\u30af\u30b7\u30e7\u30f3\u306f\u30b9\u30af\u30ed\u30fc\u30eb\u3067\u898b\u3048\u308b\u72b6\u614b\u3002"),
]


def raw_to_png(raw_path, png_path):
    with open(raw_path, "rb") as f:
        data = f.read()

    if len(data) < 8:
        print(f"[WARN] Raw file too small: {raw_path}")
        return False

    width, height = struct.unpack("<II", data[:8])
    expected = width * height * 4
    if len(data) - 8 < expected:
        print(f"[WARN] Truncated data for {raw_path} "
              f"(got {len(data) - 8}, expected {expected})")
        return False

    bgra = data[8:8 + expected]

    # BGRA in memory -> RGBA for the PNG scannerline, one stride per channel.
    rgba = bytearray(expected)
    rgba[0::4] = bgra[2::4]
    rgba[1::4] = bgra[1::4]
    rgba[2::4] = bgra[0::4]
    rgba[3::4] = bgra[3::4]

    row_bytes = width * 4
    # Each PNG scanline is prefixed with its filter type (0 = None).
    filtered = b"".join(
        b"\x00" + bytes(rgba[y * row_bytes:(y + 1) * row_bytes])
        for y in range(height)
    )

    def chunk(tag, payload):
        crc = zlib.crc32(tag + payload) & 0xFFFFFFFF
        return struct.pack(">I", len(payload)) + tag + payload + struct.pack(">I", crc)

    png = b"".join([
        b"\x89PNG\r\n\x1a\n",
        chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)),
        chunk(b"IDAT", zlib.compress(filtered, 9)),
        chunk(b"IEND", b""),
    ])

    os.makedirs(os.path.dirname(png_path), exist_ok=True)
    with open(png_path, "wb") as f:
        f.write(png)

    print(f"  [PNG] {os.path.basename(png_path)} "
          f"({width}x{height} px, {os.path.getsize(png_path)} bytes)")
    return True


def convert_all(raw_dir=RAW_DIR, out_dir=IMG_OUT_DIR):
    raws = sorted(glob.glob(os.path.join(raw_dir, "*.raw")))
    if not raws:
        print(f"[ERROR] No raw SegUI captures in {raw_dir}.")
        print("        Build them with `make segui-screens`.")
        return 0

    os.makedirs(out_dir, exist_ok=True)
    return sum(
        raw_to_png(r, os.path.join(out_dir, os.path.splitext(os.path.basename(r))[0] + ".png"))
        for r in raws
    )


BAND_CHIPS = [
    ("TABBAR", "24 px", "5 tabs, one line each"),
    ("BODY", "218 px", "the only stretchable band"),
    ("SOFTKEY", "30 px", "3 caption buttons"),
]


def _card(i, entry):
    fname, tab, badge, jp_title, desc = entry
    n, total = i + 1, len(SCREENS)
    return f"""
                <!-- {n}. {tab} -->
                <div class="segui-card">
                    <div class="segui-frame">
                        <img src="b-system/img/segui/{fname}.png"
                             alt="{n}. {jp_title} ({tab})"
                             class="segui-img" width="480" height="272">
                    </div>
                    <div class="segui-card-info">
                        <div class="segui-card-title">
                            <span>{n}. {jp_title}</span>
                            <span class="segui-badge">{badge}</span>
                        </div>
                        <div class="segui-card-sub">{tab}</div>
                        <div class="segui-card-desc">{desc}</div>
                    </div>
                </div>"""


def generate_segui_screens_html():
    chips = "\n".join(
        f'                    <li><b>{name}</b><span>{size}</span><em>{note}</em></li>'
        for name, size, note in BAND_CHIPS
    )
    cards = "\n".join(_card(i, e) for i, e in enumerate(SCREENS))

    return f"""
        {MARKER}
        <section class="audit-card" id="segui-screens">
            <h3><span>µBTRON-SEGUI セグメンテーション UI ギャラリー (Segmentation UI Showcase)</span></h3>
            <p>
                以下の画面は、新規 UI ツールキット <b>µBTRON-SEGUI (SegUI / Segmentation UI)</b> を、実機と同一の描画経路でヘッドレスレンダリングした成果物である。解像度は 480×272 の横長 TFT パネル。埋め込み機器・家電・キオスク・レトロ機を想定した軽量モジュール UI ツールキットで、起動後にヒープを確保しない。
            </p>
            <p>
                SegUI の前提は「画面とは名前付きの水平帯（セグメント）の予算である」。STATUS・TABBAR・TITLE・BODY・SOFTKEY の各帯が幅と高さを決め、このパネルでは TABBAR 24px・BODY 218px・SOFTKEY 30px、つまり chrome は 272px 中 54px（19%）を消費する。帯のうち <b>伸長を許されるのは 1 つだけ</b>で、それは BODY である。フォームファクタの違いはすべてこのテーブルの値の違いで表現され、<code class="mono">#ifdef</code> による分岐は存在しない。
            </p>
            <p style="font-size:0.9em; opacity:0.85; margin-top:0.6rem;">
                The screens below are produced by µBTRON-SEGUI, a lightweight, modular and scalable UI toolkit for embedded systems, home appliances, kiosks and retro machines, driven through the exact same drawing path the target uses. A SegUI screen is a budget of named horizontal bands; on this panel the chrome costs 54 of 272 pixels (TABBAR 24 + SOFTKEY 30) and leaves BODY 218. Exactly one band may stretch, and form factors differ by table values rather than by conditional compilation. This is the smallest budget SegUI ships on, and therefore the only frame that can prove a segment table is honest - the 480×640 portrait handset above is µBTRON-FOMA's territory.
            </p>

            <style>
                .segui-band-list {{
                    display: flex;
                    flex-wrap: wrap;
                    gap: 0.6rem;
                    list-style: none;
                    margin: 1.4rem 0 0 0;
                    padding: 0;
                }}
                .segui-band-list li {{
                    display: flex;
                    align-items: baseline;
                    gap: 0.5rem;
                    background: #f1f5f9;
                    border: 1px solid #cbd5e1;
                    border-left: 4px solid #0057b7;
                    border-radius: 8px;
                    padding: 0.45rem 0.7rem;
                    font-size: 0.82rem;
                    color: #475569;
                }}
                .segui-band-list li b {{
                    color: #003366;
                    font-family: ui-monospace, SFMono-Regular, Menlo, monospace;
                    letter-spacing: 0.04em;
                }}
                .segui-band-list li span {{
                    color: #0284c7;
                    font-weight: 700;
                    font-variant-numeric: tabular-nums;
                }}
                .segui-gallery-grid {{
                    display: grid;
                    grid-template-columns: repeat(auto-fit, minmax(420px, 1fr));
                    gap: 1.8rem;
                    margin-top: 1.8rem;
                }}
                .segui-card {{
                    background: #f8fafc;
                    border: 1px solid #cbd5e1;
                    border-radius: 16px;
                    padding: 1.2rem;
                    display: flex;
                    flex-direction: column;
                    box-shadow: 0 4px 16px rgba(0, 0, 0, 0.04);
                    transition: transform 0.2s ease, box-shadow 0.2s ease;
                }}
                .segui-card:hover {{
                    transform: translateY(-3px);
                    box-shadow: 0 8px 24px rgba(0, 87, 183, 0.12);
                    border-color: #0057b7;
                }}
                .segui-frame {{
                    background: #000;
                    border: 4px solid #1e293b;
                    border-radius: 12px;
                    overflow: hidden;
                    box-shadow: inset 0 0 10px rgba(0,0,0,0.5), 0 4px 12px rgba(0,0,0,0.15);
                    display: flex;
                    justify-content: center;
                }}
                .segui-img {{
                    width: 100%;
                    max-width: 480px;
                    height: auto;
                    display: block;
                    image-rendering: pixelated;
                }}
                .segui-card-info {{
                    width: 100%;
                    margin-top: 1rem;
                    text-align: left;
                }}
                .segui-card-title {{
                    font-size: 1.05rem;
                    font-weight: 700;
                    color: #003366;
                    margin-bottom: 0.2rem;
                    display: flex;
                    align-items: center;
                    justify-content: space-between;
                    gap: 0.6rem;
                }}
                .segui-card-sub {{
                    font-size: 0.78rem;
                    color: #64748b;
                    font-family: ui-monospace, SFMono-Regular, Menlo, monospace;
                    margin-bottom: 0.45rem;
                }}
                .segui-badge {{
                    font-size: 0.75rem;
                    background: #e0f2fe;
                    color: #0284c7;
                    border: 1px solid #bae6fd;
                    padding: 0.15rem 0.5rem;
                    border-radius: 999px;
                    font-weight: 600;
                    white-space: nowrap;
                }}
                .segui-card-desc {{
                    font-size: 0.88rem;
                    color: #475569;
                    line-height: 1.6;
                }}
            </style>

            <ul class="segui-band-list">
{chips}
            </ul>

            <div class="segui-gallery-grid">
{cards}

            </div>
        </section>
"""


def update_mobile_html():
    if not os.path.exists(MOBILE_HTML):
        print(f"[ERROR] mobile.html not found at {MOBILE_HTML}")
        return False

    with open(MOBILE_HTML, "r", encoding="utf-8") as f:
        content = f.read()

    new_section = generate_segui_screens_html().strip()

    # The FOMA injector stops where this one starts, so re-running either
    # script can never eat the other's section.
    pattern = re.compile(
        re.escape(MARKER) + r".*?" + re.escape(TAIL_MARKER), flags=re.DOTALL
    )
    if pattern.search(content):
        updated = pattern.sub(
            lambda m: new_section + "\n\n        " + TAIL_MARKER, content, count=1
        )
    elif TAIL_MARKER in content:
        updated = content.replace(
            TAIL_MARKER, new_section + "\n\n        " + TAIL_MARKER, 1
        )
    elif "</main>" in content:
        updated = content.replace("</main>", new_section + "\n    </main>", 1)
    else:
        print(f"[ERROR] No insertion anchor found in {MOBILE_HTML}")
        return False

    with open(MOBILE_HTML, "w", encoding="utf-8") as f:
        f.write(updated)

    print(f"[OK] {MOBILE_HTML}: {len(SCREENS)} SegUI screens injected.")
    return True


def main():
    print("===============================================================")
    print(" SegUI Raw Frames -> PNG (b-system/img/segui) + mobile.html")
    print("===============================================================")
    n = convert_all(*(sys.argv[1:3] if len(sys.argv) > 1 else ()))
    print(f" Total PNGs written: {n}")
    ok = update_mobile_html()
    print("===============================================================")
    return 0 if n and ok else 1


if __name__ == "__main__":
    sys.exit(main())
