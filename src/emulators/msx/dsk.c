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
