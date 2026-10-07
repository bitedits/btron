/*
 * Boot tracer for the tronMSX core.  Owns its own z80 instance (wired exactly
 * like tronmsx.c) so it can log PC, watch for a stuck loop, and record every
 * OUT to the VDP (#98/#99), PPI (#A8-#AB) and PSG (#A0-#A3) during boot.
 * Developer tool, not shipped.
 *
 * Usage: msx_trace <rom-file> [max-instructions]
 */

#include "../../src/emulators/msx/msx_int.h"
#include "../../src/emulators/msx/msx_body.h"
#include "../../src/emulators/msx/z80.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static z80 cpu;
static uint8_t vdp_latch_state;   /* tracks the #99 write-pair phase */
static uint8_t vdp_my_latch;      /* mirror of the VDP first-byte latch */
static uint8_t vdp_reg[64];       /* shadow of VDP register writes */
static long g_frame;              /* current frame, for log context */
static long g_vblanks;            /* count of VBlank IRQs delivered */
static int g_verbose = 1;
static uint8_t g_pc_latch;        /* mirror of PPI port C (row select) */
static unsigned g_psgcnt[4];      /* #A0..#A3 read count (PSG / joystick) */
static unsigned g_psgsel;         /* #A2 reads that returned joystick bits */
static unsigned g_selhist[16];    /* # of #AA writes per row, whole run */
static unsigned g_hit_4e4a;       /* entries into the input handler */
static unsigned g_hit_4e7b;       /* row-8 read inside the handler */
static unsigned g_bios_c0, g_bios_c3, g_bios_c6;   /* KROW0/1/2 */
static unsigned g_bios_d3, g_bios_db;              /* STICK/STRIG */
static unsigned g_bios_141, g_bios_9f;             /* row read / SYNCHR */
static FILE *g_vwlog;               /* VWLOG=<file>: binary #98 write trace */
static long g_step_frame = -1;      /* STEP=<frame>,<pc>,<count> */
static long g_watch = -1;           /* WATCH=<hex addr>: log reads and writes */
static int  g_watch_budget = 5000;
static int  g_watch_have;
static uint8_t g_watch_last;
static long g_step_pc;
static long g_step_count = 200;
static long g_step_left;
static int  g_step_parsed;
static unsigned g_portw[256];       /* OUT instructions per port, whole run */
static unsigned g_portr[256];       /* IN instructions per port, whole run */
static uint16_t g_portwpc[256];     /* first PC issuing OUT to each port */
static uint16_t g_portrpc[256];     /* first PC issuing IN from each port */
static uint32_t g_last_fbhash;
static unsigned g_vwhist[512];   /* #98 data writes per 256-byte VRAM block */
static uint32_t g_pchist[4096];  /* executed instructions per 16-byte PC bucket */
static uint32_t g_vwlast;        /* last VRAM address written via #98       */
static uint32_t g_wrpc[256];     /* #98 writes attributable to each 256-byte PC bucket */
static unsigned g_seekhist[512]; /* 'set write address' commands per 256-byte block */
static unsigned g_seekhist_cart[64]; /* same, but only seeks issued from cart code */
static unsigned g_ntw[2];            /* per-frame #98 writes into NT, and those from cart PC */
static unsigned g_rowhist[16];    /* # of #A9 reads per selected row, whole run */
static uint16_t g_rowfirstpc[16]; /* first PC that read each row */
static uint8_t g_rowlastval[16];  /* last value read back per row */

