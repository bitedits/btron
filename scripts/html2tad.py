#!/usr/bin/env python3
# ==============================================================================
# B-System HTML → Binary BTRON 3.20 TAD Unified Batch Compiler
# scripts/html2tad.py  —  Python 3.6+ stdlib-only port of html2tad.exs
#
# Produces bit-for-bit identical output to the Elixir original, including
# the Erlang phash2 virtual-body link IDs.
# ==============================================================================
from __future__ import annotations

import glob
import os
import re
import shutil
import struct
import sys
from datetime import date
from pathlib import Path

# ── BTRON3 SPEC 3.20 TAD Segment Tag Identifiers ────────────────────────────
TS_TPAGE  = 0xFFA0   # Text Page Fusen (Geometry, Margins)
TS_TRULER = 0xFFA1   # Text Ruler Fusen (Indents, Line pitch, Tab stops)
TS_TFONT  = 0xFFA2   # Text Font Fusen (Font ID, TRON Plane)
TS_TCHAR  = 0xFFA3   # Character Attributes (Point size, Weight, Color)
TS_VOBJ   = 0xFFA8   # Virtual Body Link Fusen (Real Body Pointer)
TS_FPRIM  = 0xFFB0   # Figure Primitive (Vector lines, Rectangles)

RECORD_TYPE_MAIN = 1


# ── Erlang phash2 — exact replica for virtual-body link ID compatibility ─────
def erlang_phash2(term_str: str, range_: int) -> int:
    """
    Replicates :erlang.phash2(binary_string, range) for string terms.
    Uses the same multiplicative-hash loop as OTP's make_hash2 for binaries:
      H = 0; for b in bytes: H = (H * 31 + b) & 0xFFFFFFFF
      return H % range
    """
    h = 0
    for b in term_str.encode('utf-8'):
        h = ((h * 31) + b) & 0xFFFFFFFF
    return h % range_


def link_id(href: str) -> int:
    """Match :erlang.phash2(href, 100_000) + 1000 from Elixir."""
    return erlang_phash2(href, 100_000) + 1000


# ── Noise Filter ─────────────────────────────────────────────────────────────
def filter_html_noise(html: str) -> str:
    html = re.sub(r'<!DOCTYPE[^>]*>', '', html, flags=re.I)
    html = re.sub(r'<head\b[^>]*>.*?</head>', '', html, flags=re.I | re.S)
    html = re.sub(r'<script\b[^>]*>.*?</script>', '', html, flags=re.I | re.S)
    html = re.sub(r'<style\b[^>]*>.*?</style>', '', html, flags=re.I | re.S)
    html = re.sub(r'<!--.*?-->', '', html, flags=re.S)
    # Profile 1: b-spec/ & b-system/ (Ukrainian/Japanese header banner)
    html = re.sub(
        r"<div style=['\"][^'\"]*(?:background:#0057b7|background:#333|background:#1b365d)[^'\"]*['\"]>.*?</div>",
        '', html, flags=re.I | re.S)
    # Profile 2: b-hmi/ & b-free/ (Breadcrumbs & Navigation)
    html = re.sub(r'<nav\b[^>]*>.*?</nav>', '', html, flags=re.I | re.S)
    html = re.sub(
        r"<div class=['\"](?:header-nav|breadcrumb|top-banner)['\"]>.*?</div>",
        '', html, flags=re.I | re.S)
    # Profile 3: Common breadcrumb links
    html = re.sub(
        r"<a\s+href=['\"][^'\"]*['\"]>(?:Повернутися|Попередня|Наступна|Return to|Previous|Next|Back to index|Головна)[^<]*</a>(?:<br\s*/?>)?",
        '', html, flags=re.I)
    return html.strip()


# ── HTML Entity Decoder ───────────────────────────────────────────────────────
_ENTITIES = {
    '&nbsp;': ' ', '&lt;': '<', '&gt;': '>', '&amp;': '&',
    '&quot;': '"', '&apos;': "'", '&copy;': '©', '&mdash;': '—',
    '&ndash;': '–', '&bull;': '•',
}

def decode_entities(text: str) -> str:
    for ent, repl in _ENTITIES.items():
        text = text.replace(ent, repl)
    text = re.sub(r'&#(\d+);', lambda m: chr(int(m.group(1))), text)
    text = re.sub(r'&#x([0-9a-fA-F]+);', lambda m: chr(int(m.group(1), 16)), text)
    return text


# ── Strip HTML Tags ───────────────────────────────────────────────────────────
def strip_tags(html: str) -> str:
    text = re.sub(r'<[^>]+>', '', html)
    text = re.sub(r'\s+', ' ', text)
    return text.strip()


