/*
 * Disk image behind the C-BIOS disk ROM.
 *
 * DSKIO does not talk to a WD2793. It maps an 8 KiB MegaROM bank:
 * bank 0 and 1 are the disk ROM, bank N>=2 is the image at (N-2)<<13.
 * slots.c owns those windows. This unit owns the bytes and the two BDOS
 * calls the boot sector needs to pull MSXDOS.SYS into RAM: 0x0F open and
 * 0x27 random block read. A COM started from the root menu also uses
 * 0x09 (print, ignored), 0x1A (set DTA) and 0x14 (sequential read).
 * The calls arrive as OUT (D4),A from a three-byte stub.
 *
 * A file whose first byte is $FE is a Disk BASIC BSAVE image, not a
 * DOS program. The seven-byte header is type, start, end, exec. The
 * payload is copied to the start address and entered at exec, with the
 * BIOS left in page 0. A DOS .COM is still copied to $0100.
 *
 * C-BIOS leaves the BIOS in page 0, and the boot sector then jumps to $0100.
 * The file is stored in the mapper page that $0100 will show, and the
 * sector's JP $0100 is pointed at a stub that selects main RAM in page 0
 * first. The stub runs only after BDOS has returned through the BIOS.
 */

#include "msx_int.h"
#include "z80.h"

#include <string.h>

static uint8_t  s_img[MSX_DSK_CAP];
static uint32_t s_size;
static uint16_t s_res, s_spf, s_root_ent, s_root, s_data;
static uint8_t  s_spc, s_fats;
static int      s_fat_ok;
static int      s_planted;

#define DSK_OPEN_MAX 4
typedef struct {
    uint8_t  used;
    uint16_t fcb;
    uint16_t cluster;
    uint32_t size;
} DskOpen;

static DskOpen s_open[DSK_OPEN_MAX];

#define DSK_MENU_MAX 96
#define DSK_MENU_VIS 20
#define DSK_MENU_COLS 40

typedef struct {
    char     name[13];
    uint8_t  com;
    uint8_t  bat;
    uint16_t cluster;
    uint32_t size;
} DskRow;

static DskRow s_row[DSK_MENU_MAX];
static int s_rows, s_cursor, s_top, s_hold, s_ran;

static void menu_clear(void)
{
    memset(s_row, 0, sizeof(s_row));
    s_rows = 0;
    s_cursor = 0;
    s_top = 0;
    s_hold = 0;
    s_ran = 0;
}

void dsk_clear(void)
{
    memset(s_img, 0, sizeof(s_img));
    s_size = 0;
    s_fat_ok = 0;
    s_planted = 0;
    memset(s_open, 0, sizeof(s_open));
    menu_clear();
}

void dsk_reset_open(void)
{
    s_planted = 0;
    memset(s_open, 0, sizeof(s_open));
    menu_clear();
}

int dsk_set_image(const uint8_t *img, uint32_t size)
{
    uint32_t i;
    uint32_t root_bytes;

    dsk_clear();
    if (!img || size < 512u || (size & 511u) || size > MSX_DSK_CAP) return 0;
    for (i = 0; i < size; i++) s_img[i] = img[i];
    s_size = size;

    s_spc = s_img[0x0D];
    s_res = (uint16_t)(s_img[0x0E] | (s_img[0x0F] << 8));
    s_fats = s_img[0x10];
    s_root_ent = (uint16_t)(s_img[0x11] | (s_img[0x12] << 8));
    s_spf = (uint16_t)(s_img[0x16] | (s_img[0x17] << 8));
    if (s_img[0x0B] == 0x00 && s_img[0x0C] == 0x02 &&
        s_spc != 0 && s_fats != 0 && s_spf != 0) {
        s_fat_ok = 1;
        s_root = (uint16_t)(s_res + (uint16_t)s_fats * s_spf);
        root_bytes = (uint32_t)s_root_ent * 32u;
        s_data = (uint16_t)(s_root + (uint16_t)((root_bytes + 511u) / 512u));
    }
    return 1;
}

uint8_t dsk_bank_byte(uint8_t bank, uint16_t off)
{
    uint32_t o;
    if (bank < 2u) return 0xFFu;
    o = ((uint32_t)(bank - 2u) << 13) + off;
    if (o >= s_size) return 0xFFu;
    return s_img[o];
}

