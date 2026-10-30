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

void dsk_write_bank_byte(uint8_t bank, uint16_t off, uint8_t val)
{
    uint32_t o;
    if (bank < 2u) return;
    o = ((uint32_t)(bank - 2u) << 13) + off;
    if (o >= s_size) return;
    s_img[o] = val;
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

static char up_ch(uint8_t c)
{
    if (c >= 'a' && c <= 'z') return (char)(c - 32u);
    return (char)c;
}

static void bdos_setdta(z80 *z, uint16_t de)
{
    slots_write(0xF23Du, (uint8_t)de);
    slots_write(0xF23Eu, (uint8_t)(de >> 8));
    bdos_a(z, 0);
}

static uint16_t bdos_get_dta(void)
{
    uint16_t d = (uint16_t)(slots_read(0xF23Du) | (slots_read(0xF23Eu) << 8));
    return d ? d : 0x0080u;
}

static void bdos_strout(z80 *z)
{
    bdos_a(z, 0);
}

static uint16_t s_search_idx = 0;
static uint8_t  s_search_want[11];

static void bdos_sfirst(z80 *z, uint16_t fcb)
{
    int k;
    for (k = 0; k < 11; k++) s_search_want[k] = slots_read((uint16_t)(fcb + 1u + (uint16_t)k));
    s_search_idx = 0;
    for (; s_search_idx < s_root_ent; s_search_idx++) {
        uint32_t o = (uint32_t)s_root * 512u + (uint32_t)s_search_idx * 32u;
        uint8_t attr;
        uint16_t dta;
        if (o + 32u > s_size || s_img[o] == 0) break;
        if (s_img[o] == 0xE5u) continue;
        attr = s_img[o + 11u];
        if (attr == 0x0Fu || (attr & 0x08u)) continue;
        if (!name_eq(s_search_want, s_img + o)) continue;

        dta = bdos_get_dta();
        slots_ram_store(dta, 0x00u); /* drive code 0 */
        for (k = 0; k < 32; k++) slots_ram_store((uint16_t)(dta + 1u + (uint16_t)k), s_img[o + (uint32_t)k]);
        s_search_idx++;
        bdos_a(z, 0);
        return;
    }
    bdos_a(z, 0xFFu);
}

static void bdos_snext(z80 *z)
{
    int k;
    for (; s_search_idx < s_root_ent; s_search_idx++) {
        uint32_t o = (uint32_t)s_root * 512u + (uint32_t)s_search_idx * 32u;
        uint8_t attr;
        uint16_t dta;
        if (o + 32u > s_size || s_img[o] == 0) break;
        if (s_img[o] == 0xE5u) continue;
        attr = s_img[o + 11u];
        if (attr == 0x0Fu || (attr & 0x08u)) continue;
        if (!name_eq(s_search_want, s_img + o)) continue;

        dta = bdos_get_dta();
        slots_ram_store(dta, 0x00u);
        for (k = 0; k < 32; k++) slots_ram_store((uint16_t)(dta + 1u + (uint16_t)k), s_img[o + (uint32_t)k]);
        s_search_idx++;
        bdos_a(z, 0);
        return;
    }
    bdos_a(z, 0xFFu);
}

static void bdos_rdrnd(z80 *z, uint16_t fcb)
{
    uint32_t recno = 0;
    int i;
    for (i = 0; i < 3; i++) {
        recno |= (uint32_t)slots_read((uint16_t)(fcb + 33u + (uint16_t)i)) << (8 * i);
    }
    slots_ram_store((uint16_t)(fcb + 32u), (uint8_t)(recno & 127u));
    slots_ram_store((uint16_t)(fcb + 12u), (uint8_t)((recno >> 7) & 0xFFu));
    bdos_seq(z, fcb);
}

static void bdos_fsize(z80 *z, uint16_t fcb)
{
    uint8_t want[11];
    uint32_t i, fsize = 0, recs, recsize;
    int k, found = 0;
    for (k = 0; k < 11; k++) want[k] = slots_read((uint16_t)(fcb + 1u + (uint16_t)k));
    for (i = 0; i < s_root_ent; i++) {
        uint32_t o = (uint32_t)s_root * 512u + i * 32u;
        if (o + 32u > s_size || s_img[o] == 0) break;
        if (s_img[o] == 0xE5u) continue;
        if (!name_eq(want, s_img + o)) continue;
        fsize = (uint32_t)s_img[o + 28u] | ((uint32_t)s_img[o + 29u] << 8) |
                ((uint32_t)s_img[o + 30u] << 16) | ((uint32_t)s_img[o + 31u] << 24);
        found = 1;
        break;
    }
    if (!found) { bdos_a(z, 0xFFu); return; }
    recsize = (uint32_t)slots_read((uint16_t)(fcb + 14u)) |
              ((uint32_t)slots_read((uint16_t)(fcb + 15u)) << 8);
    if (recsize == 0) recsize = 128u;
    recs = (fsize + recsize - 1u) / recsize;
    for (k = 0; k < 4; k++) {
        slots_ram_store((uint16_t)(fcb + 33u + (uint16_t)k), (uint8_t)(recs >> (8 * k)));
    }
    bdos_a(z, 0);
}

static void bdos_alloc(z80 *z)
{
    uint32_t clus = (s_spc && s_size) ? (s_size / (512u * (uint32_t)s_spc)) : 714u;
    z->a = s_spc ? s_spc : 2u;
    z->b = 0;
    z->c = 2; /* 512 bytes per sector */
    z->ix = 512;
    z->e = (uint8_t)(clus & 0xFFu);
    z->d = (uint8_t)((clus >> 8) & 0xFFu);
    z->l = z->e;
    z->h = z->d;
    bdos_a(z, z->a);
}

/* ---- DOS 2 File Handles & FIB support ------------------------------------ */

#define DSK_HANDLES_MAX 16
typedef struct {
    uint8_t  used;
    uint16_t cluster;
    uint32_t size;
    uint32_t pos;
} DskHandle;

static DskHandle s_hnd[DSK_HANDLES_MAX];

static void bdos_hopen(z80 *z, uint16_t de)
{
    char path[64];
    uint8_t want[11];
    int len = 0, k, slot = -1;
    uint32_t i;
    const char *p, *slash, *colon, *dot;

    while (len < 63) {
        uint8_t c = slots_read((uint16_t)(de + (uint16_t)len));
        if (c == 0 || c == '\r' || c == '\n') break;
        path[len++] = (char)c;
    }
    path[len] = '\0';

    memset(want, ' ', 11);
    p = path;
    slash = strrchr(path, '\\');
    if (slash) p = slash + 1;
    colon = strrchr(p, ':');
    if (colon) p = colon + 1;
    for (k = 0; p[k] && p[k] != '.' && k < 8; k++) want[k] = (uint8_t)up_ch((uint8_t)p[k]);
    dot = strchr(p, '.');
    if (dot) {
        for (k = 0; dot[1 + k] && k < 3; k++) want[8 + k] = (uint8_t)up_ch((uint8_t)dot[1 + k]);
    }

    for (i = 0; i < s_root_ent; i++) {
        uint32_t o = (uint32_t)s_root * 512u + i * 32u;
        if (o + 32u > s_size || s_img[o] == 0) break;
        if (s_img[o] == 0xE5u) continue;
        if (!name_eq(want, s_img + o)) continue;

        for (k = 5; k < DSK_HANDLES_MAX; k++) {
            if (!s_hnd[k].used) { slot = k; break; }
        }
        if (slot < 0) { bdos_a(z, 0xDCu); return; }
        s_hnd[slot].used = 1;
        s_hnd[slot].cluster = (uint16_t)(s_img[o + 26u] | (s_img[o + 27u] << 8));
        s_hnd[slot].size = (uint32_t)s_img[o + 28u] | ((uint32_t)s_img[o + 29u] << 8) |
                           ((uint32_t)s_img[o + 30u] << 16) | ((uint32_t)s_img[o + 31u] << 24);
        s_hnd[slot].pos = 0;
        z->b = (uint8_t)slot;
        bdos_a(z, 0);
        return;
    }
    bdos_a(z, 0xD7u); /* _NOFIL */
}

static void bdos_hread(z80 *z, uint8_t h, uint16_t dst, uint16_t count)
{
    uint32_t avail, n, copied;
    if (h >= DSK_HANDLES_MAX || !s_hnd[h].used) { bdos_a(z, 0xD8u); return; }
    if (s_hnd[h].pos >= s_hnd[h].size || count == 0) {
        z->h = 0; z->l = 0;
        bdos_a(z, 0xC7u); /* _EOF */
        return;
    }
    avail = s_hnd[h].size - s_hnd[h].pos;
    n = (count < avail) ? (uint32_t)count : avail;
    copied = file_copy(s_hnd[h].cluster, s_hnd[h].pos, dst, n);
    s_hnd[h].pos += copied;
    z->l = (uint8_t)(copied & 0xFFu);
    z->h = (uint8_t)((copied >> 8) & 0xFFu);
    bdos_a(z, 0);
}

static void bdos_hseek(z80 *z, uint8_t h, uint8_t method, int32_t off)
{
    int64_t npos;
    if (h >= DSK_HANDLES_MAX || !s_hnd[h].used) { bdos_a(z, 0xD8u); return; }
    if (method == 0) npos = off;
    else if (method == 1) npos = (int64_t)s_hnd[h].pos + off;
    else npos = (int64_t)s_hnd[h].size + off;
    if (npos < 0) npos = 0;
    if (npos > (int64_t)s_hnd[h].size) npos = (int64_t)s_hnd[h].size;
    s_hnd[h].pos = (uint32_t)npos;
    z->l = (uint8_t)(s_hnd[h].pos & 0xFFu);
    z->h = (uint8_t)((s_hnd[h].pos >> 8) & 0xFFu);
    z->e = (uint8_t)((s_hnd[h].pos >> 16) & 0xFFu);
    z->d = (uint8_t)((s_hnd[h].pos >> 24) & 0xFFu);
    bdos_a(z, 0);
}

static void bdos_hclose(z80 *z, uint8_t h)
{
    if (h < DSK_HANDLES_MAX) s_hnd[h].used = 0;
    bdos_a(z, 0);
}

static uint16_t s_fib_idx = 0;

static void bdos_ffirst(z80 *z)
{
    uint16_t dta = bdos_get_dta();
    s_fib_idx = 0;
    for (; s_fib_idx < s_root_ent; s_fib_idx++) {
        uint32_t o = (uint32_t)s_root * 512u + (uint32_t)s_fib_idx * 32u;
        uint8_t attr;
        int k, n = 0;
        char name[16];
        if (o + 32u > s_size || s_img[o] == 0) break;
        if (s_img[o] == 0xE5u) continue;
        attr = s_img[o + 11u];
        if (attr == 0x0Fu || (attr & 0x08u)) continue;

        slots_ram_store(dta, 0xFFu);
        for (k = 0; k < 8 && s_img[o + (uint32_t)k] != ' '; k++) name[n++] = up_ch(s_img[o + (uint32_t)k]);
        if (s_img[o + 8u] != ' ') {
            name[n++] = '.';
            for (k = 0; k < 3 && s_img[o + 8u + (uint32_t)k] != ' '; k++) name[n++] = up_ch(s_img[o + 8u + (uint32_t)k]);
        }
        name[n] = '\0';
        for (k = 0; k <= n && k < 13; k++) slots_ram_store((uint16_t)(dta + 1u + (uint16_t)k), (uint8_t)name[k]);
        for (; k < 13; k++) slots_ram_store((uint16_t)(dta + 1u + (uint16_t)k), 0);
        slots_ram_store((uint16_t)(dta + 14u), attr);
        slots_ram_store((uint16_t)(dta + 15u), s_img[o + 22u]);
        slots_ram_store((uint16_t)(dta + 16u), s_img[o + 23u]);
        slots_ram_store((uint16_t)(dta + 17u), s_img[o + 24u]);
        slots_ram_store((uint16_t)(dta + 18u), s_img[o + 25u]);
        slots_ram_store((uint16_t)(dta + 19u), s_img[o + 26u]);
        slots_ram_store((uint16_t)(dta + 20u), s_img[o + 27u]);
        slots_ram_store((uint16_t)(dta + 21u), s_img[o + 28u]);
        slots_ram_store((uint16_t)(dta + 22u), s_img[o + 29u]);
        slots_ram_store((uint16_t)(dta + 23u), s_img[o + 30u]);
        slots_ram_store((uint16_t)(dta + 24u), s_img[o + 31u]);
        slots_ram_store((uint16_t)(dta + 25u), 1u);
        s_fib_idx++;
        bdos_a(z, 0);
        return;
    }
    bdos_a(z, 0xD7u);
}

static void bdos_fnext(z80 *z)
{
    uint16_t dta = bdos_get_dta();
    for (; s_fib_idx < s_root_ent; s_fib_idx++) {
        uint32_t o = (uint32_t)s_root * 512u + (uint32_t)s_fib_idx * 32u;
        uint8_t attr;
        int k, n = 0;
        char name[16];
        if (o + 32u > s_size || s_img[o] == 0) break;
        if (s_img[o] == 0xE5u) continue;
        attr = s_img[o + 11u];
        if (attr == 0x0Fu || (attr & 0x08u)) continue;

        slots_ram_store(dta, 0xFFu);
        for (k = 0; k < 8 && s_img[o + (uint32_t)k] != ' '; k++) name[n++] = up_ch(s_img[o + (uint32_t)k]);
        if (s_img[o + 8u] != ' ') {
            name[n++] = '.';
            for (k = 0; k < 3 && s_img[o + 8u + (uint32_t)k] != ' '; k++) name[n++] = up_ch(s_img[o + 8u + (uint32_t)k]);
        }
        name[n] = '\0';
        for (k = 0; k <= n && k < 13; k++) slots_ram_store((uint16_t)(dta + 1u + (uint16_t)k), (uint8_t)name[k]);
        for (; k < 13; k++) slots_ram_store((uint16_t)(dta + 1u + (uint16_t)k), 0);
        slots_ram_store((uint16_t)(dta + 14u), attr);
        slots_ram_store((uint16_t)(dta + 15u), s_img[o + 22u]);
        slots_ram_store((uint16_t)(dta + 16u), s_img[o + 23u]);
        slots_ram_store((uint16_t)(dta + 17u), s_img[o + 24u]);
        slots_ram_store((uint16_t)(dta + 18u), s_img[o + 25u]);
        slots_ram_store((uint16_t)(dta + 19u), s_img[o + 26u]);
        slots_ram_store((uint16_t)(dta + 20u), s_img[o + 27u]);
        slots_ram_store((uint16_t)(dta + 21u), s_img[o + 28u]);
        slots_ram_store((uint16_t)(dta + 22u), s_img[o + 29u]);
        slots_ram_store((uint16_t)(dta + 23u), s_img[o + 30u]);
        slots_ram_store((uint16_t)(dta + 24u), s_img[o + 31u]);
        slots_ram_store((uint16_t)(dta + 25u), 1u);
        s_fib_idx++;
        bdos_a(z, 0);
        return;
    }
    bdos_a(z, 0xD7u);
}

static void bdos_genv(z80 *z, uint16_t name_ptr, uint16_t val_ptr, uint8_t max_len)
{
    char name[32];
    int len = 0, k;
    const char *val = NULL;

    while (len < 31) {
        uint8_t c = slots_read((uint16_t)(name_ptr + (uint16_t)len));
        if (c == 0 || c == '=' || c == ' ') break;
        name[len++] = up_ch(c);
    }
    name[len] = '\0';

    if (strcmp(name, "PATH") == 0) val = "A:\\";
    else if (strcmp(name, "PROMPT") == 0) val = "$p$g";
    else if (strcmp(name, "SHELL") == 0) val = "TCOMMAND.COM";
    else if (strcmp(name, "PROGRAM") == 0) val = "A:\\AUTOEXEC.BAT";

    if (val) {
        for (k = 0; val[k] && k + 1 < (int)max_len; k++) {
            slots_ram_store((uint16_t)(val_ptr + (uint16_t)k), (uint8_t)val[k]);
        }
        slots_ram_store((uint16_t)(val_ptr + (uint16_t)k), 0);
        bdos_a(z, 0);
        return;
    }
    bdos_a(z, 0xDEu); /* _ENVX */
}

static void bdos_dosver(z80 *z)
{
    z->b = 0x02u;
    z->c = 0x20u;
    z->d = 0x02u;
    z->e = 0x20u;
    bdos_a(z, 0);
}

void dsk_bdos(z80 *z)
{
    uint16_t fcb;
    uint16_t hl;
    if (!z) return;

    /* T-DOS boot trigger: OUT (D4), A with A = 0xFF */
    if (z->a == 0xFFu) {
        dsk_auto_launch();
        return;
    }

    fcb = (uint16_t)(z->e | (z->d << 8));
    hl = (uint16_t)(z->l | (z->h << 8));

    switch (z->c) {
    case 0x00u: /* _TERM0 */
        bdos_a(z, 0);
        break;
    case 0x01u: /* _CONIN */
    case 0x07u: /* _DIRIN */
    case 0x08u: /* _INNOE */
        bdos_a(z, 0);
        break;
    case 0x02u: /* _CONOUT */
    case 0x06u: /* _DIRIO */
        bdos_a(z, (z->e == 0xFFu) ? 0 : z->e);
        break;
    case 0x09u: /* _STROUT */
        bdos_strout(z);
        break;
    case 0x0Au: /* _BUFIN */
        bdos_a(z, 0);
        break;
    case 0x0Bu: /* _CONST */
        bdos_a(z, 0);
        break;
    case 0x0Cu: /* _CPMVER */
        z->h = 0x00u; z->l = 0x22u;
        bdos_a(z, 0x22u);
        break;
    case 0x0Du: /* _DSKRST */
        bdos_a(z, 0);
        break;
    case 0x0Eu: /* _SELDSK */
        bdos_a(z, 1);
        break;
    case 0x0Fu: /* _FOPEN */
        bdos_open(z, fcb);
        break;
    case 0x10u: /* _FCLOSE */
        bdos_a(z, 0);
        break;
    case 0x11u: /* _SFIRST */
        bdos_sfirst(z, fcb);
        break;
    case 0x12u: /* _SNEXT */
        bdos_snext(z);
        break;
    case 0x13u: /* _FDEL */
        bdos_a(z, 0);
        break;
    case 0x14u: /* _RDSEQ */
        bdos_seq(z, fcb);
        break;
    case 0x15u: /* _WRSEQ */
        bdos_a(z, 0);
        break;
    case 0x16u: /* _FMAKE */
        bdos_a(z, 0);
        break;
    case 0x17u: /* _FREN */
        bdos_a(z, 0);
        break;
    case 0x18u: /* _LOGIN */
        z->l = 1; z->h = 0;
        bdos_a(z, 1);
        break;
    case 0x19u: /* _CURDRV */
        bdos_a(z, 0);
        break;
    case 0x1Au: /* _SETDTA */
        bdos_setdta(z, fcb);
        break;
    case 0x1Bu: /* _ALLOC */
    case 0x1Cu: /* _ALLOD */
        bdos_alloc(z);
        break;
    case 0x21u: /* _RDRND */
        bdos_rdrnd(z, fcb);
        break;
    case 0x22u: /* _WRRND */
        bdos_a(z, 0);
        break;
    case 0x23u: /* _FSIZE */
        bdos_fsize(z, fcb);
        break;
    case 0x24u: /* _SETRND */
        bdos_a(z, 0);
        break;
    case 0x26u: /* _WRRNDB */
    case 0x28u: /* _WRRND0 */
        bdos_a(z, 0);
        break;
    case 0x27u: /* _RDRNDB */
        bdos_read(z, fcb, hl);
        break;
    case 0x2Au: /* _GETDAT */
        z->h = (uint8_t)(2026 >> 8); z->l = (uint8_t)(2026 & 0xFFu);
        z->d = 10; z->e = 31;
        bdos_a(z, 0);
        break;
    case 0x2Cu: /* _GETTIM */
        z->h = 12; z->l = 0; z->d = 0;
        bdos_a(z, 0);
        break;
    case 0x2Fu: /* _GETDTA */
        {
            uint16_t d = bdos_get_dta();
            z->e = (uint8_t)(d & 0xFFu);
            z->d = (uint8_t)(d >> 8);
            bdos_a(z, 0);
        }
        break;
    case 0x40u: /* _FFIRST */
        bdos_ffirst(z);
        break;
    case 0x41u: /* _FNEXT */
        bdos_fnext(z);
        break;
    case 0x43u: /* _OPEN */
    case 0x44u: /* _CREATE */
        bdos_hopen(z, fcb);
        break;
    case 0x45u: /* _CLOSE */
        bdos_hclose(z, z->b);
        break;
    case 0x46u: /* _ENSURE */
    case 0x4Bu: /* _IOCTL */
    case 0x4Du: /* _DELETE */
    case 0x4Eu: /* _RENAME */
    case 0x50u: /* _ATTR */
    case 0x51u: /* _FTIME */
        bdos_a(z, 0);
        break;
    case 0x48u: /* _READ */
        bdos_hread(z, z->b, fcb, hl);
        break;
    case 0x49u: /* _WRITE */
        z->l = (uint8_t)(hl & 0xFFu); z->h = (uint8_t)((hl >> 8) & 0xFFu);
        bdos_a(z, 0);
        break;
    case 0x4Au: /* _SEEK */
        bdos_hseek(z, z->b, z->a, (int32_t)((uint32_t)hl | ((uint32_t)fcb << 16)));
        break;
    case 0x59u: /* _GETCD */
        slots_ram_store(fcb, '\\');
        slots_ram_store((uint16_t)(fcb + 1u), 0);
        bdos_a(z, 0);
        break;
    case 0x5Au: /* _CHDIR */
    case 0x5Bu: /* _PARSE */
    case 0x5Cu: /* _PFILE */
    case 0x62u: /* _TERM */
    case 0x63u: /* _DEFAB */
    case 0x64u: /* _DEFER */
        bdos_a(z, 0);
        break;
    case 0x65u: /* _ERROR */
        z->b = 0;
        bdos_a(z, 0);
        break;
    case 0x66u: /* _EXPLAIN */
        slots_ram_store(fcb, '$');
        bdos_a(z, 0);
        break;
    case 0x6Bu: /* _GENV */
        bdos_genv(z, hl, fcb, z->b);
        break;
    case 0x6Cu: /* _SENV */
    case 0x6Du: /* _FENV */
    case 0x6Eu: /* _DSKCHK */
        bdos_a(z, 0);
        break;
    case 0x6Fu: /* _DOSVER */
        bdos_dosver(z);
        break;
    default:
        bdos_a(z, 0);
        break;
    }
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
    slots_ram_store(0x0006u, 0x7Du);
    slots_ram_store(0x0007u, 0xF3u);

    /* Grauw dos2_environment.php trampolines:
     * $000C RDSLT, $0014 WRSLT, $001C CALSLT, $0024 ENASLT */
    slots_ram_store(0x000Cu, 0xC3u);
    slots_ram_store(0x000Du, 0x40u);
    slots_ram_store(0x000Eu, 0xF2u);

    slots_ram_store(0x0014u, 0xC3u);
    slots_ram_store(0x0015u, 0x50u);
    slots_ram_store(0x0016u, 0xF2u);

    slots_ram_store(0x001Cu, 0xC3u);
    slots_ram_store(0x001Du, 0x60u);
    slots_ram_store(0x001Eu, 0xF2u);

    slots_ram_store(0x0024u, 0xC3u);
    slots_ram_store(0x0025u, 0x70u);
    slots_ram_store(0x0026u, 0xF2u);

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

    /* Trampoline helper templates:
     * IN A, ($A8); PUSH AF; AND $FC; OUT ($A8), A; CALL target; POP AF; OUT ($A8), A; RET */
    {
        static const uint8_t slot_tramp[14] = {
            0xDB, 0xA8,
            0xF5,
            0xE6, 0xFC,
            0xD3, 0xA8,
            0xCD, 0x00, 0x00,
            0xF1,
            0xD3, 0xA8,
            0xC9
        };
        uint8_t tr[14];
        uint32_t k;
        memcpy(tr, slot_tramp, 14);

        /* $F240: RDSLT -> CALL $000C */
        tr[7] = 0x0Cu; tr[8] = 0x00u;
        for (k = 0; k < 14; k++) slots_write((uint16_t)(0xF240u + k), tr[k]);

        /* $F250: WRSLT -> CALL $0014 */
        tr[7] = 0x14u; tr[8] = 0x00u;
        for (k = 0; k < 14; k++) slots_write((uint16_t)(0xF250u + k), tr[k]);

        /* $F260: CALSLT -> CALL $001C */
        tr[7] = 0x1Cu; tr[8] = 0x00u;
        for (k = 0; k < 14; k++) slots_write((uint16_t)(0xF260u + k), tr[k]);

        /* $F270: ENASLT -> CALL $0024 */
        tr[7] = 0x24u; tr[8] = 0x00u;
        for (k = 0; k < 14; k++) slots_write((uint16_t)(0xF270u + k), tr[k]);

        /* DOS 2 Memory Mapper Jump Table at $F100 */
        /* ALL_SEG ($F100): XOR A; RET; NOP */
        slots_write(0xF100u, 0xAFu); slots_write(0xF101u, 0xC9u); slots_write(0xF102u, 0x00u);
        /* FRE_SEG ($F103): XOR A; RET; NOP */
        slots_write(0xF103u, 0xAFu); slots_write(0xF104u, 0xC9u); slots_write(0xF105u, 0x00u);
        /* RD_SEG ($F106): RET */
        slots_write(0xF106u, 0xC9u); slots_write(0xF107u, 0x00u); slots_write(0xF108u, 0x00u);
        /* WR_SEG ($F109): RET */
        slots_write(0xF109u, 0xC9u); slots_write(0xF10Au, 0x00u); slots_write(0xF10Bu, 0x00u);
        /* CAL_SEG ($F10C): RET */
        slots_write(0xF10Cu, 0xC9u); slots_write(0xF10Du, 0x00u); slots_write(0xF10Eu, 0x00u);
        /* CALLS ($F10F): RET */
        slots_write(0xF10Fu, 0xC9u); slots_write(0xF110u, 0x00u); slots_write(0xF111u, 0x00u);
        /* PUT_PH ($F112): RET */
        slots_write(0xF112u, 0xC9u); slots_write(0xF113u, 0x00u); slots_write(0xF114u, 0x00u);
        /* GET_PH ($F115): RET */
        slots_write(0xF115u, 0xC9u); slots_write(0xF116u, 0x00u); slots_write(0xF117u, 0x00u);
        /* PUT_P0 ($F118): LD A, B; OUT ($FC), A; RET */
        slots_write(0xF118u, 0x78u); slots_write(0xF119u, 0xD3u); slots_write(0xF11Au, 0xFCu); slots_write(0xF11Bu, 0xC9u);
        /* GET_P0 ($F11C): IN A, ($FC); RET */
        slots_write(0xF11Cu, 0xDBu); slots_write(0xF11Du, 0xFCu); slots_write(0xF11Eu, 0xC9u);
        /* PUT_P1 ($F11F): LD A, B; OUT ($FD), A; RET */
        slots_write(0xF11Fu, 0x78u); slots_write(0xF120u, 0xD3u); slots_write(0xF121u, 0xFDu); slots_write(0xF122u, 0xC9u);
        /* GET_P1 ($F123): IN A, ($FD); RET */
        slots_write(0xF123u, 0xDBu); slots_write(0xF124u, 0xFDu); slots_write(0xF125u, 0xC9u);
        /* PUT_P2 ($F126): LD A, B; OUT ($FE), A; RET */
        slots_write(0xF126u, 0x78u); slots_write(0xF127u, 0xD3u); slots_write(0xF128u, 0xFEu); slots_write(0xF129u, 0xC9u);
        /* GET_P2 ($F12A): IN A, ($FE); RET */
        slots_write(0xF12Au, 0xDBu); slots_write(0xF12Bu, 0xFEu); slots_write(0xF12Cu, 0xC9u);
        /* PUT_P3 ($F12D): LD A, B; OUT ($FF), A; RET */
        slots_write(0xF12Du, 0x78u); slots_write(0xF12Eu, 0xD3u); slots_write(0xF12Fu, 0xFFu); slots_write(0xF130u, 0xC9u);
        /* GET_P3 ($F131): IN A, ($FF); RET */
        slots_write(0xF131u, 0xDBu); slots_write(0xF132u, 0xFFu); slots_write(0xF133u, 0xC9u);

        /* EXTBIO hook at $FFCA for DOS 2 Memory Mapper table */
        static const uint8_t extbio_code[] = {
            0xF5,                     /* PUSH AF */
            0x7A,                     /* LD A, D */
            0xFE, 0x04,               /* CP 4 (DOS 2) */
            0x20, 0x0D,               /* JR NZ, +13 */
            0x7B,                     /* LD A, E */
            0xFE, 0x02,               /* CP 2 (Memory Mapper table) */
            0x20, 0x08,               /* JR NZ, +8 */
            0xF1,                     /* POP AF */
            0x3E, 0x20,               /* LD A, 32 (512 KiB segments) */
            0x06, 0x88,               /* LD B, $88 (Slot 3-2) */
            0x21, 0x00, 0xF1,         /* LD HL, $F100 */
            0xC9,                     /* RET */
            0xF1,                     /* POP AF */
            0xC9                      /* RET */
        };
        for (k = 0; k < sizeof(extbio_code); k++)
            slots_write((uint16_t)(0xFFCAu + k), extbio_code[k]);
    }

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

int dsk_auto_launch(void)
{
    int i, pick = -1;
    if (!s_fat_ok) return 0;
    if (s_rows == 0) build_list();
    if (s_rows == 0) return 0;

    /* 1. Try AUTOEXEC.BAT target */
    for (i = 0; i < s_rows; i++) {
        if (s_row[i].bat && strcmp(s_row[i].name, "AUTOEXEC.BAT") == 0) {
            pick = bat_target(i);
            break;
        }
    }
    if (pick >= 0) {
        launch_com(pick);
        return 1;
    }

    /* 2. Try any .COM file */
    for (i = 0; i < s_rows; i++) {
        if (s_row[i].com) {
            launch_com(i);
            return 1;
        }
    }

    /* 3. Try BSAVE image ($FE header) or AUTOEXEC.BAS */
    for (i = 0; i < s_rows; i++) {
        uint16_t start, end, exec;
        if (bload_header(&s_row[i], &start, &end, &exec)) {
            launch_bload(&s_row[i], start, end, exec);
            s_hold = 0;
            s_ran = 1;
            return 1;
        }
    }

    /* 4. Fallback: arm interactive menu */
    arm_menu();
    paint_menu();
    return 0;
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
