#!/usr/bin/env python3
# ==============================================================================
# B-Book Specialized TAD Compiler: scripts/book2tad.py
# Python 3.6+ stdlib-only port of book2tad.exs
# Compiles B-Book developer manual HTML into native BTRON 3.20 TAD files.
# Preserves C99 code layout, ASCII diagrams, API cards, callouts, metadata.
# ==============================================================================
from __future__ import annotations

import glob
import os
import re
import struct
import sys
from datetime import date

# ── BTRON3 SPEC 3.20 TAD Segment Tag Identifiers ────────────────────────────
TS_TPAGE  = 0xFFA0
TS_TRULER = 0xFFA1
TS_TFONT  = 0xFFA2
TS_TCHAR  = 0xFFA3
TS_VOBJ   = 0xFFA8
TS_FPRIM  = 0xFFB0
RECORD_TYPE_MAIN = 1


# ── Erlang phash2 replica ─────────────────────────────────────────────────────
def erlang_phash2(term_str: str, range_: int) -> int:
    h = 0
    for b in term_str.encode('utf-8'):
        h = ((h * 31) + b) & 0xFFFFFFFF
    return h % range_

def link_id(href: str) -> int:
    return erlang_phash2(href, 100_000) + 1000


# ── Noise Filter for B-Book ───────────────────────────────────────────────────
def filter_book_noise(html: str) -> str:
    html = re.sub(r'<!DOCTYPE[^>]*>', '', html, flags=re.I)
    html = re.sub(r'<head\b[^>]*>.*?</head>', '', html, flags=re.I | re.S)
    html = re.sub(r'<script\b[^>]*>.*?</script>', '', html, flags=re.I | re.S)
    html = re.sub(r'<style\b[^>]*>.*?</style>', '', html, flags=re.I | re.S)
    html = re.sub(r'<!--.*?-->', '', html, flags=re.S)
    html = re.sub(r'<nav\b[^>]*>.*?</nav>', '', html, flags=re.I | re.S)
    html = re.sub(r"<div class=['\"]breadcrumb['\"]>.*?</div>", '', html, flags=re.I | re.S)
    return html.strip()


# ── Entity Decoder ────────────────────────────────────────────────────────────
_ENTITIES = {
    '&nbsp;':' ','&lt;':'<','&gt;':'>','&amp;':'&','&quot;':'"','&apos;':"'",
    '&times;':'×','&mdash;':'—','&ndash;':'–','&bull;':'•',
    '&mu;':'μ','&larr;':'←','&rarr;':'→',
}
def decode_entities(text: str) -> str:
    for ent, repl in _ENTITIES.items():
        text = text.replace(ent, repl)
    text = re.sub(r'&#(\d+);', lambda m: chr(int(m.group(1))), text)
    text = re.sub(r'&#x([0-9a-fA-F]+);', lambda m: chr(int(m.group(1),16)), text)
    return text

def strip_tags(html: str) -> str:
    text = re.sub(r'<[^>]+>', '', html)
    text = re.sub(r'[ \t]+', ' ', text)
    return text.strip()

def clean_code(html: str) -> str:
    html = re.sub(r'^<code\b[^>]*>', '', html, flags=re.I)
    html = re.sub(r'</code>$', '', html, flags=re.I)
    return decode_entities(html).strip('\n\r')


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