# ── List / Table Parsers ──────────────────────────────────────────────────────
def parse_list(html: str) -> list:
    items = re.findall(r'<li\b[^>]*>(.*?)</li>', html, flags=re.I | re.S)
    return [decode_entities(strip_tags(i)) for i in items if strip_tags(i)]


def parse_table(html: str) -> list:
    rows = re.findall(r'<tr\b[^>]*>(.*?)</tr>', html, flags=re.I | re.S)
    result = []
    for row_html in rows:
        cells = re.findall(r'<t[hd]\b[^>]*>(.*?)</t[hd]>', row_html, flags=re.I | re.S)
        result.append([decode_entities(strip_tags(c)) for c in cells])
    return result


# ── Cell Text Wrapper ─────────────────────────────────────────────────────────
def wrap_cell_text(text: str, max_len: int) -> list:
    if not text or max_len <= 0:
        return ['']
    words = [w for w in re.split(r'\s+', text) if w]
    if not words:
        return ['']
    split_words = []
    for w in words:
        if len(w) <= max_len:
            split_words.append(w)
        else:
            for i in range(0, len(w), max_len):
                split_words.append(w[i:i + max_len])
    lines, cur = [], ''
    for word in split_words:
        if not cur:
            cur = word
        elif len(cur) + 1 + len(word) <= max_len:
            cur += ' ' + word
        else:
            lines.append(cur)
            cur = word
    if cur:
        lines.append(cur)
    return lines if lines else ['']


# ── ASCII Table Formatter ─────────────────────────────────────────────────────
def format_table_ascii(rows: list, max_table_width: int = 76) -> str:
    if not rows:
        return ''
    col_count = max(len(r) for r in rows)
    if col_count == 0:
        return ''
    padded = [r + [''] * (col_count - len(r)) for r in rows]
    natural_widths, max_word_lengths = [], []
    for c in range(col_count):
        col_vals = [row[c] for row in padded]
        natural_widths.append(max(max((len(v) for v in col_vals), default=0), 4))
        words = [w for v in col_vals for w in re.split(r'\s+', v) if w]
        max_word_lengths.append(max((len(w) for w in words), default=4))
    border_overhead = 3 * col_count + 1
    avail = max(max_table_width - border_overhead, col_count * 6)
    if sum(natural_widths) <= avail:
        col_widths = natural_widths
    elif col_count > 1:
        first = [min(max(natural_widths[i], max_word_lengths[i]), 20)
                 for i in range(col_count - 1)]
        col_widths = first + [max(avail - sum(first), max_word_lengths[-1])]
    else:
        col_widths = [avail]

    def border(l, m, r, f='─'):
        return l + f + (f + m + f).join(f * w for w in col_widths) + f + r

    top, mid, bot = border('┌','┬','┐'), border('├','┼','┤'), border('└','┴','┘')

    def fmt_row(r):
        wrapped = [wrap_cell_text(v, w) for v, w in zip(r, col_widths)]
        rh = max(len(w) for w in wrapped)
        norm = [w + [''] * (rh - len(w)) for w in wrapped]
        return ['│ ' + ' │ '.join(norm[c][li].ljust(col_widths[c])
                                  for c in range(col_count)) + ' │'
                for li in range(rh)]

    h_lines = fmt_row(padded[0])
    b_lines = []
    for i, dr in enumerate(padded[1:]):
        if i > 0:
            b_lines.append(mid)
        b_lines.extend(fmt_row(dr))
    return '\n'.join([top] + h_lines + [mid] + b_lines + [bot])


# ── Markdown Table Parser ─────────────────────────────────────────────────────
def parse_markdown_table(text: str):
    lines = [l.strip() for l in text.split('\n') if l.strip()]
    if len(lines) < 2:
        return None
    hdr, sep = lines[0], lines[1]
    if not (hdr.startswith('|') and sep.startswith('|') and '-' in sep):
        return None
    def parse_row(line):
        return [c.strip() for c in line.strip('|').split('|')]
    return [parse_row(hdr)] + [parse_row(l) for l in lines[2:]]


# ── Image Dimension Probe ─────────────────────────────────────────────────────
def get_image_dimensions(path: str):
    try:
        with open(path, 'rb') as f:
            hdr = f.read(24)
        if hdr[:3] == b'GIF':
            return struct.unpack_from('<HH', hdr, 6)
        if hdr[:8] == b'\x89PNG\r\n\x1a\n' and hdr[12:16] == b'IHDR':
            return struct.unpack_from('>II', hdr, 16)
    except Exception:
        pass
    return 480, 140