static uint16_t fat_next(uint16_t cl)
{
    uint32_t o = (uint32_t)s_res * 512u + (uint32_t)cl + ((uint32_t)cl >> 1);
    uint16_t v;
    if (!s_fat_ok || o + 1u >= s_size) return 0xFFFu;
    v = (uint16_t)(s_img[o] | (s_img[o + 1u] << 8));
    if (cl & 1u) v = (uint16_t)(v >> 4);
    else v = (uint16_t)(v & 0x0FFFu);
    return v;
}

static int name_eq(const uint8_t *fcb, const uint8_t *dir)
{
    int i;
    for (i = 0; i < 11; i++) {
        uint8_t a = fcb[i];
        uint8_t b = dir[i];
        if (a == (uint8_t)'?') continue;
        if (a >= 'a' && a <= 'z') a = (uint8_t)(a - 32u);
        if (b >= 'a' && b <= 'z') b = (uint8_t)(b - 32u);
        if (a != b) return 0;
    }
    return 1;
}

static DskOpen *open_lookup(uint16_t fcb)
{
    int i;
    for (i = 0; i < DSK_OPEN_MAX; i++) {
        if (s_open[i].used && s_open[i].fcb == fcb) return &s_open[i];
    }
    return NULL;
}

static void bdos_a(z80 *z, uint8_t a)
{
    z->a = a;
    z->zf = (a == 0);
    z->sf = (a & 0x80u) != 0;
    z->cf = 0;
    z->nf = 0;
    z->hf = 0;
    z->pf = 0;
}

static void bdos_open(z80 *z, uint16_t fcb)
{
    uint8_t want[11];
    uint32_t i, nent;
    int slot = -1;
    int k;

    if (!s_fat_ok) { bdos_a(z, 0xFFu); return; }
    for (k = 0; k < 11; k++) want[k] = slots_read((uint16_t)(fcb + 1u + (uint16_t)k));
    nent = s_root_ent;
    for (i = 0; i < nent; i++) {
        uint32_t o = (uint32_t)s_root * 512u + i * 32u;
        uint8_t attr;
        if (o + 32u > s_size) break;
        if (s_img[o] == 0x00u) break;
        if (s_img[o] == 0xE5u) continue;
        attr = s_img[o + 11u];
        if (attr == 0x0Fu || (attr & 0x08u)) continue;
        if (!name_eq(want, s_img + o)) continue;
        for (k = 0; k < DSK_OPEN_MAX; k++) {
            if (!s_open[k].used || s_open[k].fcb == fcb) { slot = k; break; }
        }
        if (slot < 0) { bdos_a(z, 0xFFu); return; }
        s_open[slot].used = 1;
        s_open[slot].fcb = fcb;
        s_open[slot].cluster = (uint16_t)(s_img[o + 26u] | (s_img[o + 27u] << 8));
        s_open[slot].size = (uint32_t)s_img[o + 28u] |
                            ((uint32_t)s_img[o + 29u] << 8) |
                            ((uint32_t)s_img[o + 30u] << 16) |
                            ((uint32_t)s_img[o + 31u] << 24);
        slots_ram_store((uint16_t)(fcb + 16u), s_img[o + 28u]);
        slots_ram_store((uint16_t)(fcb + 17u), s_img[o + 29u]);
        slots_ram_store((uint16_t)(fcb + 18u), s_img[o + 30u]);
        slots_ram_store((uint16_t)(fcb + 19u), s_img[o + 31u]);
        bdos_a(z, 0);
        return;
    }
    bdos_a(z, 0xFFu);
}

/* Point the boot sector's JP $0100 at a page-3 stub that maps main RAM
 * over the BIOS, then jumps. The stub runs after this BDOS call returns. */