# ── API Card Parser ───────────────────────────────────────────────────────────
def parse_api_card(card_html: str) -> tuple:
    nm = re.search(r"<span class=['\"]api-name['\"]>(.*?)</span>", card_html, re.I|re.S)
    rt = re.search(r"<span class=['\"]ret-badge['\"]>(.*?)</span>", card_html, re.I|re.S)
    ds = re.search(r'<p>(.*?)</p>', card_html, re.I|re.S)
    sg = re.search(r"<div class=['\"]api-signature['\"]>(.*?)</div>", card_html, re.I|re.S)
    tb = re.search(r'<table\b[^>]*>(.*?)</table>', card_html, re.I|re.S)
    mt = re.search(r"<div class=['\"]api-meta['\"]>(.*?)</div>", card_html, re.I|re.S)

    name     = decode_entities(strip_tags(nm.group(1))) if nm else 'UnknownAPI'
    ret_type = decode_entities(strip_tags(rt.group(1))) if rt else 'void'
    desc     = decode_entities(strip_tags(ds.group(1))) if ds else ''
    sig      = decode_entities(clean_code(strip_tags(sg.group(1)))) if sg else ''
    table    = parse_table(tb.group(0)) if tb else None

    meta = []
    if mt:
        pairs = re.findall(
            r"<span class=['\"]meta-label['\"]>(.*?)</span>\s*<span>(.*?)</span>",
            mt.group(1), re.I|re.S)
        meta = [(decode_entities(strip_tags(l)), decode_entities(strip_tags(v)))
                for l, v in pairs]

    return ('api_card', name, ret_type, desc, sig, table, meta)


# ── Callout Parser ────────────────────────────────────────────────────────────
def parse_callout(html: str) -> tuple:
    tm = re.search(r"<div class=['\"]callout-title['\"]>(.*?)</div>", html, re.I|re.S)
    title = decode_entities(strip_tags(tm.group(1))) if tm else 'Note'
    body_html = re.sub(r"<div class=['\"]callout-title['\"]>.*?</div>", '', html, flags=re.I|re.S)
    body = decode_entities(strip_tags(body_html))
    return ('callout', title, body)


# ── Balanced DIV extractor ────────────────────────────────────────────────────
def extract_balanced_div(html: str):
    """Return (full_block, remaining) or None if not found at offset 0."""
    if not html.lower().startswith('<div'):
        return None
    depth = 0
    pos = 0
    for m in re.finditer(r'</?div\b[^>]*>', html, flags=re.I):
        if m.start() == 0 and depth == 0:
            depth = 1
            pos = m.end()
            continue
        if m.group(0).startswith('</') or m.group(0).lower().startswith('</'):
            depth -= 1
        else:
            depth += 1
        if depth == 0:
            end = m.end()
            return html[:end], html[end:]
    return None


# ── Cell wrapper + ASCII table (same as html2tad.py) ─────────────────────────
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
                split_words.append(w[i:i+max_len])
    lines, cur = [], ''
    for word in split_words:
        if not cur:
            cur = word
        elif len(cur)+1+len(word) <= max_len:
            cur += ' '+word
        else:
            lines.append(cur); cur = word
    if cur: lines.append(cur)
    return lines if lines else ['']

def format_table_ascii(rows: list, max_table_width: int = 76) -> str:
    if not rows: return ''
    col_count = max(len(r) for r in rows)
    if not col_count: return ''
    padded = [r+['']*(col_count-len(r)) for r in rows]
    nat, mwl = [], []
    for c in range(col_count):
        vals = [row[c] for row in padded]
        nat.append(max(max((len(v) for v in vals),default=0),4))
        words = [w for v in vals for w in re.split(r'\s+',v) if w]
        mwl.append(max((len(w) for w in words),default=4))
    overhead = 3*col_count+1
    avail = max(max_table_width-overhead, col_count*6)
    if sum(nat) <= avail:
        cw = nat
    elif col_count > 1:
        first = [min(max(nat[i],mwl[i]),20) for i in range(col_count-1)]
        cw = first+[max(avail-sum(first),mwl[-1])]
    else:
        cw = [avail]
    def brd(l,m,r,f='─'):
        return l+f+(f+m+f).join(f*w for w in cw)+f+r
    top,mid,bot = brd('┌','┬','┐'),brd('├','┼','┤'),brd('└','┴','┘')
    def fmt(row):
        wrapped=[wrap_cell_text(v,w) for v,w in zip(row,cw)]
        rh=max(len(w) for w in wrapped)
        norm=[w+['']*(rh-len(w)) for w in wrapped]
        return ['│ '+' │ '.join(norm[c][li].ljust(cw[c]) for c in range(col_count))+' │'
                for li in range(rh)]
    hl=fmt(padded[0]); bl=[]
    for i,dr in enumerate(padded[1:]):
        if i: bl.append(mid)
        bl.extend(fmt(dr))
    return '\n'.join([top]+hl+[mid]+bl+[bot])