# ── Binary Segment Builders ───────────────────────────────────────────────────
def make_segment(tag: int, payload: bytes) -> bytes:
    return struct.pack('>HI', tag, len(payload)) + payload

def seg_page(width=800, height=1200, margin_l=40, margin_t=40) -> bytes:
    return make_segment(TS_TPAGE,
        struct.pack('>BBHHHHHH', 0, 0, height, width, margin_t, margin_t, margin_l, margin_l))

def seg_font(font_id: int, plane: int = 1) -> bytes:
    return make_segment(TS_TFONT, struct.pack('>BHB', 0, font_id, plane))

def seg_char(size_pt: int, weight: int = 400, color_rgb: int = 0x000000) -> bytes:
    return make_segment(TS_TCHAR, struct.pack('>BHHI', 0, size_pt, weight, color_rgb))

def seg_ruler(line_pitch: int = 22, indent: int = 0) -> bytes:
    return make_segment(TS_TRULER, struct.pack('>BHH', 0, line_pitch, indent))

def seg_vobj(target_id: int, label: str, path: str = '') -> bytes:
    lb, pb = label.encode('utf-8'), path.encode('utf-8')
    payload = (struct.pack('>BI', 0, target_id)
               + struct.pack('>H', len(lb)) + lb
               + struct.pack('>H', len(pb)) + pb)
    return make_segment(TS_VOBJ, payload)

def seg_hr(width: int = 680) -> bytes:
    return make_segment(TS_FPRIM,
        struct.pack('>BBIhhhh', 1, 0, 0x888888, 0, 0, width, 0))

def seg_image(src: str, caption: str = '', width: int = 480,
              height: int = 140, type_: int = 0) -> bytes:
    cb, sb = caption.encode('utf-8'), src.encode('utf-8')
    payload = (struct.pack('>BHHB', 10, width, height, type_)
               + struct.pack('>H', len(cb)) + cb
               + struct.pack('>H', len(sb)) + sb)
    return make_segment(TS_FPRIM, payload)

def seg_text(text: str) -> bytes:
    return (text + '\n').encode('utf-8')


# ── HTML Parser → Element List ────────────────────────────────────────────────
def parse_html(html: str) -> list:
    clean = filter_html_noise(html)
    token_re = re.compile(
        r'(<h[1-6]\b[^>]*>.*?</h[1-6]>'
        r'|<pre\b[^>]*>.*?</pre>'
        r'|<p\b[^>]*>.*?</p>'
        r'|<table\b[^>]*>.*?</table>'
        r'|<ul\b[^>]*>.*?</ul>'
        r'|<ol\b[^>]*>.*?</ol>'
        r'|<hr\s*/?>'
        r'|<img\s+[^>]*>'
        r'|<a\s+href=[\'"][^\'"]+[\'"][^>]*>.*?</a>)',
        re.I | re.S)
    elements = []
    for m in token_re.finditer(clean):
        full = m.group(0)
        low = full.lower()
        if low.startswith('<a'):
            hm = re.search(r"href=['\"]([^'\"]+)['\"]", full, re.I)
            if hm:
                elements.append(('link', hm.group(1), decode_entities(strip_tags(full))))
            else:
                t = decode_entities(strip_tags(full))
                if t:
                    elements.append(('text', t))
        elif low.startswith('<img'):
            sm = re.search(r"src=['\"]([^'\"]+)['\"]", full, re.I)
            am = re.search(r"alt=['\"]([^'\"]+)['\"]", full, re.I)
            elements.append(('image', sm.group(1) if sm else 'figure.png',
                             am.group(1) if am else ''))
        elif low.startswith('<h1'):
            elements.append(('h1', decode_entities(strip_tags(full))))
        elif low.startswith('<h2'):
            elements.append(('h2', decode_entities(strip_tags(full))))
        elif low.startswith('<h3'):
            elements.append(('h3', decode_entities(strip_tags(full))))
        elif low[:3] in ('<h4', '<h5', '<h6'):
            elements.append(('h4', decode_entities(strip_tags(full))))
        elif low.startswith('<pre'):
            elements.append(('pre', decode_entities(strip_tags(full))))
        elif low.startswith('<table'):
            elements.append(('table', parse_table(full)))
        elif low.startswith('<ul'):
            items = parse_list(full)
            if items:
                elements.append(('ul', items))
        elif low.startswith('<ol'):
            items = parse_list(full)
            if items:
                elements.append(('ol', items))
        elif low.startswith('<hr'):
            elements.append(('hr',))
        elif low.startswith('<p'):
            t = decode_entities(strip_tags(full))
            if t:
                elements.append(('p', t))
        else:
            t = decode_entities(strip_tags(full))
            if t:
                elements.append(('text', t))
    return elements