static void plant_page0_stub(void)
{
    static const uint8_t tmpl[22] = {
        0x3A, 0xFF, 0xFF,
        0x2F,
        0xE6, 0xFC,
        0xF6, 0x02,
        0x32, 0xFF, 0xFF,
        0xDB, 0xA8,
        0xE6, 0xFC,
        0xF6, 0x03,
        0xD3, 0xA8,
        0xC3, 0x00, 0x01
    };
    uint8_t stub[22];
    uint16_t a;
    int i;

    if (s_planted) return;
    memcpy(stub, tmpl, sizeof(stub));
    stub[7] = slots_ram_ssec();
    for (i = 0; i < 22; i++) slots_ram_store((uint16_t)(0xC200u + (uint16_t)i), stub[i]);
    /* IM 1 lands here once page 0 is RAM. $44E0 runs the BIOS handler. */
    slots_ram_store(0x0038u, 0xC3u);
    slots_ram_store(0x0039u, 0xE0u);
    slots_ram_store(0x003Au, 0x44u);
    for (a = 0xC000u; a + 2u < 0xC200u; a++) {
        if (slots_read(a) == 0xC3u && slots_read((uint16_t)(a + 1u)) == 0x00u &&
            slots_read((uint16_t)(a + 2u)) == 0x01u) {
            slots_ram_store((uint16_t)(a + 1u), 0x00u);
            slots_ram_store((uint16_t)(a + 2u), 0xC2u);
            s_planted = 1;
            return;
        }
    }
}

static uint32_t file_copy(uint16_t cluster, uint32_t pos, uint16_t dst, uint32_t n)
{
    uint32_t cps = (uint32_t)s_spc * 512u;
    uint32_t done = 0;

    if (!s_fat_ok || s_spc == 0) return 0;
    while (pos >= cps) {
        cluster = fat_next(cluster);
        if (cluster < 2u || cluster >= 0xFF8u) return done;
        pos -= cps;
    }
    while (done < n) {
        uint32_t sec = (uint32_t)s_data + (uint32_t)(cluster - 2u) * s_spc + pos / 512u;
        uint32_t off = pos % 512u;
        uint32_t chunk = 512u - off;
        uint32_t i;
        if (chunk > n - done) chunk = n - done;
        if (sec * 512u + off + chunk > s_size) break;
        for (i = 0; i < chunk; i++) {
            slots_ram_store((uint16_t)((uint32_t)dst + done + i),
                            s_img[sec * 512u + off + i]);
        }
        done += chunk;
        pos += chunk;
        if (pos >= cps) {
            cluster = fat_next(cluster);
            if (cluster < 2u || cluster >= 0xFF8u) break;
            pos = 0;
        }
    }
    return done;
}

static void bdos_read(z80 *z, uint16_t fcb, uint16_t nrec)
{
    DskOpen *f = open_lookup(fcb);
    uint32_t recsize, recno, want, off, n, got_recs;
    uint16_t dta;
    uint32_t copied;
    int i;

    if (!f) { bdos_a(z, 0xFFu); z->h = 0; z->l = 0; return; }
    recsize = (uint32_t)slots_read((uint16_t)(fcb + 14u)) |
              ((uint32_t)slots_read((uint16_t)(fcb + 15u)) << 8);
    if (recsize == 0) recsize = 128u;
    recno = 0;
    for (i = 0; i < 4; i++) {
        recno |= (uint32_t)slots_read((uint16_t)(fcb + 33u + (uint16_t)i)) << (8 * i);
    }
    if (nrec != 0 && recsize > 0xFFFFFFFFu / nrec) want = 0xFFFFFFFFu;
    else want = (uint32_t)nrec * recsize;
    if (recno != 0 && recsize > 0xFFFFFFFFu / recno) off = 0xFFFFFFFFu;
    else off = recno * recsize;

    dta = (uint16_t)(slots_read(0xF23Du) | (slots_read(0xF23Eu) << 8));
    if (off >= f->size || want == 0) n = 0;
    else {
        uint32_t avail = f->size - off;
        n = want < avail ? want : avail;
    }
    copied = n ? file_copy(f->cluster, off, dta, n) : 0;
    if (copied < n) n = copied;
    got_recs = (n == 0) ? 0u : (n + recsize - 1u) / recsize;
    recno += got_recs;
    for (i = 0; i < 4; i++) {
        slots_ram_store((uint16_t)(fcb + 33u + (uint16_t)i),
                        (uint8_t)(recno >> (8 * i)));
    }
    if (dta < 0x4000u && n != 0) plant_page0_stub();
    z->l = (uint8_t)(got_recs & 0xFFu);
    z->h = (uint8_t)((got_recs >> 8) & 0xFFu);
    bdos_a(z, (want == 0 || n == want) ? 0 : 1);
}