def parse_markdown_table(text: str):
    lines=[l.strip() for l in text.split('\n') if l.strip()]
    if len(lines)<2: return None
    hdr,sep=lines[0],lines[1]
    if not(hdr.startswith('|') and sep.startswith('|') and '-' in sep): return None
    def pr(line): return [c.strip() for c in line.strip('|').split('|')]
    return [pr(hdr)]+[pr(l) for l in lines[2:]]


# ── Robust Semantic Parser for B-Book ────────────────────────────────────────
def parse_book_html(html: str) -> list:
    clean = filter_book_noise(html)
    elements = []
    remaining = clean
    tag_re = re.compile(
        r'<(?:div\s+class=[\'"](?:api-card|callout\b)'
        r'|h[1-6]|pre|p|table|ul|ol|hr|a\s+href)\b[^>]*>',
        re.I)

    while remaining:
        m = tag_re.search(remaining)
        if m is None:
            md = parse_markdown_table(remaining)
            if md:
                elements.append(('table', md))
            break

        lead = remaining[:m.start()]
        if lead:
            md = parse_markdown_table(lead)
            if md:
                elements.append(('table', md))

        token = m.group(0)
        low = token.lower()
        sub = remaining[m.start():]

        if 'api-card' in low:
            result = extract_balanced_div(sub)
            if result:
                block, remaining = result
                elements.append(parse_api_card(block))
            else:
                remaining = sub[m.end()-m.start():]
        elif 'callout' in low:
            result = extract_balanced_div(sub)
            if result:
                block, remaining = result
                elements.append(parse_callout(block))
            else:
                remaining = sub[len(token):]
        elif low.startswith('<pre'):
            pm = re.match(r'<pre\b[^>]*>(.*?)</pre>', sub, re.I|re.S)
            if pm:
                code = clean_code(pm.group(1))
                elements.append(('code_block', code))
                remaining = sub[pm.end():]
            else:
                remaining = sub[len(token):]
        elif low.startswith('<table'):
            pm = re.match(r'<table\b[^>]*>.*?</table>', sub, re.I|re.S)
            if pm:
                elements.append(('table', parse_table(pm.group(0))))
                remaining = sub[pm.end():]
            else:
                remaining = sub[len(token):]
        elif re.match(r'<h[1-6]', low):
            n = low[2]
            pm = re.match(fr'<h{n}\b[^>]*>(.*?)</h{n}>', sub, re.I|re.S)
            if pm:
                title = decode_entities(strip_tags(pm.group(1)))
                htype = {'1':'h1','2':'h2','3':'h3'}.get(n,'h4')
                elements.append((htype, title))
                remaining = sub[pm.end():]
            else:
                remaining = sub[len(token):]
        elif low.startswith('<ul'):
            pm = re.match(r'<ul\b[^>]*>.*?</ul>', sub, re.I|re.S)
            if pm:
                items = parse_list(pm.group(0))
                if items: elements.append(('ul', items))
                remaining = sub[pm.end():]
            else:
                remaining = sub[len(token):]
        elif low.startswith('<ol'):
            pm = re.match(r'<ol\b[^>]*>.*?</ol>', sub, re.I|re.S)
            if pm:
                items = parse_list(pm.group(0))
                if items: elements.append(('ol', items))
                remaining = sub[pm.end():]
            else:
                remaining = sub[len(token):]
        elif low.startswith('<hr'):
            pm = re.match(r'<hr\s*/?>', sub, re.I)
            if pm:
                elements.append(('hr',))
                remaining = sub[pm.end():]
            else:
                remaining = sub[len(token):]
        elif low.startswith('<a'):
            pm = re.match(r"<a\s+href=['\"]([^'\"]+)['\"][^>]*>(.*?)</a>", sub, re.I|re.S)
            if pm:
                href  = pm.group(1)
                label = decode_entities(strip_tags(pm.group(2)))
                elements.append(('link', href, label))
                remaining = sub[pm.end():]
            else:
                remaining = sub[len(token):]
        elif low.startswith('<p'):
            pm = re.match(r'<p\b[^>]*>(.*?)</p>', sub, re.I|re.S)
            if pm:
                text = decode_entities(strip_tags(pm.group(1)))
                if text:
                    etype = 'page_lead' if 'page-lead' in low else 'p'
                    elements.append((etype, text))
                remaining = sub[pm.end():]
            else:
                remaining = sub[len(token):]
        else:
            remaining = sub[len(token):]

    return elements