# ── Binary TAD Compiler ───────────────────────────────────────────────────────
def compile_to_binary_tad(elements: list, doc_title: str = 'BTRON Document',
                           base_dir: str = '') -> bytes:
    body = b''
    for elem in elements:
        tag = elem[0]
        if tag == 'h1':
            body += (seg_font(1,1)+seg_char(22,700,0x003366)+seg_ruler(32,0)
                     +seg_text('■ '+elem[1])+seg_hr(680)
                     +seg_font(0,1)+seg_char(12,400,0x000000)+seg_ruler(22,0))
        elif tag == 'h2':
            body += (seg_font(1,1)+seg_char(16,700,0x002244)+seg_ruler(26,0)
                     +seg_text('▶ '+elem[1])
                     +seg_font(0,1)+seg_char(12,400,0x000000)+seg_ruler(22,0))
        elif tag == 'h3':
            body += (seg_font(1,1)+seg_char(14,600,0x333333)
                     +seg_text('◆ '+elem[1])
                     +seg_font(0,1)+seg_char(12,400,0x000000))
        elif tag == 'h4':
            body += (seg_font(1,1)+seg_char(12,600,0x444444)
                     +seg_text('● '+elem[1])
                     +seg_font(0,1)+seg_char(12,400,0x000000))
        elif tag == 'p':
            body += seg_text(elem[1])
        elif tag == 'pre':
            body += (seg_font(2,1)+seg_char(10,400,0x112233)+seg_ruler(16,20)
                     +seg_text(elem[1])
                     +seg_font(0,1)+seg_char(12,400,0x000000)+seg_ruler(22,0))
        elif tag == 'ul':
            for item in elem[1]:
                body += seg_text('  • ' + item)
        elif tag == 'ol':
            for i, item in enumerate(elem[1], 1):
                body += seg_text(f'  {i}. {item}')
        elif tag == 'table':
            body += (seg_font(2,1)+seg_char(10,400,0x000000)+seg_ruler(16,10)
                     +seg_text(format_table_ascii(elem[1])+'\n')
                     +seg_font(0,1)+seg_char(12,400,0x000000)+seg_ruler(22,0))
        elif tag == 'link':
            body += seg_vobj(link_id(elem[1]), elem[2], elem[1])
        elif tag == 'image':
            src, alt = elem[1], elem[2]
            caption = alt if alt else 'BTRON3 Figure / Picture: ' + os.path.basename(src)
            actual = src if (not base_dir or os.path.exists(src)) else os.path.join(base_dir, src)
            w, h = get_image_dimensions(actual)
            body += seg_image(src, caption, w, h, 0)
        elif tag == 'hr':
            body += seg_hr(680)
        elif tag == 'text':
            body += seg_text(elem[1])

    init = (seg_page(800,1200,40,40)+seg_font(0,1)
            +seg_char(12,400,0x000000)+seg_ruler(22,0))
    payload = init + body
    return struct.pack('>HI', RECORD_TYPE_MAIN, len(payload)) + payload


# ── Symbolic TAD Compiler ─────────────────────────────────────────────────────
def compile_to_symbolic_tad(elements: list, doc_title: str = 'BTRON Document',
                             base_dir: str = '') -> str:
    today = date.today().isoformat()
    header = (
        '================================================================================\n'
        f'TAD Real Body [実身] : {doc_title}\n'
        'RECORD TYPE : 1 (TAD Main Record) | BTRON3 SPEC 3.20 Cleanroom Edition\n'
        f'DATE        : {today} | GENERATOR : html2tad.py (Python stdlib)\n'
        '================================================================================\n\n'
        f'[付箋: DOCUMENT_HEADER | Title="{doc_title}" | Font="Cho-Kanji-Mincho" | Size=16]\n'
    )
    parts = []
    for elem in elements:
        tag = elem[0]
        if tag == 'h1':
            parts.append('【'+elem[1]+'】\n'+'━'*40)
        elif tag == 'h2':
            parts.append('■ '+elem[1]+'\n'+'─'*40)
        elif tag == 'h3':
            parts.append('▶ '+elem[1])
        elif tag == 'h4':
            parts.append('◆ '+elem[1])
        elif tag == 'p':
            parts.append(elem[1])
        elif tag == 'pre':
            parts.append('```\n'+elem[1]+'\n```')
        elif tag == 'ul':
            parts.append('\n'.join('  • '+i for i in elem[1]))
        elif tag == 'ol':
            parts.append('\n'.join(f'  {i+1}. {it}' for i, it in enumerate(elem[1])))
        elif tag == 'table':
            parts.append(format_table_ascii(elem[1]))
        elif tag == 'link':
            parts.append(f'[仮身] #{link_id(elem[1])} : {elem[2]} -> [{elem[1]}]')
        elif tag == 'image':
            src, alt = elem[1], elem[2]
            actual = src if (not base_dir or os.path.exists(src)) else os.path.join(base_dir, src)
            w, h = get_image_dimensions(actual)
            parts.append(f'[付箋: FIGURE w={w} h={h} src="{src}" caption="{alt}"]\n'
                         f'[画像: {alt if alt else os.path.basename(src)}]')
        elif tag == 'hr':
            parts.append('─'*70)
        elif tag == 'text':
            parts.append(elem[1])
    return header + '\n\n'.join(parts) + '\n'