/* One sequential record. Record size 0 means the DOS default of 128.
 * The pointer is extent*128 + current record, which is what a .COM
 * that never calls the random-record functions walks. */
static void bdos_seq(z80 *z, uint16_t fcb)
{
    DskOpen *f = open_lookup(fcb);
    uint32_t recsize, recno, off, n, copied;
    uint16_t dta;
    uint32_t i;

    if (!f) { bdos_a(z, 0xFFu); return; }
    recsize = (uint32_t)slots_read((uint16_t)(fcb + 14u)) |
              ((uint32_t)slots_read((uint16_t)(fcb + 15u)) << 8);
    if (recsize == 0) recsize = 128u;
    recno = (uint32_t)slots_read((uint16_t)(fcb + 12u)) * 128u +
            slots_read((uint16_t)(fcb + 32u));
    if (recno > 0xFFFFFFFFu / recsize) off = 0xFFFFFFFFu;
    else off = recno * recsize;
    dta = (uint16_t)(slots_read(0xF23Du) | (slots_read(0xF23Eu) << 8));
    if (off >= f->size) { bdos_a(z, 1); return; }
    n = f->size - off;
    if (n > recsize) n = recsize;
    copied = file_copy(f->cluster, off, dta, n);
    if (copied < n) n = copied;
    recno++;
    slots_ram_store((uint16_t)(fcb + 32u), (uint8_t)(recno & 127u));
    slots_ram_store((uint16_t)(fcb + 12u), (uint8_t)((recno >> 7) & 0xFFu));
    for (i = 0; i < 4u; i++) {
        slots_ram_store((uint16_t)(fcb + 33u + i), (uint8_t)(recno >> (8u * i)));
    }
    bdos_a(z, (n == recsize) ? 0 : 1);
}

static void bdos_setdta(z80 *z, uint16_t de)
{
    slots_write(0xF23Du, (uint8_t)de);
    slots_write(0xF23Eu, (uint8_t)(de >> 8));
    bdos_a(z, 0);
}

/* Credits and "Loading..." are not the picture. The COM keeps going
 * when this returns zero. */
static void bdos_strout(z80 *z)
{
    bdos_a(z, 0);
}

void dsk_bdos(z80 *z)
{
    uint16_t fcb;
    uint16_t hl;
    if (!z) return;
    fcb = (uint16_t)(z->e | (z->d << 8));
    hl = (uint16_t)(z->l | (z->h << 8));
    if (z->c == 0x0Fu) bdos_open(z, fcb);
    else if (z->c == 0x09u) bdos_strout(z);
    else if (z->c == 0x14u) bdos_seq(z, fcb);
    else if (z->c == 0x1Au) bdos_setdta(z, fcb);
    else if (z->c == 0x27u) bdos_read(z, fcb, hl);
}

/* ---- root menu ---------------------------------------------------------- */

static char up_ch(uint8_t c)
{
    if (c >= 'a' && c <= 'z') return (char)(c - 32u);
    return (char)c;
}

static uint32_t file_bytes(uint16_t cluster, uint8_t *dst, uint32_t n)
{
    uint32_t cps = (uint32_t)s_spc * 512u;
    uint32_t pos = 0;
    uint32_t done = 0;

    if (!s_fat_ok || s_spc == 0 || !dst) return 0;
    while (done < n) {
        uint32_t sec, off, chunk, i;
        if (cluster < 2u || cluster >= 0xFF8u) break;
        sec = (uint32_t)s_data + (uint32_t)(cluster - 2u) * s_spc + pos / 512u;
        off = pos % 512u;
        chunk = 512u - off;
        if (chunk > n - done) chunk = n - done;
        if (sec * 512u + off + chunk > s_size) break;
        for (i = 0; i < chunk; i++) dst[done + i] = s_img[sec * 512u + off + i];
        done += chunk;
        pos += chunk;
        if (pos >= cps) {
            cluster = fat_next(cluster);
            pos = 0;
        }
    }
    return done;
}

