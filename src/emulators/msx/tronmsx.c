/*
 * tronMSX — top-level machine: wires the Z80 core to the slot map, VDP, PSG,
 * PPI and keyboard, drives the frame loop and exposes the public surface of
 * include/emulators/tronmsx.h.  B-MSX Rev 1.05, Part 5.
 *
 * One instance per process; all state is static and bounded, no heap, and the
 * step path reads no host clock and no float (D1–D7, §8.1).  The frame is
 * exactly MSX_T_PER_FRAME T-states: each z80_step's consumed cycles are fed to
 * vdp_advance, and the single VBlank flag the VDP raises per frame becomes the
 * maskable IRQ (MSX has no periodic NMI, D6).
 */

#include "msx_int.h"
#include "msx_body.h"

#include <emulators/tronmsx.h>
#include <string.h>

#include "z80.h"

#define MSX_VERSION "tronMSX 0.1 (B-MSX Rev 1.05)"
#define MSX_PCM_FRAME 735u          /* 44100 / 60 */

static z80         s_cpu;
static int         s_ready;
static int         s_paused;
static uint16_t    s_machine = BMSX_MACHINE_MSX;
static uint64_t    s_body_hash;

static const char *s_bios_dir = "third_party/openMSX/Contrib/cbios";

void msx_set_bios_dir(const char *dir)
{
    if (dir && *dir) s_bios_dir = dir;
}

/* ---- Z80 bus callbacks -------------------------------------------------- */

static uint8_t cpu_read(void *ud, uint16_t addr)
{
    (void)ud;
    return slots_read(addr);
}

static void cpu_write(void *ud, uint16_t addr, uint8_t value)
{
    (void)ud;
    slots_write(addr, value);
}

static uint8_t cpu_in(z80 *z, uint8_t port)
{
    (void)z;
    return msx_bus_in(port);
}

static void cpu_out(z80 *z, uint8_t port, uint8_t value)
{
    (void)z;
    msx_bus_out(port, value);
}

/* z80_init clears the callback pointers, so they are (re)assigned here after
 * every init/reset that runs it. */
static void cpu_wire(void)
{
    s_cpu.read_byte  = cpu_read;
    s_cpu.write_byte = cpu_write;
    s_cpu.port_in    = cpu_in;
    s_cpu.port_out   = cpu_out;
    s_cpu.userdata   = NULL;
}

/* ---- FNV-1a 64 (D7) ----------------------------------------------------- */

uint64_t msx_fnv1a64(const void *data, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint64_t h = 14695981039346656037ULL;
    uint32_t i;
    for (i = 0; i < len; i++) {
        h ^= (uint64_t)p[i];
        h *= 1099511628211ULL;
    }
    return h;
}

/* ---- lifecycle ---------------------------------------------------------- */

int msx_init(const msx_cfg_t *cfg)
{
    uint16_t machine = BMSX_MACHINE_MSX;
    int rc;

    if (cfg && cfg->machine) machine = cfg->machine;
    if (machine != BMSX_MACHINE_MSX &&
        machine != BMSX_MACHINE_MSX2 &&
        machine != BMSX_MACHINE_MSX2P) return MSX_ERR_MACHINE;

    s_machine  = machine;
    s_body_hash = 0u;
    s_paused   = 0;

    slots_init();
    rc = bios_load(machine, s_bios_dir);
    if (rc != MSX_OK) return rc;

    kbd_init();
    ppi_init();
    psg_init();
    vdp_init((uint8_t)machine);

    z80_init(&s_cpu);
    cpu_wire();

    s_ready = 1;
    msx_reset();
    return MSX_OK;
}

void msx_shutdown(void)
{
    s_ready = 0;
    s_paused = 0;
}

void msx_reset(void)
{
    slots_reset();
    kbd_reset();
    ppi_reset();
    psg_reset();
    vdp_reset();

    z80_init(&s_cpu);          /* PC=0, SP=0xFFFF, IFF cleared, IM 0 */
    cpu_wire();
}

void msx_pause(BOOL on)
{
    s_paused = on ? 1 : 0;
}

/* ---- frame loop --------------------------------------------------------- */

void msx_run_frame(void)
{
    unsigned long target;

    if (!s_ready || s_paused) return;

    target = s_cpu.cyc + MSX_T_PER_FRAME;
    while ((long)(target - s_cpu.cyc) > 0) {
        unsigned long before = s_cpu.cyc;
        z80_step(&s_cpu);
        vdp_advance((uint32_t)(s_cpu.cyc - before));
        if (vdp_vblank_taken()) z80_gen_int(&s_cpu, 0xFFu);
    }
}

void msx_blit_rgb565(uint16_t *fb)
{
    if (!fb) return;
    if (!s_ready) {
        memset(fb, 0, (size_t)MSX_SCREEN_W * MSX_SCREEN_H * sizeof(uint16_t));
        return;
    }
    vdp_render(fb);
}

/* ---- cartridge ---------------------------------------------------------- */

int msx_load_rom(const uint8_t *rom, uint32_t size, uint32_t entry)
{
    if (!rom || !size) return MSX_ERR_SIZE;
    if (size > MSX_ROM_SIZE) return MSX_ERR_SIZE;
    /* A plain ROM cartridge is entered by the BIOS at 0x4000; any other vector
     * would have to live inside the mapped page. */
    if (entry && (entry < 0x4000u || entry > 0x7FFFu)) return MSX_ERR_ENTRY;

    slots_set_rom(rom, size);
    s_body_hash = msx_fnv1a64(rom, size);
    return MSX_OK;
}

/* ---- input -------------------------------------------------------------- */

void msx_kbd_down(uint8_t row, uint8_t col) { kbd_down(row, col); }
void msx_kbd_up(uint8_t row, uint8_t col)   { kbd_up(row, col); }

void msx_joy_set(uint8_t bits)
{
    kbd_set_joy(bits, 0u);      /* socket A; socket B stays idle */
}

/* ---- audio -------------------------------------------------------------- */

void msx_psg_submit_pcm(int16_t *buf, uint32_t *frames_out)
{
    uint32_t n = 0u;
    if (buf) n = psg_render(buf, MSX_PCM_FRAME);
    if (frames_out) *frames_out = n;
}

/* ---- introspection ------------------------------------------------------ */

uint64_t msx_total_cycles(void) { return (uint64_t)s_cpu.cyc; }
uint64_t msx_body_hash(void)    { return s_body_hash; }
const char *msx_version_string(void) { return MSX_VERSION; }