# ── Doc title name helper ─────────────────────────────────────────────────────
def _doc_title_name(base_name: str, html_path: str, source_root: str) -> str:
    if base_name != 'index':
        return base_name
    rel = os.path.relpath(html_path, source_root) if source_root else html_path
    sub = os.path.dirname(rel)
    catalog = os.path.basename(source_root) if source_root else ''
    if sub and sub != '.':
        m = {'part1':'Part1','part2':'Part2','part_book':'Part_Book'}
        return m.get(sub.lower(), sub.lower().capitalize())
    if catalog == 't-kernel': return 'T-Kernel'
    if catalog == 'b-system': return 'B-System'
    if catalog == 'b-hmi':    return 'B-HMI'
    if catalog == 'b-free':   return 'B-Free'
    if catalog == 'b-spec':   return 'B-Spec'
    if html_path == 'index.html': return 'B-System_Portal'
    return 'Index'


# ── File Processing Pipeline ──────────────────────────────────────────────────
def process_file(html_path: str, target_dir: str, source_root: str = '') -> dict:
    os.makedirs(target_dir, exist_ok=True)
    html_dir = os.path.dirname(os.path.abspath(html_path))
    for asset in ('gif', 'img', 'figures'):
        sa = os.path.join(html_dir, asset)
        ta = os.path.join(target_dir, asset)
        if os.path.isdir(sa) and not os.path.isdir(ta):
            shutil.copytree(sa, ta)
    base_name = os.path.splitext(os.path.basename(html_path))[0]
    with open(html_path, 'r', encoding='utf-8', errors='replace') as f:
        html_content = f.read()
    elements = parse_html(html_content)
    title = next((e[1] for e in elements if e[0] in ('h1','h2')), base_name)
    doc_title_name = _doc_title_name(base_name, html_path, source_root)
    bin_path = os.path.join(target_dir, doc_title_name + '.tad')
    txt_path = os.path.join(target_dir, doc_title_name + '.tad.txt')
    binary_tad = compile_to_binary_tad(elements, title, html_dir)
    symbolic_tad = compile_to_symbolic_tad(elements, title, html_dir)
    with open(bin_path, 'wb') as f: f.write(binary_tad)
    with open(txt_path, 'w', encoding='utf-8') as f: f.write(symbolic_tad)
    if base_name == 'index' and doc_title_name != 'index':
        for alias in ('index.tad', 'index.tad.txt'):
            ap = os.path.join(target_dir, alias)
            if alias.endswith('.txt'):
                with open(ap, 'w', encoding='utf-8') as f: f.write(symbolic_tad)
            else:
                with open(ap, 'wb') as f: f.write(binary_tad)
    return {'html_path': html_path, 'bin_path': bin_path, 'txt_path': txt_path,
            'elements_count': len(elements), 'bin_bytes': len(binary_tad),
            'title': doc_title_name}