# ── Binary Segment Builders ───────────────────────────────────────────────────
def make_segment(tag: int, payload: bytes) -> bytes:
    return struct.pack('>HI', tag, len(payload)) + payload

def seg_page(w=800,h=1200,ml=40,mt=40) -> bytes:
    return make_segment(TS_TPAGE, struct.pack('>BBHHHHHH',0,0,h,w,mt,mt,ml,ml))
def seg_font(fid,plane=1) -> bytes:
    return make_segment(TS_TFONT, struct.pack('>BHB',0,fid,plane))
def seg_char(sz,wt=400,rgb=0x000000) -> bytes:
    return make_segment(TS_TCHAR, struct.pack('>BHHI',0,sz,wt,rgb))
def seg_ruler(lp=22,ind=0) -> bytes:
    return make_segment(TS_TRULER, struct.pack('>BHH',0,lp,ind))
def seg_vobj(tid,label,path='') -> bytes:
    lb,pb = label.encode('utf-8'),path.encode('utf-8')
    return make_segment(TS_VOBJ,
        struct.pack('>BI',0,tid)+struct.pack('>H',len(lb))+lb+struct.pack('>H',len(pb))+pb)
def seg_hr(width=680) -> bytes:
    return make_segment(TS_FPRIM, struct.pack('>BBIhhhh',1,0,0x888888,0,0,width,0))
def seg_text(text) -> bytes:
    return (text+'\n').encode('utf-8')