static uint8_t cb_read(void *ud, uint16_t a)
{
    uint8_t v;
    (void)ud;
    v = slots_read(a);
    if (g_watch >= 0 && (long)a == g_watch && g_watch_budget > 0 &&
        (!g_watch_have || g_watch_last != v)) {
        g_watch_have = 1; g_watch_last = v; g_watch_budget--;
        printf("f%ld WATCH-RD %04X -> %02X @PC=%04X\n", g_frame, a, v, cpu.pc);
    }
    /* Watch the cart-signature probe addresses: C-BIOS reads (0x4000)/(0x8000)
     * looking for "AB" (0x41,0x42).  Log the slot that is selected at the time
     * so we can see whether the cart is ever made visible there. */
    if ((a >= 0x4000u && a <= 0x4001u) || (a >= 0x8000u && a <= 0x8001u)) {
        uint8_t pa = slots_ppi_a();
        printf("f%ld RD %04X = %02X  @PC=%04X  ppi_a=%02X(p1=%u p2=%u)\n",
               g_frame, a, v, cpu.pc, pa, (pa>>2)&3, (pa>>4)&3);
    }
    /* During the maze load, watch the game read the cart's upper half. */
    {
        static int rd_budget = 120;
        if (a >= 0x8000u && a < 0xC000u && cpu.pc >= 0x4000u &&
            rd_budget > 0 && g_frame >= 140 && g_frame <= 146) {
            rd_budget--;
            printf("f%ld GAME-RD %04X = %02X @PC=%04X ppi_a=%02X\n",
                   g_frame, a, v, cpu.pc, slots_ppi_a());
        }
    }
    return v;
}
static void cb_write(void *ud, uint16_t a, uint8_t v)
{
    (void)ud;
    if (g_watch >= 0 && (long)a == g_watch && g_watch_budget > 0) {
        g_watch_budget--;
        printf("f%ld WATCH-WR %04X <- %02X @PC=%04X\n", g_frame, a, v, cpu.pc);
    }
    if ((a == 0xEFA4u || a == 0xEFAAu || a == 0xEF70u) && g_frame >= 130) {
        static int w_budget = 3000;
        if (w_budget > 0) {
            w_budget--;
            printf("f%ld W@%04X <- %02X @PC=%04X\n", g_frame, a, v, cpu.pc);
        }
    }
    /* The input-result byte the title loop compares against the 0xFB sentinel. */
    if (a == 0xF020u && g_frame >= 415) {
        static int f20_budget = 80;
        if (f20_budget > 0) {
            f20_budget--;
            printf("f%ld F020 <- %02X @PC=%04X\n", g_frame, v, cpu.pc);
        }
    }
    slots_write(a, v);
}
static uint8_t cb_in(z80 *z, uint8_t p)
{
    uint8_t v = msx_bus_in(p);
    (void)z;
    g_portr[p]++;
    if (g_portrpc[p] == 0u) g_portrpc[p] = (uint16_t)(cpu.pc ? cpu.pc : 1u);
    /* Does the game read the PSG I/O ports (joystick on port A)?  #A2 is the
     * PSG read port; a repeated read there during the title loop means the
     * game polls the joystick, not (only) the keyboard. */
    if (p >= 0xA0u && p <= 0xA3u) {
        g_psgcnt[p & 3u]++;
        if (p == 0xA2u) {
            static int psg_budget = 24;
            g_psgsel++;
            if (psg_budget > 0 && g_frame >= 415) {
                psg_budget--;
                printf("f%ld PSG-RD #A2 -> %02X @PC=%04X sel=%u R7=%02X R14=%02X R15=%02X pb=%02X\n",
                       g_frame, v, cpu.pc, psg_debug_select(),
                       psg_debug_r7(), psg_debug_r14(), psg_debug_r15(),
                       psg_debug_pb());
            }
        }
    }
    /* Does the game poll the keyboard matrix directly through the 8255?
     * Log port B (#A9, columns) and port C (#AA, row select) reads. */
    if (p == 0xA9u || p == 0xAAu) {
        /* Full-run histogram: which rows does the game EVER select, and from
         * which PC?  Answers whether any handler (e.g. the vblank IRQ) scans a
         * row other than 7, such as row 8 (SPACE/arrows). */
        if (p == 0xA9u) {
            unsigned r = (unsigned)(g_pc_latch & 0x0Fu);
            g_rowhist[r]++;
            if (g_rowfirstpc[r] == 0u) g_rowfirstpc[r] = cpu.pc;
            g_rowlastval[r] = v;
        }
        static int kb_budget = 120;
        if (kb_budget > 0 && g_frame >= 415 && g_frame <= 460) {
            kb_budget--;
            printf("f%ld KBD-IN #%02X -> %02X (row=%u) @PC=%04X\n",
                   g_frame, p, v, (unsigned)(g_pc_latch & 0x0Fu), cpu.pc);
        }
    }
    /* Watch every VDP status read: Lode Runner runs with IE0=0 and syncs by
     * polling bit7 (FG/vblank) of the status register. */
    if (p == 0x89u || p == 0x99u) {
        static int st_budget = 400;
        if (st_budget > 0 && g_frame >= 138 && g_frame <= 220) {
            st_budget--;
            printf("f%ld ST-RD port=%02X -> %02X (FG=%d) @PC=%04X\n",
                   g_frame, p, v, (v >> 7) & 1, cpu.pc);
        }
    }
    return v;
}