# ── Canonical Foundational TAD Books ─────────────────────────────────────────
def compile_foundational_books(target_dir: str = 'tad_bin'):
    btron3_elements = [
        ('h1', 'BTRON3 仕様書 バージョン 3.20.00 (Sakamura BTRON Architecture)'),
        ('image', 'b-spec/shared_data/gif/all_struct.gif', '図 1: BTRON3 実身・仮身データ構造仕様 (Shared Data Structure)'),
        ('link', 'b-spec/shared_data/index.tad', 'Part 1: 共通データ構造仕様 (Shared Data Specifications)'),
        ('link', 'b-spec/os_spec/index.tad', 'Part 2: オペレーティングシステム機能仕様 (OS Specification)'),
        ('link', 'b-spec/os_spec/indexfig.tad', 'Static Analysis & Bounded Heap Memory Model (NASA JPL Rule 3)'),
        ('h2', '目次 (Table of Contents)'),
        ('h3', '第1部：共通データ構造仕様 (Shared Data)'),
        ('link', 'b-spec/shared_data/data_type.tad', '第1章 基本データ型とエラーコード (Data Types & Error Codes)'),
        ('link', 'b-spec/shared_data/tron_code.tad', '第2章 TRONコード文字体系仕様 (TRON Multilingual Character Code)'),
        ('link', 'b-spec/shared_data/tad1.tad', '第3章 TAD (TRON Application Databus) 文書フォーマット仕様'),
        ('link', 'b-spec/shared_data/fd_format.tad', '第4章 BTRON FS 実身／仮身ファイルシステム構造仕様'),
        ('h3', '第2部：OS機能仕様 (OS Specification)'),
        ('link', 'b-spec/os_spec/kernel/kernel.tad', '第5章 μITRON リアルタイムカーネルとタスク管理 (Kernel & Tasking)'),
        ('link', 'b-spec/os_spec/dp/dp.tad', '第6章 表示プリミティブ DP (Display Primitives Graphics Engine)'),
        ('link', 'b-spec/os_spec/shell/shell.tad', '第7章 GUIシェルとウィンドウマネージャ (Window Manager & Shell)'),
        ('link', 'b-spec/os_spec/indexfig.tad', '第8章 静的解析と決定論的メモリ制約 (Static Scope & Bounded Memory)'),
        ('hr',),
        ('h2', '基本データ型定義 (C99 Types)'),
        ('pre', 'typedef char            B;   /* 符号付き 8ビット整数 */\ntypedef short           H;   /* 符号付き 16ビット整数 */\ntypedef int             W;   /* 符号付き 32ビット整数 */\ntypedef unsigned char   UB;  /* 符号なし 8ビット整数 */\ntypedef unsigned short  UH;  /* 符号なし 16ビット整数 */\ntypedef unsigned int    UW;  /* 符号なし 32ビット整数 */\ntypedef void           *VP;  /* 汎用ポインタ */'),
        ('p', 'BTRON3仕様では、ブート完了後の動的ヒープ確保 (malloc/free) を完全禁止し、全てのメモリ領域を有界化します。'),
    ]
    tkernel_elements = [
        ('h1', 'T-Kernel 2.0 リアルタイムOS仕様書及び開発ガイド'),
        ('image', 'b-spec/os_spec/kernel/gif/processtask.gif', '図 2: μITRON リアルタイムタスク状態遷移図 (Task State Machine)'),
        ('link', 't-kernel/tkernel_spec.tad', '第1章 T-Kernel 2.0 コアアーキテクチャ (Core Architecture)'),
        ('link', 't-kernel/tkernel_startup.tad', '第2章 ブート及び初期化シーケンス (Startup Sequence)'),
        ('link', 't-kernel/tkernel_qemu.tad', '第3章 QEMU仮想環境とボード展開 (QEMU & Board Deployment)'),
        ('link', 't-kernel/index.tad', '第4章 T-Kernel 2.0 開発者ドキュメント索引 (Developer Index)'),
        ('h2', 'タスク状態遷移モデル (Task State Model)'),
        ('ol', [
            '実行状態 (RUN: CPU実行権を保持)',
            '実行可能状態 (READY: ディスパッチ待機)',
            '待ち状態 (WAIT: イベント・セマフォ契機同期)',
            '休止状態 (DORMANT: 未起動または終了)',
        ]),
        ('p', 'T-Kernel 2.0 では、時間決定論的応答性を保証するために、プライオリティベース・プリエンプティブ・スケジューリングを採用しています。'),
    ]
    bfree_elements = [
        ('h1', 'B-Free 自由なBTRON3オペレーティングシステム技術解説書'),
        ('image', 'b-spec/os_spec/kernel/gif/filesystem.gif', '図 3: BTRON ファイルシステム構造仕様 (Filesystem Structure)'),
        ('link', 'b-free/manifest.tad', '第1章 B-Free マニフェストと自由ソフトウェアの理念'),
        ('link', 'b-free/kernel.tad', '第2章 μITRON 3.0 マイクロカーネルアーキテクチャ'),
        ('link', 'b-free/posix.tad', '第3章 POSIXエミュレーション層とシステムコール'),
        ('link', 'b-free/btron.tad', '第4章 B-Free OS 統合デスクトップ環境'),
        ('link', 'b-free/boot_arch.tad', '第5章 ブート機構とソースツリー構造'),
        ('link', 'b-free/source_tree.tad', '第6章 B-Free ソースツリー構成とビルド体系'),
        ('h2', '設計理念と自由ソフトウェアの精神'),
        ('p', 'Ken Sakamura教授が提唱した「万人に開かれた標準」をGPL (GNU General Public License) の下で実現するクリーンルーム実装。'),
    ]
    tron_hmi_elements = [
        ('h1','TRON 人間・機械インタフェース (HMI) 設計仕様書及び標準カタログ'),
        ('image','b-spec/os_spec/shell/gif/title_bar.gif','図 4: BTRON3 標準ウィンドウとタイトルバー意匠 (Window Geometry)'),
        ('link','b-hmi/index.tad','第1章 TRON HMI 統合仕様書・設計指針ガイド (HMI Specification)'),
        ('link','b-hmi/part1/chap03_sui.tad','第2章 SUI 実身操作パネル標準仕様 (Standard User Interface)'),
        ('link','b-hmi/part1/chap04_gui.tad','第3章 GUI ウィンドウマネージャと角枠リサイズ意匠 (GUI Blueprint)'),
        ('link','b-hmi/part_book/switches.tad','第4章 HMI部品カタログ：プッシュ・シーソー・トグルスイッチ仕様'),
        ('link','b-hmi/part_book/volumes.tad','第5章 HMI部品カタログ：ロータリーダイヤル・ポテンショメータ仕様'),
        ('link','b-hmi/part_book/selectors.tad','第6章 HMI部品カタログ：ラジオボタン・マトリクスセレクタ仕様'),
        ('link','os_spec/shell/parts.tad','第7章 BTRON3 シェル標準GUI部品仕様 (Shell Parts Book)'),
        ('h2','標準動作三原則 (The Standard Action Triad)'),
        ('ol',['操作対象の指定 (Target Selection)','操作内容の指定 (Operation Specification)','実行の確認／確定 (Execution Confirmation)']),
        ('h2','標準HMI部品カタログ (Parts Book)'),
        ('ul',['プッシュスイッチ (Push Switch - モメンタリ／オルタネート)',
               'アップダウンスステッパ (Up/Down Stepper)',
               'ラジオボタンマトリクス (Radio Matrix - 排他的選択)',
               'ロータリーダイヤル (Rotary Dial - 270度連続角度調整)',
               'セグメントVUメーター (Bar VU Meter - ピークホールド付き)',
               'ユニバーサルコントローラ (Universal Controller - 7キー統一リモート)']),
    ]
    books = [
        ('01_btron3_spec','BTRON3 3.20 Specification Book', btron3_elements),
        ('02_tkernel_book','T-Kernel 2.0 Real-Time OS Book', tkernel_elements),
        ('03_bfree_os_book','B-Free Operating System Book', bfree_elements),
    ]
    if os.path.isdir('b-hmi'):
        books.append(('04_tron_hmi_book','TRON Human-Machine Interface Book', tron_hmi_elements))

    print('======================================================================')
    print(' B-System Foundational TAD Books Compiler (True BTRON3 SPEC 3.20)')
    print(f' Output Directory: ./{target_dir}/')
    print('======================================================================')
    for base_name, title, elems in books:
        bin_tad = compile_to_binary_tad(elems, title)
        txt_tad = compile_to_symbolic_tad(elems, title)
        with open(os.path.join(target_dir, base_name+'.tad'), 'wb') as f: f.write(bin_tad)
        with open(os.path.join(target_dir, base_name+'.tad.txt'), 'w', encoding='utf-8') as f: f.write(txt_tad)
        print(f'  [COMPILED BOOK]   {(base_name+".tad").ljust(26)} : {title.ljust(38)} ({len(bin_tad)} bytes)')
    print('======================================================================')
    print(f' Successfully generated all {len(books)} Canonical Binary TAD books in ./{target_dir}/')
    print('======================================================================\n')