# ── Binary TAD Compiler ───────────────────────────────────────────────────────
def compile_to_binary_tad(elements: list) -> bytes:
    init = seg_page()+seg_font(0,1)+seg_char(12,400,0x000000)+seg_ruler(22,0)
    body = b''
    for elem in elements:
        tag = elem[0]
        if tag == 'h1':
            body += (seg_font(1,1)+seg_char(20,700,0x001833)+seg_ruler(30,0)
                     +seg_text('\n■ '+elem[1])+seg_hr(680)
                     +seg_font(0,1)+seg_char(12,400,0x000000)+seg_ruler(22,0))
        elif tag == 'h2':
            body += (seg_font(1,1)+seg_char(16,700,0x003355)+seg_ruler(26,0)
                     +seg_text('\n▶ '+elem[1])+seg_hr(560)
                     +seg_font(0,1)+seg_char(12,400,0x000000)+seg_ruler(22,0))
        elif tag == 'h3':
            body += (seg_font(1,1)+seg_char(13,600,0x004466)+seg_ruler(24,0)
                     +seg_text('\n▼ '+elem[1])
                     +seg_font(0,1)+seg_char(12,400,0x000000)+seg_ruler(22,0))
        elif tag == 'h4':
            body += (seg_font(1,1)+seg_char(12,600,0x444444)+seg_ruler(20,0)
                     +seg_text('● '+elem[1])
                     +seg_font(0,1)+seg_char(12,400,0x000000)+seg_ruler(22,0))
        elif tag == 'api_card':
            _, name, ret_type, desc, sig, table, meta = elem
            body += (seg_font(1,1)+seg_char(14,700,0x005577)+seg_ruler(24,0)
                     +seg_text('\n◆ FUNCTION: '+name+'  ['+ret_type+']'))
            if desc:
                body += (seg_font(0,1)+seg_char(12,400,0x222222)+seg_ruler(20,10)
                         +seg_text('  '+desc))
            if sig:
                body += (seg_font(2,1)+seg_char(11,600,0x0A2540)+seg_ruler(16,20)
                         +seg_text('  ┌─ C99 Prototype ──────────────────────────────────────────\n  │ '
                                   +sig+'\n  └──────────────────────────────────────────────────────────'))
            if table:
                body += (seg_font(2,1)+seg_char(10,400,0x111111)+seg_ruler(15,20)
                         +seg_text(format_table_ascii(table)))
            if meta:
                meta_lines = '\n'.join(f'  • {k}: {v}' for k, v in meta)
                body += (seg_font(0,1)+seg_char(11,500,0x475569)+seg_ruler(18,20)
                         +seg_text(meta_lines))
            body += (seg_hr(680)+seg_font(0,1)+seg_char(12,400,0x000000)+seg_ruler(22,0))
        elif tag == 'callout':
            _, title, cbody = elem
            body += (seg_font(1,1)+seg_char(11,600,0x1E3A5F)+seg_ruler(20,10)
                     +seg_text('┌─ NOTE: '+title+' ─────────────────────────────────────────')
                     +seg_text('│ '+cbody)
                     +seg_text('└──────────────────────────────────────────────────────────')
                     +seg_font(0,1)+seg_char(12,400,0x000000)+seg_ruler(22,0))
        elif tag == 'code_block':
            body += (seg_font(2,1)+seg_char(10,400,0x1E293B)+seg_ruler(16,20)
                     +seg_text(elem[1]+'\n')
                     +seg_font(0,1)+seg_char(12,400,0x000000)+seg_ruler(22,0))
        elif tag == 'table':
            body += (seg_font(2,1)+seg_char(10,400,0x000000)+seg_ruler(16,10)
                     +seg_text(format_table_ascii(elem[1])+'\n')
                     +seg_font(0,1)+seg_char(12,400,0x000000)+seg_ruler(22,0))
        elif tag == 'ul':
            text = '\n'.join('  • '+it for it in elem[1])
            body += (seg_font(0,1)+seg_char(12,400,0x000000)+seg_ruler(20,10)
                     +seg_text(text+'\n')+seg_ruler(22,0))
        elif tag == 'ol':
            text = '\n'.join(f'  {i+1}. {it}' for i,it in enumerate(elem[1]))
            body += (seg_font(0,1)+seg_char(12,400,0x000000)+seg_ruler(20,10)
                     +seg_text(text+'\n')+seg_ruler(22,0))
        elif tag == 'hr':
            body += seg_hr(720)
        elif tag in ('p','page_lead'):
            body += seg_text(elem[1])
        elif tag == 'link':
            body += seg_vobj(link_id(elem[1]), elem[2], elem[1])

    payload = init + body
    return struct.pack('>HI', RECORD_TYPE_MAIN, len(payload)) + payload