static void build_list(void)
{
    uint32_t i;

    s_rows = 0;
    if (!s_fat_ok) return;
    for (i = 0; i < s_root_ent && s_rows < DSK_MENU_MAX; i++) {
        uint32_t o = (uint32_t)s_root * 512u + i * 32u;
        uint8_t attr;
        int k, n;
        DskRow *e;

        if (o + 32u > s_size) break;
        if (s_img[o] == 0x00u) break;
        if (s_img[o] == 0xE5u) continue;
        attr = s_img[o + 11u];
        if (attr == 0x0Fu || (attr & 0x08u) || (attr & 0x10u)) continue;
        e = &s_row[s_rows];
        memset(e, 0, sizeof(*e));
        n = 0;
        for (k = 0; k < 8 && s_img[o + (uint32_t)k] != ' '; k++)
            e->name[n++] = up_ch(s_img[o + (uint32_t)k]);
        if (s_img[o + 8u] != ' ') {
            e->name[n++] = '.';
            for (k = 0; k < 3 && s_img[o + 8u + (uint32_t)k] != ' '; k++)
                e->name[n++] = up_ch(s_img[o + 8u + (uint32_t)k]);
        }
        e->name[n] = '\0';
        e->com = (uint8_t)(n >= 4 && e->name[n - 4] == '.' &&
                           e->name[n - 3] == 'C' && e->name[n - 2] == 'O' &&
                           e->name[n - 1] == 'M');
        e->bat = (uint8_t)(n >= 4 && e->name[n - 4] == '.' &&
                           e->name[n - 3] == 'B' && e->name[n - 2] == 'A' &&
                           e->name[n - 1] == 'T');
        e->cluster = (uint16_t)(s_img[o + 26u] | (s_img[o + 27u] << 8));
        e->size = (uint32_t)s_img[o + 28u] | ((uint32_t)s_img[o + 29u] << 8) |
                  ((uint32_t)s_img[o + 30u] << 16) | ((uint32_t)s_img[o + 31u] << 24);
        s_rows++;
    }
}

static int com_named(const char *base, int nlen)
{
    int i, k;
    if (nlen <= 0) return -1;
    for (i = 0; i < s_rows; i++) {
        if (!s_row[i].com) continue;
        for (k = 0; k < nlen; k++) {
            if (s_row[i].name[k] != base[k]) break;
        }
        if (k == nlen && s_row[i].name[k] == '.') return i;
    }
    return -1;
}

static int bat_target(int idx)
{
    uint8_t buf[24];
    char tok[9];
    uint32_t n, i, k;

    if (idx < 0 || idx >= s_rows) return -1;
    n = file_bytes(s_row[idx].cluster, buf, (uint32_t)sizeof(buf));
    i = 0;
    k = 0;
    while (i < n && (buf[i] == ' ' || buf[i] == '\t')) i++;
    while (i < n && k < 8u) {
        uint8_t c = buf[i];
        if (c == ' ' || c == '\r' || c == '\n' || c == '\t' || c == 0x1Au || c == '.')
            break;
        tok[k++] = up_ch(c);
        i++;
    }
    tok[k] = '\0';
    return com_named(tok, (int)k);
}

static void prefer_cursor(void)
{
    int i, pick = -1;

    for (i = 0; i < s_rows; i++) {
        if (s_row[i].bat && strcmp(s_row[i].name, "AUTOEXEC.BAT") == 0) {
            pick = bat_target(i);
            break;
        }
    }
    if (pick < 0) {
        for (i = 0; i < s_rows; i++) {
            if (!s_row[i].com) continue;
            if (strcmp(s_row[i].name, "COMMAND.COM") == 0) continue;
            pick = i;
            break;
        }
    }
    if (pick < 0) {
        for (i = 0; i < s_rows; i++) {
            if (s_row[i].com) { pick = i; break; }
        }
    }
    if (pick < 0) pick = 0;
    s_cursor = pick;
    s_top = 0;
    if (s_rows > 0 && s_cursor >= DSK_MENU_VIS)
        s_top = s_cursor - DSK_MENU_VIS + 1;
}