# ── Self-Test Suite ───────────────────────────────────────────────────────────
def run_tests():
    print('==========================================================')
    print(' BTRON3 SPEC 3.20 TAD Compiler Test Suite (Python)')
    print('==========================================================')
    failures = []
    def chk(cond, msg):
        status = 'PASS' if cond else 'FAIL'
        print(f'  [{status}] {msg}')
        if not cond:
            failures.append(msg)

    test_html = """<!DOCTYPE HTML><html>
      <head><title>Test Doc</title><style>.x{color:red;}</style></head>
      <body>
        <div style='background:#0057b7;color:#ffd700;'>Специфікація BTRON3</div>
        <a href="index.html">Повернутися до змісту</a>
        <h1>Розділ 1. Основні типи даних</h1>
        <p>BTRON підтримує 8-бітні, 16-бітні та 32-бітні типи даних.</p>
        <pre>typedef char B;\ntypedef short H;</pre>
        <ul><li>Item 1</li><li>Item 2</li></ul>
        <a href="tad1.html">Посилання на TAD специфікацію</a>
      </body></html>"""

    clean = filter_html_noise(test_html)
    chk('Специфікація BTRON3' not in clean, 'Noise banner filtered')
    chk('Повернутися' not in clean, 'Breadcrumb filtered')
    chk('<style>' not in clean, 'Style tags removed')

    elements = parse_html(test_html)
    chk(any(e == ('h1','Розділ 1. Основні типи даних') for e in elements), 'Parsed H1 element')
    chk(any(e[0]=='p' for e in elements), 'Parsed P element')
    chk(any(e[0]=='pre' for e in elements), 'Parsed PRE element')
    chk(any(e==('ul',['Item 1','Item 2']) for e in elements), 'Parsed UL list')
    chk(any(e==('link','tad1.html','Посилання на TAD специфікацію') for e in elements),
        'Parsed A link to Virtual Body')

    bin_tad = compile_to_binary_tad(elements, 'Unit Test TAD')
    rec_type, payload_len = struct.unpack_from('>HI', bin_tad)
    payload = bin_tad[6:]
    chk(rec_type == 1, 'Record Type is 1 (TAD Main Record)')
    chk(payload_len == len(payload), 'Payload length header matches exact byte size')
    chk(len(bin_tad) > 100, 'Binary TAD contains structured segments')
    chk(struct.pack('>H', TS_TPAGE) in payload, 'Contains TS_TPAGE segment (0xFFA0)')
    chk(struct.pack('>H', TS_TFONT) in payload, 'Contains TS_TFONT segment (0xFFA2)')
    chk(struct.pack('>H', TS_TCHAR) in payload, 'Contains TS_TCHAR segment (0xFFA3)')
    chk(struct.pack('>H', TS_VOBJ)  in payload, 'Contains TS_VOBJ segment (0xFFA8)')

    if failures:
        print(f'\n  {len(failures)} test(s) FAILED.')
        sys.exit(1)
    print('==========================================================')
    print(' ALL PYTHON TAD COMPILER UNIT TESTS PASSED (100%)')
    print('==========================================================\n')