# ── Symbolic TAD Compiler ─────────────────────────────────────────────────────
def compile_to_symbolic_tad(elements: list, doc_title: str = 'B-Book Reference Manual') -> str:
    today = date.today().isoformat()
    header = (
        '================================================================================\n'
        f'TAD Real Body [実身] : {doc_title}\n'
        'RECORD TYPE : 1 (TAD Main Record) | B-Book BeBook-Style Developer Reference\n'
        f'DATE        : {today} | GENERATOR : book2tad.py (Python stdlib)\n'
        '================================================================================\n\n'
        f'[付箋: DOCUMENT_HEADER | Title="{doc_title}" | Layout="BeBook-TwoColumn" | Spec="BTRON 3.20"]\n'
    )
    parts = []
    for elem in elements:
        tag = elem[0]
        if tag == 'h1':
            parts.append('【'+elem[1]+'】\n'+'━'*60)
        elif tag == 'page_lead':
            parts.append('>>> '+elem[1])
        elif tag == 'callout':
            _, title, body = elem
            parts.append(
                f'┌─ NOTE: {title} ─────────────────────────────────────────\n'
                f'│ {body}\n'
                f'└──────────────────────────────────────────────────────────')
        elif tag == 'h2':
            parts.append('\n■ '+elem[1]+'\n'+'─'*50)
        elif tag == 'h3':
            parts.append('\n▶ '+elem[1])
        elif tag == 'h4':
            parts.append('◆ '+elem[1])
        elif tag == 'api_card':
            _, name, ret_type, desc, sig, table, meta = elem
            tbl_part = ('\n'+format_table_ascii(table)) if table else ''
            meta_part = ('\n'+'\n'.join(f'  • {k}: {v}' for k,v in meta)) if meta else ''
            tbl_section = ('│ Parameters & Invariants:\n'
                           + tbl_part.replace('\n','\n│   ')+'\n│') if tbl_part else ''
            meta_section = (meta_part+'\n│') if meta_part else ''
            parts.append(
                f'┌── FUNCTION: {name} [{ret_type}] ──────────────────────────────\n'
                f'│ {desc}\n│\n│ C99 Prototype:\n│   {sig}\n'
                f'{tbl_section}{meta_section}'
                f'└─────────────────────────────────────────────────────────────────')
        elif tag == 'code_block':
            parts.append('```\n'+elem[1]+'\n```')
        elif tag == 'table':
            parts.append(format_table_ascii(elem[1]))
        elif tag == 'ul':
            parts.append('\n'.join('  • '+i for i in elem[1]))
        elif tag == 'ol':
            parts.append('\n'.join(f'  {i+1}. {it}' for i,it in enumerate(elem[1])))
        elif tag == 'p':
            parts.append(elem[1])
        elif tag == 'hr':
            parts.append('─'*70)
        elif tag == 'link':
            parts.append(f'[仮身] #{link_id(elem[1])} : {elem[2]} -> [{elem[1]}]')
    return header + '\n\n'.join(parts) + '\n'


# ── Doc title name ────────────────────────────────────────────────────────────
def _doc_title_name(base_name: str, html_path: str, src_root: str) -> str:
    if base_name != 'index':
        return base_name
    rel = os.path.relpath(html_path, src_root) if src_root else html_path
    sub = os.path.dirname(rel)
    if sub and sub != '.':
        m = {'vobject':'VObject','hmi':'HMI'}
        return m.get(sub.lower(), sub.lower().capitalize())
    return 'B-Book'


# ── File Processor ────────────────────────────────────────────────────────────
def process_file(html_path: str, target_dir: str, src_root: str = '') -> dict:
    os.makedirs(target_dir, exist_ok=True)
    base_name = os.path.splitext(os.path.basename(html_path))[0]
    with open(html_path, 'r', encoding='utf-8', errors='replace') as f:
        html = f.read()

    # title from <title> tag
    tm = re.search(r'<title>(.*?)</title>', html, re.I|re.S)
    title = decode_entities(strip_tags(tm.group(1))) if tm else base_name

    elements = parse_book_html(html)
    doc_title_name = _doc_title_name(base_name, html_path, src_root)

    bin_path = os.path.join(target_dir, doc_title_name+'.tad')
    txt_path = os.path.join(target_dir, doc_title_name+'.tad.txt')

    bin_tad = compile_to_binary_tad(elements)
    sym_tad = compile_to_symbolic_tad(elements, title)

    with open(bin_path,'wb') as f: f.write(bin_tad)
    with open(txt_path,'w',encoding='utf-8') as f: f.write(sym_tad)

    if base_name == 'index' and doc_title_name != 'index':
        with open(os.path.join(target_dir,'index.tad'),'wb') as f: f.write(bin_tad)
        with open(os.path.join(target_dir,'index.tad.txt'),'w',encoding='utf-8') as f: f.write(sym_tad)

    return {'html_path':html_path,'bin_path':bin_path,'txt_path':txt_path,
            'elements_count':len(elements),'bin_bytes':len(bin_tad),'title':doc_title_name}