static void put_u32(uint8_t *dst, int width, uint32_t v)
{
    char tmp[10];
    int n = 0;
    int i;
    if (v == 0) tmp[n++] = '0';
    while (v && n < 10) {
        tmp[n++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    for (i = 0; i < width; i++) dst[i] = ' ';
    for (i = 0; i < n && i < width; i++) dst[width - 1 - i] = (uint8_t)tmp[i];
}

static int text_ready(uint32_t *pt_out)
{
    uint8_t r0 = vdp_debug_reg(0);
    uint8_t r1 = vdp_debug_reg(1);
    uint8_t r4 = vdp_debug_reg(4);
    uint8_t mode;
    uint32_t base, cap = 0, i;
    uint8_t *vram;

    if ((r1 & 0x40u) == 0) return 0;
    mode = (uint8_t)((((r0 & 0x0Eu) << 1) | ((r1 & 0x08u) >> 2) |
                      ((r1 & 0x10u) >> 4)) & 0x1Fu);
    if (mode != 0x00u && mode != 0x01u) return 0;
    base = (uint32_t)(r4 & 0x3Fu) << 11;
    vram = vdp_vram(&cap);
    if (!vram || base + 0x210u >= cap) return 0;
    for (i = 0; i < 8u; i++) {
        if (vram[base + 0x41u * 8u + i] != 0) {
            if (pt_out) *pt_out = base;
            return 1;
        }
    }
    return 0;
}

static void vreg(uint8_t reg, uint8_t value)
{
    vdp_write(0x99u, value);
    vdp_write(0x99u, (uint8_t)(0x80u | reg));
}

static void paint_menu(void)
{
    uint32_t cap = 0;
    uint8_t *vram = vdp_vram(&cap);
    int row, i;
    static const char help[] = " Up Down Enter";
    static const char empty[] = "(empty)";
    static const char mark[] = "B-MSX";

    if (!vram || cap < (uint32_t)DSK_MENU_COLS * 24u) return;
    for (i = 0; i < DSK_MENU_COLS * 24; i++) vram[i] = ' ';
    vram[0] = ' ';
    vram[1] = 'A';
    vram[2] = ':';
    vram[3] = '\\';
    for (i = 0; mark[i]; i++) vram[34 + i] = (uint8_t)mark[i];

    for (row = 0; row < 24; row++) {
        uint32_t base = (uint32_t)row * (uint32_t)DSK_MENU_COLS;
        if (row == 1 || row == 22) {
            vram[base] = '+';
            vram[base + 39u] = '+';
            for (i = 1; i < 39; i++) vram[base + (uint32_t)i] = '-';
        } else if (row == 23) {
            for (i = 0; help[i]; i++) vram[base + (uint32_t)i] = (uint8_t)help[i];
        } else if (row >= 2 && row < 22) {
            int idx = s_top + (row - 2);
            vram[base] = '|';
            vram[base + 39u] = '|';
            if (s_rows == 0 && row == 2) {
                for (i = 0; empty[i]; i++) vram[base + 2u + (uint32_t)i] = (uint8_t)empty[i];
            } else if (idx >= 0 && idx < s_rows) {
                const char *nm = s_row[idx].name;
                vram[base + 1u] = (idx == s_cursor) ? (uint8_t)'>' : (uint8_t)' ';
                for (i = 0; nm[i] && i < 12; i++)
                    vram[base + 2u + (uint32_t)i] = (uint8_t)nm[i];
                put_u32(vram + base + 31u, 7, s_row[idx].size);
            }
        }
    }
}

static int arm_menu(void)
{
    uint32_t pt = 0, cap = 0, i;
    uint8_t *vram;
    uint8_t r0, r1;

    if (!text_ready(&pt)) return 0;
    vram = vdp_vram(&cap);
    if (!vram || cap < 0x1000u) return 0;
    if (pt != 0x800u) {
        for (i = 0; i < 0x800u; i++) vram[0x800u + i] = vram[pt + i];
    }
    r0 = (uint8_t)(vdp_debug_reg(0) & (uint8_t)~0x0Eu);
    r1 = vdp_debug_reg(1);
    r1 = (uint8_t)((r1 | 0x50u) & (uint8_t)~0x08u);
    vreg(0, r0);
    vreg(1, r1);
    vreg(2, 0);
    vreg(4, 1);
    vreg(7, 0xF4u);
    vreg(8, (uint8_t)(vdp_debug_reg(8) | 0x02u));
    build_list();
    prefer_cursor();
    s_hold = 1;
    return 1;
}

/* Tell SLTTBL what the hardware map already is. A program that builds a
 * CALLF slot id reads this byte, not the live secondary register. */
static void note_ram_slttbl(void)
{
    uint8_t ram = slots_ram_ssec();
    uint8_t ssl = (uint8_t)(ram | (ram << 2) | (ram << 4) | (ram << 6));
    slots_write(0xFCC8u, ssl);
}

/* Disk BASIC leaves a JP to BDOS at $F37D. Pushes must start below that
 * JP: the first push from $F380 replaces it, and the next CALL $F37D
 * falls into clprim and rewrites the slot register. A BLOAD image that
 * RETs also must not pop the JP itself, or the return address is $00C3
 * and the BIOS reboots the disk. $F1F0 is the idle the return lands on. */
static void plant_bdos_entry(void)
{
    slots_write(0xF1F0u, 0xF3u);
    slots_write(0xF1F1u, 0x76u);
    slots_write(0xF1F2u, 0x18u);
    slots_write(0xF1F3u, 0xFDu);
    slots_write(0xF200u, 0xD3u);
    slots_write(0xF201u, 0xD4u);
    slots_write(0xF202u, 0xC9u);
    slots_write(0xF37Du, 0xC3u);
    slots_write(0xF37Eu, 0x00u);
    slots_write(0xF37Fu, 0xF2u);
    slots_write(0xFC4Au, 0x7Du);
    slots_write(0xFC4Bu, 0xF3u);
    slots_write(0xF674u, 0x7Du);
    slots_write(0xF675u, 0xF3u);
}

/* Disk BASIC BSAVE: FE, start, end, exec, then (end-start+1) bytes.
 * The load address sits above the BIOS and below the system work area,
 * which is where BLOAD puts a binary. A DOS .COM is not this shape. */
static int bload_header(const DskRow *e, uint16_t *start, uint16_t *end,
                        uint16_t *exec)
{
    uint8_t h[7];
    uint16_t s, n, x;
    uint32_t span;

    if (!e->size || file_bytes(e->cluster, h, 7) < 7u) return 0;
    if (h[0] != 0xFEu) return 0;
    s = (uint16_t)(h[1] | ((uint16_t)h[2] << 8));
    n = (uint16_t)(h[3] | ((uint16_t)h[4] << 8));
    x = (uint16_t)(h[5] | ((uint16_t)h[6] << 8));
    if (n < s || s < 0x8000u || n >= 0xF380u) return 0;
    span = (uint32_t)(n - s) + 1u;
    if (span + 7u > e->size) return 0;
    if (x != 0 && (x < s || x > n)) return 0;
    *start = s;
    *end = n;
    *exec = x ? x : s;
    return 1;
}

/* Page 0 is RAM, so IM 1 and RST $30 cannot stay in the BIOS.
 * The vblank trampoline runs KEYINT with the main ROM in page 0, then
 * puts the TPA back. RST $30 reads its slot and address from the COM,
 * then enters C-BIOS clprim ($F38C) with page 0 forced to the BIOS. */
static void launch_dos(const DskRow *e)
{
    static const uint8_t tramp[27] = {
        0xF5,
        0x3A, 0xFF, 0xFF,
        0x2F,
        0xE6, 0xFC,
        0xF6, 0x02,
        0x32, 0xFF, 0xFF,
        0xDB, 0xA8,
        0xF5,
        0xE6, 0xFC,
        0xD3, 0xA8,
        0xCD, 0x38, 0x00,
        0xF1,
        0xD3, 0xA8,
        0xF1,
        0xC9
    };
    static const uint8_t callf[] = {
        0x08,
        0xD9,
        0xE1,
        0x7E,
        0x23,
        0x5E,
        0x23,
        0x56,
        0x23,
        0xE5,
        0xD5,
        0xDD, 0xE1,
        0x08,
        0xD9,
        0xF3,
        0x08,
        0xDB, 0xA8,
        0xF5,
        0xE6, 0xFC,
        0xC3, 0x8C, 0xF3
    };
    uint32_t i;

    if (e->size) file_copy(e->cluster, 0, 0x0100u, e->size);
    slots_map_com_pages();
    note_ram_slttbl();
    /* $0005 jumps at the stub in page 3. ($0006) is the TPA top.
     * The vblank trampoline sits just above it, still in page 3,
     * because page 1 is now the TPA and no longer the disk ROM. */
    slots_ram_store(0x0000u, 0xC3u);
    slots_ram_store(0x0001u, 0x00u);
    slots_ram_store(0x0002u, 0x00u);
    slots_ram_store(0x0005u, 0xC3u);
    slots_ram_store(0x0006u, 0x00u);
    slots_ram_store(0x0007u, 0xF2u);
    slots_ram_store(0x0030u, 0xC3u);
    slots_ram_store(0x0031u, 0x30u);
    slots_ram_store(0x0032u, 0xF2u);
    slots_ram_store(0x0038u, 0xC3u);
    slots_ram_store(0x0039u, 0x10u);
    slots_ram_store(0x003Au, 0xF2u);
    plant_bdos_entry();
    for (i = 0; i < sizeof(tramp); i++)
        slots_write((uint16_t)(0xF210u + i), tramp[i]);
    slots_write(0xF218u, slots_ram_ssec());
    for (i = 0; i < sizeof(callf); i++)
        slots_write((uint16_t)(0xF230u + i), callf[i]);
    msx_cpu_go(0x0100u, 0xE000u);
}

/* The image is not a TPA program. Disk BASIC would BLOAD it and jump
 * to the exec word with the BIOS still in page 0. */
static void launch_bload(const DskRow *e, uint16_t start, uint16_t end,
                         uint16_t exec)
{
    uint32_t span = (uint32_t)(end - start) + 1u;

    slots_map_bios_ram();
    note_ram_slttbl();
    file_copy(e->cluster, 7u, start, span);
    plant_bdos_entry();
    /* Two bytes under the JP. The word there is the RET target. */
    slots_write(0xF37Bu, 0xF0u);
    slots_write(0xF37Cu, 0xF1u);
    msx_cpu_go(exec, 0xF37Bu);
}

static void launch_com(int idx)
{
    DskRow *e;
    uint16_t start, end, exec;

    if (idx < 0 || idx >= s_rows || !s_row[idx].com) return;
    e = &s_row[idx];
    memset(s_open, 0, sizeof(s_open));
    if (bload_header(e, &start, &end, &exec)) launch_bload(e, start, end, exec);
    else launch_dos(e);
    s_hold = 0;
    s_ran = 1;
}

int dsk_menu_service(void)
{
    if (!s_fat_ok || s_ran) return 0;
    if (!s_hold && !arm_menu()) return 0;
    paint_menu();
    return 1;
}

int dsk_menu_active(void)
{
    return (s_hold && !s_ran) ? 1 : 0;
}

int dsk_menu_cursor(void)
{
    return s_cursor;
}

int dsk_menu_rows(void)
{
    return s_rows;
}

int dsk_menu_row(int index, char *out, uint32_t cap)
{
    uint32_t i;
    if (!out || cap == 0 || index < 0 || index >= s_rows) return 0;
    for (i = 0; i + 1u < cap && s_row[index].name[i]; i++)
        out[i] = s_row[index].name[i];
    out[i] = '\0';
    return 1;
}

int dsk_menu_key(int op)
{
    int com;
    if (!s_hold || s_ran) return 0;
    if (op == 0 && s_cursor > 0) s_cursor--;
    else if (op == 1 && s_cursor + 1 < s_rows) s_cursor++;
    else if (op == 2) {
        com = -1;
        if (s_cursor >= 0 && s_cursor < s_rows) {
            if (s_row[s_cursor].com) com = s_cursor;
            else if (s_row[s_cursor].bat) com = bat_target(s_cursor);
        }
        if (com >= 0) launch_com(com);
    }
    if (s_cursor < s_top) s_top = s_cursor;
    if (s_cursor >= s_top + DSK_MENU_VIS) s_top = s_cursor - DSK_MENU_VIS + 1;
    if (s_hold) paint_menu();
    return 1;
}