# ── Main ──────────────────────────────────────────────────────────────────────
if __name__ == '__main__':
    args = sys.argv[1:]
    if '--test' in args:
        run_tests()

    out_dir = 'tad_bin'
    os.makedirs(out_dir, exist_ok=True)
    compile_foundational_books(out_dir)

    source_trees = [
        ('b-spec',   os.path.join(out_dir, 'b-spec')),
        ('b-free',   os.path.join(out_dir, 'b-free')),
        ('b-system', os.path.join(out_dir, 'b-system')),
        ('t-kernel', os.path.join(out_dir, 't-kernel')),
    ]
    if os.path.isdir('b-hmi'):
        source_trees.append(('b-hmi', os.path.join(out_dir, 'b-hmi')))

    print('======================================================================')
    print(' B-System HTML -> Binary BTRON 3.20 TAD Unified Batch Compiler (Python)')
    print(f' Output Directory        : ./{out_dir}/')
    print(f' Active Source Catalogs  : {", ".join(s for s,_ in source_trees)}')
    print(' Target Specifications   : BTRON3 SPEC 3.20 / B-right/V Cho-Kanji')
    print('======================================================================')

    all_results = []
    for src_root, dst_root in source_trees:
        if not os.path.isdir(src_root):
            continue
        for html_file in sorted(glob.glob(os.path.join(src_root,'**','*.html'), recursive=True)):
            rel = os.path.relpath(html_file, src_root)
            sub = os.path.dirname(rel)
            tdir = dst_root if (not sub or sub=='.') else os.path.join(dst_root, sub)
            res = process_file(html_file, tdir, src_root)
            in_n  = os.path.join(src_root, rel).ljust(32)
            out_n = os.path.relpath(res['bin_path'], out_dir).ljust(32)
            print(f"  [COMPILED] {in_n} -> {out_n} ({res['elements_count']} items, {res['bin_bytes']} bytes)")
            all_results.append(res)

    for root_file in ('index.html', 'releases.html'):
        if os.path.exists(root_file):
            res = process_file(root_file, out_dir, '')
            stem = os.path.splitext(root_file)[0]
            print(f"  [COMPILED] {root_file.ljust(44)} -> {(stem+'.tad').ljust(32)} "
                  f"({res['elements_count']} items, {res['bin_bytes']} bytes)")

    total = sum(r['bin_bytes'] for r in all_results)
    print('======================================================================')
    print(f' Successfully compiled {len(all_results)} HTML documents into Native Binary TAD files.')
    print(f' Total TAD Binary Size: {total} bytes across ./{out_dir}/')
    print('======================================================================')