# ── Self-Test ─────────────────────────────────────────────────────────────────
def run_tests():
    print('Running BtronBook.Compiler unit tests (Python)...')
    failures = []
    def chk(cond, msg):
        status = 'PASS' if cond else 'FAIL'
        print(f'  [{status}] {msg}')
        if not cond: failures.append(msg)

    sample_html = """<!DOCTYPE html><html>
    <head><title>Test Book</title></head>
    <body>
      <nav class="book-sidebar"><a>Ignored link</a></nav>
      <main class="book-content">
        <h1>Kernel Specification</h1>
        <div class="callout callout-info">
          <div class="callout-title">Important Scope</div>
          Zero dynamic heap allocations in fast paths.
        </div>
        <h2>1. Architecture</h2>
        <p>Preemptive priority scheduling.</p>
        <pre><code>
        [ Ready Queue ] ──► [ Task Dispatcher ]
        </code></pre>
        <h2>2. API Reference</h2>
        <div class="api-card">
          <div class="api-header">
            <span class="api-name">tk_cre_tsk</span>
            <span class="ret-badge">ID</span>
          </div>
          <p>Creates a new task within the kernel execution context.</p>
          <div class="api-signature">ID tk_cre_tsk(const T_CTSK *pk_ctsk);</div>
          <table class="api-table">
            <tr><th>Parameter</th><th>Type</th><th>Description</th></tr>
            <tr><td>pk_ctsk</td><td>const T_CTSK*</td><td>Task packet.</td></tr>
          </table>
          <div class="api-meta">
            <span class="meta-label">Errors</span><span>ER_NOMEM, ER_LIMIT</span>
          </div>
        </div>
      </main>
    </body></html>"""

    elements = parse_book_html(sample_html)
    chk(len(elements) >= 6, f'Parsed elements count ({len(elements)} >= 6)')
    api = next((e for e in elements if e[0]=='api_card' and e[1]=='tk_cre_tsk'), None)
    chk(api is not None, 'API Card extraction')
    cb = next((e for e in elements if e[0]=='code_block'), None)
    chk(cb is not None and '\n' in cb[1], 'Code block newline preservation')
    bin_tad = compile_to_binary_tad(elements)
    rec_type, payload_len = struct.unpack_from('>HI', bin_tad)
    chk(rec_type == 1, 'Binary TAD header validation')
    chk(payload_len == len(bin_tad)-6, 'Payload length matches')

    if failures:
        print(f'  {len(failures)} test(s) FAILED.')
        sys.exit(1)
    print('All BtronBook.Compiler unit tests passed!\n')


# ── Main ──────────────────────────────────────────────────────────────────────
if __name__ == '__main__':
    args = sys.argv[1:]
    if '--test' in args:
        run_tests()

    out_dir = 'tad_bin/b-book'
    os.makedirs(out_dir, exist_ok=True)

    src_dir = 'b-book'
    if os.path.isdir(src_dir):
        print('======================================================================')
        print(' B-Book Specialized TAD Formatter & Batch Compiler (scripts/book2tad.py)')
        print(f' Source Directory        : ./{src_dir}/')
        print(f' Output Directory        : ./{out_dir}/')
        print(' Target Specifications   : BeBook-Style C99 Systems Reference / BTRON 3.20')
        print('======================================================================')

        html_files = sorted(glob.glob(os.path.join(src_dir, '*.html')))
        results = []
        for html_file in html_files:
            rel = os.path.relpath(html_file, src_dir)
            sub = os.path.dirname(rel)
            tdir = out_dir if (not sub or sub=='.') else os.path.join(out_dir, sub)
            res = process_file(html_file, tdir, src_dir)
            in_n  = html_file.ljust(30)
            out_n = os.path.relpath(res['bin_path'],'tad_bin').ljust(30)
            print(f"  [BOOK2TAD] {in_n} -> {out_n} ({res['elements_count']} items, {res['bin_bytes']} bytes)")
            results.append(res)

        total = sum(r['bin_bytes'] for r in results)
        print('======================================================================')
        print(f' Successfully compiled {len(results)} B-Book documents via book2tad.')
        print(f' Total TAD Binary Size: {total} bytes across ./{out_dir}/')
        print('======================================================================')