static void cb_out(z80 *z, uint8_t p, uint8_t v)
{
    (void)z;
    g_portw[p]++;
    if (g_portwpc[p] == 0u) g_portwpc[p] = cpu.pc;
    /* Ground truth on the game's PSG port-A configuration: log writes to R7
     * (bit6 = port A direction) and R14 (port A latch) during the title loop. */
    if (p == 0xA1u && g_frame >= 415) {
        uint8_t sel = psg_debug_select();
        if (sel == 7u || sel == 14u) {
            static int pr_budget = 60;
            if (pr_budget > 0) {
                pr_budget--;
                printf("f%ld PSG-WR R%u = %02X  (R7 bit6=%u) @PC=%04X\n",
                       g_frame, sel, v, (v >> 6) & 1u, cpu.pc);
            }
        }
    }
    if (p == 0x98) {
        /* Where does the game's screen data actually land?  Bucket every VDP
         * write-port byte by 256-byte VRAM block; the populated blocks reveal
         * the true name/pattern/colour tables independently of R2-R6. */
        uint32_t wa = vdp_debug_write_addr();
        const char *vwf = getenv("VWWIN");
        long a = 0, b = 0;
        /* VWLOG=<file>: authoritative binary trace of every VDP data write
         * (frame, VRAM addr, value, issuing PC) for offline analysis. */
        if (g_vwlog) {
            struct { uint32_t f; uint32_t wa; uint8_t v; uint32_t pc;
                     uint32_t ret; } rec;
            rec.f = (uint32_t)g_frame; rec.wa = wa;
            rec.v = v; rec.pc = cpu.pc;
            rec.ret = (uint32_t)(slots_read(cpu.sp) |
                                 (slots_read((uint16_t)(cpu.sp + 1u)) << 8));
            fwrite(&rec, 1u, sizeof rec, g_vwlog);
        }
        g_wrpc[cpu.pc >> 8]++;
        if (vwf && sscanf(vwf, "%ld,%ld", &a, &b) == 2) {
            if (g_frame >= a && g_frame <= b) { g_vwhist[(wa >> 8) & 0x1FFu]++; g_vwlast = wa; }
        } else {
            g_vwhist[(wa >> 8) & 0x1FFu]++;
            g_vwlast = wa;
        }
        /* Did the game EVER put real content into its own name table?  In
         * SCREEN 2 the maze is 768 bytes at NT; log the first touches. */
        if (wa >= 0x3800u && wa < 0x3B00u) {
            g_ntw[0]++;
            if (cpu.pc >= 0x4000u) g_ntw[1]++;
        }
        if (wa >= 0x3800u && wa < 0x3B00u && v != 0u && g_frame >= 147u) {
            static int nt_budget = 40;
            if (nt_budget > 0) {
                nt_budget--;
                printf("f%ld NTW [%04X] = %02X @PC=%04X\n", g_frame, wa, v, cpu.pc);
            }
        }
        /* Who touches the 0x3800 block that our R2 decode treats as the name
         * table?  Frame + PC + value distinguishes a VRAM-size probe from the
         * game's real screen writes. */
        if (wa >= 0x3800u && wa < 0x3810u && v != 0u) {
            static int ramp_budget = 48;
            if (ramp_budget > 0) {
                ramp_budget--;
                printf("f%ld RAMPW [%04X] = %02X @PC=%04X\n", g_frame, wa, v, cpu.pc);
            }
        }
        /* Log the target VRAM byte when it lands in the name-table window, so
         * we can see whether the game itself writes the ramp or real maze. */
        static int vdpw_budget = 500;
        if (wa < 0x1800u && v != 0u && vdpw_budget > 0 &&
            g_frame >= 144 && g_frame <= 200) {
            vdpw_budget--;
            printf("f%ld VDPW-PAT [%04X] = %02X @PC=%04X\n", g_frame, wa, v, cpu.pc);
        }
    }
    if (p == 0x99u || p == 0x9Bu) {
        /* Raw control-port byte stream, decoded offline by a script: the
         * in-process shadow can desync, so keep an authoritative capture. */
        const char *raw = getenv("VDPRAW");
        if (raw && g_frame <= atol(raw))
            printf("RAW f%ld %02X %02X @%04X\n", g_frame, p, v, cpu.pc);
    }
    if ((p == 0x99u || p == 0x9Bu) && vdp_latch_state && (v & 0xC0u) == 0x40u) {
        /* Authoritative 'set VDP write address' event: 2nd control byte of a
         * read/write command.  VWSEEK=<from>,<to> lists every block the game
         * targets, which pins down its real table layout. */
        const char *sw = getenv("VWSEEK");
        long a = 0, b = 0;
        uint32_t sa = (uint32_t)(vdp_my_latch | ((v & 0x3Fu) << 8));
        g_seekhist[(sa >> 8) & 0x1FFu]++;
        if (cpu.pc >= 0x4000u) g_seekhist_cart[(sa >> 8) & 0x3Fu]++;
        if (sa >= 0x3800u && sa < 0x3C00u) {
            static int nts_budget = 120;
            if (nts_budget > 0) {
                nts_budget--;
                printf("f%ld NTSEEK %04X @PC=%04X\n", g_frame, sa, cpu.pc);
            }
        }
        if (sw && sscanf(sw, "%ld,%ld", &a, &b) == 2 &&
            g_frame >= a && g_frame <= b) {
            static int sk_budget = 300;
            if (sk_budget > 0) {
                sk_budget--;
                printf("f%ld SEEK %04X @PC=%04X\n", g_frame, sa, cpu.pc);
            }
        }
    }
    if (p == 0x99 || p == 0x9B) {
        if (!vdp_latch_state) {
            vdp_my_latch = v;                 /* first byte */
        } else if ((v & 0xC0u) == 0x80u) {    /* register write: reg=v&0x3F */
            uint8_t rn = (uint8_t)(v & 0x3Fu);
            vdp_reg[rn] = vdp_my_latch;
            if (g_verbose || rn == 1u)
                printf("f%ld   => VDP R%u = %02X\n", g_frame, rn, vdp_my_latch);
        }
        vdp_latch_state ^= 1;
    } else if (p == 0x98 || p == 0x9A) {
        vdp_latch_state = 0;
    } else if (p == 0xA8) {
        /* Every primary-slot write, with the PC that made it and the per-page
         * decode, so we can see whether C-BIOS ever points page 2 at the cart. */
        printf("f%ld OUT PPI #A8 = %02X  @PC=%04X  (p3=%u p2=%u p1=%u p0=%u)\n",
               g_frame, v, cpu.pc, (v>>6)&3, (v>>4)&3, (v>>2)&3, v&3);
    } else if (p >= 0xA9 && p <= 0xAB) {
        if (p == 0xAAu) {
            g_selhist[v & 0x0Fu]++;            /* authoritative row-select count */
            if (g_frame >= 415) {
                static int aa_budget = 200;
                if (aa_budget > 0) { aa_budget--; printf("f%ld SEL row=%u\n", g_frame, v & 0x0Fu); }
            }
        }
        if (p == 0xAAu) g_pc_latch = v;
        if (g_verbose) printf("f%ld OUT PPI #%02X = %02X @PC=%04X\n", g_frame, p, v, cpu.pc);
    }
    msx_bus_out(p, v);
}

static uint8_t *read_file(const char *path, uint32_t *len)
{
    FILE *f = fopen(path, "rb");
    long n; uint8_t *b;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); n = ftell(f); rewind(f);
    b = (uint8_t *)malloc((size_t)n);
    if (fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); fclose(f); return NULL; }
    fclose(f); *len = (uint32_t)n; return b;
}

static uint16_t s_fb[256 * 212];

static uint32_t ink(void)
{
    uint16_t bg = s_fb[0];
    uint32_t i, n = 0;
    for (i = 0; i < 256u * 212u; i++) if (s_fb[i] != bg) n++;
    return n;
}

static void write_ppm(const char *path)
{
    FILE *f = fopen(path, "wb");
    uint32_t i;
    if (!f) return;
    fprintf(f, "P6\n256 212\n255\n");
    for (i = 0; i < 256u * 212u; i++) {
        uint16_t p = s_fb[i];
        fputc((uint8_t)(((p >> 11) & 0x1F) << 3), f);
        fputc((uint8_t)(((p >> 5)  & 0x3F) << 2), f);
        fputc((uint8_t)(( p        & 0x1F) << 3), f);
    }
    fclose(f);
}

/* The 32x24 name table rendered as text.  In SCREEN 1 the MSX pattern ROM
 * maps codes 0x20..0x7F onto ASCII 0x20..0x7F, so the table reads directly
 * as the on-screen text; 0x00..0x1F are blanks/flashing blocks. */
static void dump_name_table(long f)
{
    uint32_t vsz = 0;
    uint8_t *vv = vdp_vram(&vsz);
    uint16_t ntb = (uint16_t)((vdp_debug_reg(2) & 0x0F) << 10);
    int r, c, nz = 0;
    if (!vv || (uint32_t)(ntb + 0x300u) > vsz) { printf("f%ld NT out of range\n", f); return; }
    printf("=== f%ld name table @%04X (R1=%02X R2=%02X R3=%02X R4=%02X) ===\n",
           f, ntb, vdp_debug_reg(1), vdp_debug_reg(2), vdp_debug_reg(3), vdp_debug_reg(4));
    for (r = 0; r < 24; r++) {
        putchar('|');
        for (c = 0; c < 32; c++) {
            uint8_t e = vv[ntb + (uint16_t)(r * 32 + c)];
            if (e) nz++;
            putchar(e >= 0x20u && e < 0x80u ? e : (e ? '#' : ' '));
        }
        putchar('|');
        putchar('\n');
    }
    printf("=== %d non-zero cells ===\n", nz);
    fflush(stdout);
}

/* Render a 768-byte region as SCREEN-1 glyph text, so a name table can be
 * recognised by eye even when the emulator is reading the wrong base. */
static void dump_nt_at(long f, uint32_t addr, const char *tag)
{
    uint32_t vsz = 0;
    uint8_t *vv = vdp_vram(&vsz);
    int r, c, nz = 0;
    if (!vv || addr + 0x300u > vsz) return;
    printf("--- f%ld %s @%04X ---\n", f, tag, addr);
    for (r = 0; r < 24; r++) {
        putchar('|');
        for (c = 0; c < 32; c++) {
            uint8_t e = vv[addr + (uint32_t)(r * 32 + c)];
            if (e) nz++;
            putchar(e >= 0x20u && e < 0x80u ? e : (e ? '#' : ' '));
        }
        printf("| %s\n", r == 0 ? (nz ? "has-content" : "blank") : "");
    }
    fflush(stdout);
}

/* How much of each 1 KiB VRAM block is non-zero: shows which regions the
 * game actually populated (font, name table, colour table, sprites). */
static void dump_vram_map(long f)
{
    uint32_t vsz = 0;
    uint8_t *vv = vdp_vram(&vsz);
    uint32_t b;
    printf("=== f%ld VRAM occupancy (%lu bytes) ===\n", f, (unsigned long)vsz);
    for (b = 0; b < vsz; b += 0x400u) {
        uint32_t i, nz = 0, ramp = 0;
        for (i = 0; i < 0x400u && b + i < vsz; i++) {
            if (vv[b + i]) nz++;
            if (vv[b + i] == (uint8_t)((b + i) & 0xFFu)) ramp++;
        }
        printf("  %04X: %4u/1024 non-zero, %4u/1024 == addr&0xFF%s\n",
               b, nz, ramp, ramp > 900 ? "   <-- RAMP FILL" : "");
    }
    fflush(stdout);
}

static int region(uint16_t pc)
{
    if (pc < 0x4000) return 0;   /* BIOS low  */
    if (pc < 0x8000) return 1;   /* BIOS high / cartridge */
    if (pc < 0xC000) return 2;   /* logo / RAM page 2 */
    return 3;                    /* RAM page 3 */
}

static const char *region_name(int r)
{
    static const char *n[4] = { "BIOS-lo", "BIOS-hi/CART", "logo/RAM2", "RAM3" };
    return n[r & 3];
}

int main(int argc, char **argv)
{
    const char *rom_path = (argc > 1) ? argv[1] : "assets/msx/Lode Runner (1984)(Sony)[a].rom";
    long frames = (argc > 2) ? strtol(argv[2], NULL, 10) : 300;
    uint8_t *rom; uint32_t rom_len = 0;
    long f;
    int last_region = -1;
    uint16_t frame_start_pc;

    rom = read_file(rom_path, &rom_len);
    if (!rom) { fprintf(stderr, "no rom\n"); return 2; }

    if (getenv("WATCH")) g_watch = strtol(getenv("WATCH"), NULL, 16);

    if (getenv("VWLOG")) {
        g_vwlog = fopen(getenv("VWLOG"), "wb");
        if (!g_vwlog) { fprintf(stderr, "cannot open VWLOG\n"); return 2; }
    }

    slots_init();
    if (bios_load(BMSX_MACHINE_MSX, "third_party/openMSX/Contrib/cbios") != MSX_OK) {
        fprintf(stderr, "bios_load failed\n"); return 1;
    }
    slots_set_rom(rom, rom_len);

    kbd_init(); ppi_init(); psg_init(); vdp_init(BMSX_MACHINE_MSX);
    z80_init(&cpu);
    slots_reset(); kbd_reset(); ppi_reset(); psg_reset(); vdp_reset();
    z80_init(&cpu);
    cpu.read_byte = cb_read; cpu.write_byte = cb_write;
    cpu.port_in = cb_in;     cpu.port_out = cb_out;
    cpu.userdata = NULL;

    for (f = 1; f <= frames; f++) {
        unsigned long target = cpu.cyc + MSX_T_PER_FRAME;
        g_frame = f;
        g_verbose = (f <= 3);           /* quiet the port log after early boot */
        frame_start_pc = cpu.pc;

        /* Inject a SPACE keypress (MSX matrix row 5, bit 7) to test whether the
         * game is simply waiting for the player to start the level. */
        /* Sweep each row-7 key one at a time (bit b held for 24 frames).
         * row 7: bit7=RET bit6=SELECT bit5=BS bit4=STOP bit3=TAB bit2=ESC
         *        bit1=F5 bit0=F4 */
        if (getenv("SWEEP") && f >= 416 && f < 632) {
            int slot = (int)((f - 416) / 24);          /* 0..8 */
            int phase = (int)(f - 416) % 24;
            static int cur = -1;
            if (phase == 0) {
                if (cur >= 0) kbd_up(7u, (uint8_t)cur);
                cur = slot;
                if (cur < 8) { kbd_down(7u, (uint8_t)cur);
                    printf(">>> hold row7 bit%d (f%ld)\n", cur, f); }
            }
        }
        /* Single-key mode: KEY=<row>,<bit> holds one matrix cell from f416 on. */
        {
            const char *k = getenv("KEY");
            if (k) {
                int kr, kb;
                if (sscanf(k, "%d,%d", &kr, &kb) == 2) {
                    if (f == 416) kbd_down((uint8_t)kr, (uint8_t)kb);
                }
            }
        }
        /* Joystick mode: JOY=<hex> sets socket-A pressed bits from f416 on.
         * MSX_JOY layout: TRGA=01 UP=02 DOWN=04 LEFT=08 RIGHT=10 TRGB=40. */
        {
            const char *j = getenv("JOY");
            if (j) {
                int jb;
                if (sscanf(j, "%x", &jb) == 1) {
                    if (f == 416) kbd_set_joy((uint8_t)jb, 0u);
                }
            }
        }
        /* Pulse mode: PULSE=<row>,<bit> presses for 3 frames, releases for 3,
         * repeating from f416 — catches a game that wants a press edge or that
         * only samples the matrix at a particular point in its cycle. */
        {
            const char *pp = getenv("PULSE");
            if (pp) {
                int pr, pb;
                if (sscanf(pp, "%d,%d", &pr, &pb) == 2 && f >= 416) {
                    int on = (((f - 416) / 3) % 2) == 0;
                    if (on) kbd_down((uint8_t)pr, (uint8_t)pb);
                    else    kbd_up((uint8_t)pr, (uint8_t)pb);
                }
            }
        }

        /* KEYSWEEP=1: hold one matrix cell at a time, 16 frames on / 2 off,
         * walking all 80 cells.  Reports the name-table write count per key so
         * the input that unblocks the game's first screen draw is identified in
         * a single run. */
        if (getenv("KEYSWEEP") && f >= 500) {
            long idx = (f - 500) / 18;                 /* 0..79 */
            int ph = (int)((f - 500) % 18);
            static long prev = -1;
            if (idx < 80 && prev != idx) {
                if (prev >= 0) kbd_up((uint8_t)(prev / 8), (uint8_t)(prev % 8));
                prev = idx;
                kbd_down((uint8_t)(idx / 8), (uint8_t)(idx % 8));
                printf(">>> KEYSWEEP f%ld hold row%ld bit%ld\n", f, idx / 8, idx % 8);
            }
            if (idx < 80 && ph == 16) kbd_up((uint8_t)(idx / 8), (uint8_t)(idx % 8));
        }

        /* Report name-table activity every frame once boot has settled. */
        if (g_frame >= 200 && g_ntw[0])
            printf("f%ld NTW total=%u cart=%u\n", g_frame, g_ntw[0], g_ntw[1]);
        g_ntw[0] = g_ntw[1] = 0;

        /* Report how "maze-like" the name table is: count cells whose value is
         * NOT the linear ramp (i.e. real content the game drew). */
        {
            uint32_t vsz = 0; uint8_t *vv = vdp_vram(&vsz);
            uint16_t ntb = (uint16_t)((vdp_debug_reg(2) & 0x0F) << 10);
            int i, off = 0;
            for (i = 0; i < 768; i++) if (vv[ntb + i] != (uint8_t)i) off++;
            if (f % 20 == 0) printf("f%ld nt_offramp=%d/768\n", f, off);
        }

        while ((long)(target - cpu.cyc) > 0) {
            unsigned long before = cpu.cyc;
            int reg;
            /* coarse coverage map: first entry into each 0x80-aligned block */
            {
                static uint8_t seen[512];
                unsigned blk = (unsigned)(cpu.pc >> 7);
                if (blk < 512u && !seen[blk]) {
                    seen[blk] = 1;
                    printf("f%ld ENTER block %04X (PC=%04X)\n", f, blk << 7, cpu.pc);
                }
            }
            z80_step(&cpu);
            /* STEP=<frame>,<pc>,<count>: linear instruction trace with opcode
             * bytes, so a wait loop can be disassembled from its real control
             * flow instead of by guessing at linear dumps. */
            if (!g_step_parsed) {
                const char *s = getenv("STEP");
                long fr = -1, pc = 0, cnt = 200;
                g_step_parsed = 1;
                if (s && sscanf(s, "%ld,%lx,%ld", &fr, &pc, &cnt) >= 2) {
                    g_step_frame = fr; g_step_pc = pc;
                    g_step_count = cnt > 0 ? cnt : 200;
                }
            }
            if (g_step_left > 0) {
                g_step_left--;
                printf("f%ld %04X: %02X %02X %02X %02X  A=%02X F=%02X BC=%04X"
                       " DE=%04X HL=%04X IX=%04X IY=%04X SP=%04X cyc=%lu\n",
                       f, cpu.pc, slots_read(cpu.pc), slots_read(cpu.pc + 1u),
                       slots_read(cpu.pc + 2u), slots_read(cpu.pc + 3u),
                       cpu.a, (uint8_t)((cpu.sf << 7) | (cpu.zf << 6) |
                                        (cpu.pf << 2) | cpu.cf),
                       (uint16_t)((cpu.b << 8) | cpu.c),
                       (uint16_t)((cpu.d << 8) | cpu.e),
                       (uint16_t)((cpu.h << 8) | cpu.l), cpu.ix, cpu.iy,
                       cpu.sp, cpu.cyc);
            } else if (g_step_frame >= 0 && f == g_step_frame &&
                       cpu.pc == (uint16_t)g_step_pc) {
                g_step_left = g_step_count;
                g_step_frame = -1;
            }
            /* HIST=<from>,<to>: 16-byte PC histogram over a frame window, to
             * locate a loop the game cannot escape. */
            {
                const char *hx = getenv("HIST");
                if (hx) {
                    long a = 0, b = 0;
                    if (sscanf(hx, "%ld,%ld", &a, &b) == 2 && f >= a && f <= b)
                        g_pchist[(cpu.pc >> 4) & 0xFFFu]++;
                }
            }
            if (cpu.pc == 0x4E4Au) g_hit_4e4a++;
            if (cpu.pc == 0x4E7Bu) g_hit_4e7b++;
            /* Which BIOS input routines does the game call? */
            switch (cpu.pc) {
            case 0x00C0u: g_bios_c0++; break;   /* KROW0  rows 0-3   */
            case 0x00C3u: g_bios_c3++; break;   /* KROW1  rows 4-7   */
            case 0x00C6u: g_bios_c6++; break;   /* KROW2  rows 8-11  */
            case 0x00D3u: g_bios_d3++; break;   /* STICK  joystick   */
            case 0x00DBu: g_bios_db++; break;   /* STRIG  trigger    */
            case 0x0141u: g_bios_141++; break;  /* row read          */
            case 0x009Fu: g_bios_9f++; break;   /* SYNCHR vblank     */
            default: break;
            }
            /* Log every BIOS row-read: which row (A) and, after the call, the
             * returned column bits.  Shows all rows the game polls. */
            if (cpu.pc == 0x0141u && g_frame >= 415) {
                static int rr_budget = 80;
                if (rr_budget > 0) {
                    rr_budget--;
                    printf("f%ld ROWREAD A=%u\n", g_frame, cpu.a & 0x0Fu);
                }
            }
            /* Detect the game waiting for a vblank interrupt: HALT + IM1 entry. */
            if (cpu.pc == 0x0038u && g_frame >= 160) {
                static int im_budget = 40;
                if (im_budget > 0) {
                    im_budget--;
                    printf("f%ld IM1@0038 iff1=%d IE0=%d R1=%02X\n",
                           g_frame, cpu.iff1, (vdp_debug_reg(1) >> 5) & 1,
                           vdp_debug_reg(1));
                }
            }
            if (cpu.halted && g_frame >= 160) {
                static int h_budget = 20;
                if (h_budget > 0) {
                    h_budget--;
                    printf("f%ld HALT at PC=%04X IE0=%d\n", g_frame, cpu.pc,
                           (vdp_debug_reg(1) >> 5) & 1);
                }
            }
            /* Catch the game's stuck helper: 0x532C XOR A / CALL WRVRM / delay.
             * Log HL (VRAM target), A (value) and the caller return address. */
            if (cpu.pc == 0x532Cu) {
                static int hl_budget = 60;
                if (hl_budget > 0 && g_frame >= 205 && g_frame <= 212) {
                    uint16_t hl = (uint16_t)((cpu.h << 8) | cpu.l);
                    uint16_t ra = (uint16_t)(slots_read(cpu.sp) |
                                             (slots_read((uint16_t)(cpu.sp + 1u)) << 8));
                    hl_budget--;
                    printf("f%ld @532C HL=%04X A=%02X ret=%04X\n",
                           g_frame, hl, cpu.a, ra);
                }
            }
            /* WRTVDP (#0047): MSX convention is A = register, E = value.  Log
             * the caller's arguments next to what C-BIOS actually shifts out,
             * so a lost or altered register value is unambiguous. */
            if (cpu.pc == 0x0047u && g_frame >= 130 && g_frame <= 145) {
                uint16_t ra = (uint16_t)(slots_read(cpu.sp) |
                                         (slots_read((uint16_t)(cpu.sp + 1u)) << 8));
                printf("f%ld WRTVDP A=%02X B=%02X C=%02X D=%02X E=%02X H=%02X L=%02X"
                       " (HL)=%02X ret=%04X\n",
                       g_frame, cpu.a, cpu.b, cpu.c, cpu.d, cpu.e, cpu.h, cpu.l,
                       slots_read((uint16_t)((cpu.h << 8) | cpu.l)), ra);
            }
            if (cpu.pc == 0x0234u && g_frame >= 130 && g_frame <= 145)
                printf("f%ld   WRTVDP out1 A=%02X E=%02X\n", g_frame, cpu.a, cpu.e);
            vdp_advance((uint32_t)(cpu.cyc - before));
            if (vdp_vblank_taken()) { g_vblanks++; z80_gen_int(&cpu, 0xFF); }

            reg = region(cpu.pc);
            if (reg != last_region) {
                printf("f%ld region -> %s (PC=%04X)\n", f, region_name(reg), cpu.pc);
                last_region = reg;
            }
        }

        /* NT=<hex>: from frame NT_FROM on, reprogram R2 through the real #99
         * port so a candidate name-table address can be rendered and judged. */
        {
            const char *nx = getenv("NT");
            const char *nf = getenv("NT_FROM");
            long from = nf ? atol(nf) : 800;
            if (nx && f >= from && f < from + 2) {
                uint8_t val = (uint8_t)strtol(nx, NULL, 16);
                msx_bus_out(0x99u, val);
                msx_bus_out(0x99u, (uint8_t)(0x82u));
                printf("f%ld EXPERIMENT: forced R2 = %02X (NT = %04X)\n",
                       f, val, (uint32_t)(val & 0x0F) << 10);
            }
        }

        vdp_render(s_fb);
        /* REGS=1: the VDP register file at the moments that matter, so the
         * screen-mode decode can be checked against the chip's own formula. */
        if (getenv("REGS")) {
            static const long reg_at[] = { 140, 145, 418, 425, 440, 500, 1300 };
            unsigned q;
            for (q = 0; q < sizeof reg_at / sizeof reg_at[0]; q++) {
                if (f == reg_at[q]) {
                    printf("f%ld REGS", f);
                    { uint8_t k; for (k = 0u; k < 8u; k++)
                          printf(" R%d=%02X", k, vdp_debug_reg(k)); }
                    printf("  mode=%02X\n",
                           (unsigned)(((vdp_debug_reg(0) & 0x0Eu) << 1) |
                                      ((vdp_debug_reg(1) & 0x08u) >> 2) |
                                      ((vdp_debug_reg(1) & 0x10u) >> 4)));
                }
            }
        }
        /* Framebuffer fingerprint: reveals the attract cycle period and any
         * steady state, so an input can be timed to a phase boundary. */
        {
            uint32_t h = 2166136261u, i;
            for (i = 0; i < 256u * 212u; i++) {
                h = (h ^ s_fb[i]) * 16777619u;
            }
            if (h != g_last_fbhash) {
                printf("f%ld FBHASH=%08X (changed)\n", f, h);
                g_last_fbhash = h;
            }
        }
        if (f >= 410 && f <= 430)
            printf("f%ld EFA4=%02X EFAA=%02X EF70=%02X\n", f,
                   slots_read(0xEFA4u), slots_read(0xEFAAu), slots_read(0xEF70u));
        {
            static const long dump_at[] = { 60, 100, 130, 500, 650, 800, 950, 1100, 1300 };
            unsigned d;
            for (d = 0; d < sizeof dump_at / sizeof dump_at[0]; d++) {
                if (f == dump_at[d]) {
                    char path[64];
                    uint32_t vs = 0; uint8_t *vv = vdp_vram(&vs);
                    sprintf(path, "/tmp/lr_f%ld.ppm", f);
                    write_ppm(path);
                    printf(">>> dumped %s\n", path);
                    if (getenv("VRAMDUMP") && vs >= 0x4000u) {
                        sprintf(path, "/tmp/vram_f%ld.bin", f);
                        FILE *vf = fopen(path, "wb");
                        if (vf) { fwrite(vv, 1u, 0x4000u, vf); fclose(vf);
                                  printf(">>> dumped %s\n", path); }
                    }
                    dump_name_table(f);
                    dump_vram_map(f);
                    dump_nt_at(f, 0x0000u, "NT?0000");
                    dump_nt_at(f, 0x0800u, "NT?0800");
                    dump_nt_at(f, 0x1800u, "NT?1800");
                }
            }
            /* TEXT=<period>: cheap name-table trace so the screen can be read
             * as text every few frames instead of eyeballing pixels. */
            {
                const char *t = getenv("TEXT");
                long per = t ? atol(t) : 0;
                if (per > 0 && f % per == 0) dump_name_table(f);
            }
        }
        if (f <= 3 || (f % 10) == 0 || f == frames) {
            printf("f%ld PC=%04X cyc=%lu ink=%u halt=%d iff1=%d | R1=%02X(IE0=%d DISP=%d) ST=%02X irqp=%d vblanks=%ld\n",
                   f, frame_start_pc, cpu.cyc, ink(), cpu.halted, cpu.iff1,
                   vdp_debug_reg(1), (vdp_debug_reg(1)>>5)&1, (vdp_debug_reg(1)>>6)&1,
                   vdp_debug_status(), vdp_debug_irq_pending(), g_vblanks);
        }
        if (f >= 140 && f <= 175) {
            uint32_t vsz = 0; uint8_t *vv = vdp_vram(&vsz);
            uint16_t ntb = (uint16_t)((vdp_debug_reg(2) & 0x0F) << 10);
            int q;
            printf("f%ld nt=%04X[", f, ntb);
            for (q = 0; q < 8; q++) printf("%02X ", vv[ntb + q]);
            printf("] row8=[");
            for (q = 0; q < 8; q++) printf("%02X ", vv[ntb + 256 + q]);
            printf("]\n");
        }
    }
    printf("trace end: %ld frames, cyc=%lu PC=%04X\n", frames, cpu.cyc, cpu.pc);
    printf("PSG port read counts: #A0=%u #A1=%u #A2=%u #A3=%u\n",
           g_psgcnt[0], g_psgcnt[1], g_psgcnt[2], g_psgcnt[3]);
    printf("handler entries: 4E4A=%u  4E7B(row8)=%u\n", g_hit_4e4a, g_hit_4e7b);
    printf("BIOS input calls: KROW0(C0)=%u KROW1(C3)=%u KROW2(C6)=%u "
           "STICK(D3)=%u STRIG(DB)=%u rowread(141)=%u SYNCHR(9F)=%u\n",
           g_bios_c0, g_bios_c3, g_bios_c6, g_bios_d3, g_bios_db,
           g_bios_141, g_bios_9f);
    printf("=== #AA row-select writes (whole run) ===\n");
    {
        int r;
        for (r = 0; r < 16; r++)
            if (g_selhist[r]) printf("  row %2d: %6u selects\n", r, g_selhist[r]);
    }
    {
        int r;
        printf("=== row-select histogram (#A9 reads per row, whole run) ===\n");
        for (r = 0; r < 16; r++)
            if (g_rowhist[r])
                printf("row %2d: %6u reads  firstPC=%04X  lastval=%02X\n",
                       r, g_rowhist[r], g_rowfirstpc[r], g_rowlastval[r]);
    }
    {
        static const int TOP = 24;
        unsigned tot = 0;
        int i, n = 0, k;
        int top[24];
        for (i = 0; i < 4096; i++) {
            tot += g_pchist[i];
            if (!g_pchist[i]) continue;
            for (k = 0; k < n; k++) {
                if (g_pchist[i] > g_pchist[top[k]]) break;
            }
            if (k < TOP) {
                int m = (n < TOP) ? n : TOP - 1;
                for (; m > k; m--) top[m] = top[m - 1];
                top[k] = i;
                if (n < TOP) n++;
            }
        }
        if (tot) {
            printf("=== PC histogram: %u samples, top %d 16-byte buckets ===\n",
                   tot, n);
            for (k = 0; k < n; k++)
                printf("  %04X-%04X: %8u (%.1f%%)\n", top[k] << 4,
                       (top[k] << 4) | 0xFu, g_pchist[top[k]],
                       100.0 * (double)g_pchist[top[k]] / (double)tot);
        }
    }
    {
        int b;
        printf("=== I/O port usage (whole run) ===\n");
        {
            int p;
            for (p = 0; p < 256; p++)
                if (g_portw[p] || g_portr[p])
                    printf("  #%02X: OUT=%-9u firstPC=%04X  IN=%-9u firstPC=%04X\n",
                           p, g_portw[p], g_portwpc[p], g_portr[p], g_portrpc[p]);
        }
        printf("=== VRAM #98 write histogram (per 256-byte block) ===\n");
        for (b = 0; b < 512; b++)
            if (g_vwhist[b]) printf("  %05X: %8u writes\n", b << 8, g_vwhist[b]);
        printf("  last write addr = %05X\n", g_vwlast);
    }
    {
        int q;
        printf("=== 'set VDP write address' commands per 256-byte block ===\n");
        for (q = 0; q < 64; q++)
            if (g_seekhist[q]) printf("  %04X: %7u seeks\n", q << 8, g_seekhist[q]);
        printf("=== ...same, restricted to seeks issued from cart code (PC>=4000) ===\n");
        for (q = 0; q < 64; q++)
            if (g_seekhist_cart[q])
                printf("  %04X: %7u seeks\n", q << 8, g_seekhist_cart[q]);
    }
    {
        static const int TOP = 16;
        unsigned tot = 0;
        int i, n = 0, k;
        int top[16];
        for (i = 0; i < 256; i++) {
            tot += g_wrpc[i];
            if (!g_wrpc[i]) continue;
            for (k = 0; k < n; k++) {
                if (g_wrpc[i] > g_wrpc[top[k]]) break;
            }
            if (k < TOP) {
                int m = (n < TOP) ? n : TOP - 1;
                for (; m > k; m--) top[m] = top[m - 1];
                top[k] = i;
                if (n < TOP) n++;
            }
        }
        printf("=== PC buckets issuing #98 VDP writes (%u total) ===\n", tot);
        for (k = 0; k < n; k++)
            printf("  %04X-%04X: %8u\n", top[k] << 8, (top[k] << 8) | 0xFFu,
                   g_wrpc[top[k]]);
    }
    write_ppm("/tmp/msx_trace_final.ppm");
    {
        uint8_t pa = slots_ppi_a();
        int pg;
        printf("ppi_a = %02X  (page3=%u page2=%u page1=%u page0=%u)\n",
               pa, (pa>>6)&3, (pa>>4)&3, (pa>>2)&3, pa&3);
        printf("mem 0x8000..: ");
        for (pg = 0; pg < 16; pg++) printf("%02X ", slots_read((uint16_t)(0x8000+pg)));
        printf("\nmem 0x4000..: ");
        for (pg = 0; pg < 8; pg++) printf("%02X ", slots_read((uint16_t)(0x4000+pg)));
        printf("\n");
    }

    {
        int r;
        uint32_t size = 0, i, nz = 0, n;
        uint8_t *vram = vdp_vram(&size);
        uint8_t R0 = vdp_debug_reg(0), R1 = vdp_debug_reg(1), R2 = vdp_debug_reg(2);
        uint16_t nt = (uint16_t)((R2 & 0x0F) << 10);
        printf("VDP regs (LIVE):");
        for (r = 0; r <= 9; r++) printf(" R%d=%02X", r, vdp_debug_reg(r));
        printf("\n  R1 DISP=%d IE0=%d M1=%d M2=%d SZ=%d\n",
               (R1>>6)&1, (R1>>5)&1, (R1>>4)&1, (R1>>3)&1, R1&1);
        printf("  R0 M3=%d M4=%d M5=%d\n", (R0>>1)&1, (R0>>2)&1, (R0>>3)&1);
        printf("  R7 = %02X (fg=%d bg=%d)\n", vdp_debug_reg(7),
               vdp_debug_reg(7)>>4, vdp_debug_reg(7)&0xF);
        printf("  name-table base = %04X, first 32 bytes:\n   ", nt);
        for (i = 0; i < 32; i++) printf("%02X ", vram[nt + i]);
        printf("\n");
        for (i = 0; i < size && i < 0x4000; i++) if (vram[i]) nz++;
        for (n = 0, i = 0; i < 768; i++) if (vram[nt + i]) n++;
        printf("VRAM[0..16K] nonzero = %u ; name-table[0..768] nonzero = %u\n", nz, n);
        {
            /* Per-row name-table map: '.' = all-zero row, else first codes.
             * Shows exactly which maze rows the game actually wrote. */
            int row, col;
            printf("name table rows (32 cols each):\n");
            for (row = 0; row < 24; row++) {
                int nzr = 0;
                for (col = 0; col < 32; col++) if (vram[nt + row*32 + col]) nzr++;
                printf(" r%02d b%d : %3d set | ", row, row/8, nzr);
                for (col = 0; col < 16 && col < 32; col++)
                    printf("%02X ", vram[nt + row*32 + col]);
                printf("\n");
            }
        }
        {
            uint32_t ct = (uint32_t)(vdp_debug_reg(3) & 0x80u) << 6;
            uint32_t pt = (uint32_t)(vdp_debug_reg(4) & 0x04u) << 11;
            uint32_t c, p, k;
            for (c = 0, i = ct; i < ct + 6144u && i < size; i++) if (vram[i]) c++;
            for (p = 0, i = pt; i < pt + 6144u && i < size; i++) if (vram[i]) p++;
            printf("ct base=%04X nonzero=%u ; pt base=%04X nonzero=%u\n", ct, c, pt, p);
            /* sample band 1 (code 256..263): name, colour, pattern row0 */
            printf("band1 sample (code, name, colour, pat[0]):\n ");
            for (k = 0; k < 8; k++) {
                uint32_t code = 256u + k;
                printf("[%u n=%02X c=%02X p=%02X] ", code,
                       vram[nt + code], vram[ct + code], vram[pt + code * 8u]);
            }
            printf("\n ");
            for (k = 0; k < 8; k++) {
                uint32_t code = 512u + k;
                printf("[%u n=%02X c=%02X p=%02X] ", code,
                       vram[nt + code], vram[ct + code], vram[pt + code * 8u]);
            }
            printf("\n");
        }
    }

    free(rom);
    return 0;
}
