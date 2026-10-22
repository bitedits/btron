/*
 * tronMSX — top-level machine: wires the Z80 / R800 core to the slot map, VDP,
 * PSG, PPI and keyboard, drives the frame loop and exposes the public surface of
 * include/emulators/tronmsx.h.  B-MSX Rev 1.05, Part 5.
 *
 * Full support for the complete set of 18 MSX2 / MSX2+ / Turbo R machines
 * with TurboR R800, V9938/V9958, S1985, T9769B, T9769C emulation.
 */

#include "msx_int.h"
#include "msx_body.h"

#include <emulators/tronmsx.h>
#include <string.h>
#include <stdio.h>

#include "z80.h"
#include "r800.h"
#include "t9769c.h"

#define MSX_VERSION "tronMSX 0.2 (TurboR R800 / MSX2 / MSX2+)"
#define MSX_PCM_FRAME 735u          /* 44100 / 60 */

extern t9769c_t g_t9769c;

static z80         s_cpu;
static int         s_ready;
static int         s_paused;
static uint16_t    s_machine = BMSX_MACHINE_MSXTR_CBIOS;
static uint64_t    s_body_hash;

static const char *s_bios_dir;

void msx_set_bios_dir(const char *dir)
{
    if (dir && *dir) s_bios_dir = dir;
}

/* ---- CPU bus callbacks -------------------------------------------------- */

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
    /* C-BIOS disk BDOS stub. Not a machine port. */
    if (port == 0xD4u) { dsk_bdos(z); return; }
    (void)z;
    msx_bus_out(port, value);
}

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

/* ---- Machine resolution ------------------------------------------------- */

static uint16_t resolve_machine(const msx_cfg_t *cfg)
{
    if (cfg && cfg->bios_key && *cfg->bios_key) {
        const char *k = cfg->bios_key;
        if (strstr(k, "MSX2 - C-BIOS FDD - BR") || strstr(k, "MSX2_FDD_BR")) return BMSX_MACHINE_MSX2_CBIOS_FDD_BR;
        if (strstr(k, "MSX2 - C-BIOS FDD - EU") || strstr(k, "MSX2_FDD_EU")) return BMSX_MACHINE_MSX2_CBIOS_FDD_EU;
        if (strstr(k, "MSX2 - C-BIOS FDD - JP") || strstr(k, "MSX2_FDD_JP")) return BMSX_MACHINE_MSX2_CBIOS_FDD_JP;
        if (strstr(k, "MSX2 - C-BIOS FDD")      || strstr(k, "MSX2_FDD"))     return BMSX_MACHINE_MSX2_CBIOS_FDD;
        if (strstr(k, "MSX2 - C-BIOS - BR")     || strstr(k, "MSX2_BR"))       return BMSX_MACHINE_MSX2_CBIOS_BR;
        if (strstr(k, "MSX2 - C-BIOS - EU")     || strstr(k, "MSX2_EU"))       return BMSX_MACHINE_MSX2_CBIOS_EU;
        if (strstr(k, "MSX2 - C-BIOS - JP")     || strstr(k, "MSX2_JP"))       return BMSX_MACHINE_MSX2_CBIOS_JP;
        if (strstr(k, "MSX2 - C-BIOS")          || strstr(k, "msx2"))          return BMSX_MACHINE_MSX2_CBIOS;

        if (strstr(k, "MSX2+ - C-BIOS FDD - BR") || strstr(k, "MSX2+_FDD_BR")) return BMSX_MACHINE_MSX2P_CBIOS_FDD_BR;
        if (strstr(k, "MSX2+ - C-BIOS FDD - EU") || strstr(k, "MSX2+_FDD_EU")) return BMSX_MACHINE_MSX2P_CBIOS_FDD_EU;
        if (strstr(k, "MSX2+ - C-BIOS FDD - JP") || strstr(k, "MSX2+_FDD_JP")) return BMSX_MACHINE_MSX2P_CBIOS_FDD_JP;
        if (strstr(k, "MSX2+ - C-BIOS FDD")      || strstr(k, "MSX2+_FDD"))     return BMSX_MACHINE_MSX2P_CBIOS_FDD;
        if (strstr(k, "MSX2+ - C-BIOS - BR")     || strstr(k, "MSX2+_BR"))      return BMSX_MACHINE_MSX2P_CBIOS_BR;
        if (strstr(k, "MSX2+ - C-BIOS - EU")     || strstr(k, "MSX2+_EU"))      return BMSX_MACHINE_MSX2P_CBIOS_EU;
        if (strstr(k, "MSX2+ - C-BIOS - JP")     || strstr(k, "MSX2+_JP"))      return BMSX_MACHINE_MSX2P_CBIOS_JP;
        if (strstr(k, "MSX2+ - C-BIOS")          || strstr(k, "msx2+"))         return BMSX_MACHINE_MSX2P_CBIOS;

        if (strstr(k, "turboR - C-BIOS FDD")     || strstr(k, "MSXTR_FDD"))     return BMSX_MACHINE_MSXTR_CBIOS_FDD;
        if (strstr(k, "turboR") || strstr(k, "turbor") || strstr(k, "msxtr"))  return BMSX_MACHINE_MSXTR_CBIOS;
        if (strstr(k, "msx1") || strstr(k, "MSX1"))                            return BMSX_MACHINE_MSX;
    }
    if (cfg && cfg->machine) {
        if (cfg->machine >= BMSX_MACHINE_MSX && cfg->machine <= BMSX_MACHINE_MSXTR_CBIOS_FDD) {
            return cfg->machine;
        }
    }
    return BMSX_MACHINE_MSXTR_CBIOS;
}

/* ---- lifecycle ---------------------------------------------------------- */

int msx_init(const msx_cfg_t *cfg)
{
    uint16_t machine = resolve_machine(cfg);
    int rc;

    s_machine   = machine;
    s_body_hash = 0u;
    s_paused    = 0;

    slots_init();
    slots_set_machine(machine);
    rc = bios_load(machine, s_bios_dir);
    if (rc != MSX_OK) return rc;

    kbd_init();
    ppi_init();
    psg_init();
    scc_init();
    opll_init();
    pcm_init();
    y8950_init();
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
    scc_reset();
    opll_reset();
    pcm_reset();
    y8950_reset();
    vdp_reset();

    z80_init(&s_cpu);
    cpu_wire();
}

void msx_pause(BOOL on)
{
    s_paused = on ? 1 : 0;
}

/* ---- frame loop --------------------------------------------------------- */

uint16_t msx_get_pc(void) { return s_cpu.pc; }
uint16_t msx_get_machine(void) { return s_machine; }
int msx_is_r800_active(void) { return (s_machine >= BMSX_MACHINE_MSXTR_CBIOS) && t9769c_is_r800(&g_t9769c); }

void msx_run_frame(void)
{
    uint32_t frame_vdp_t = 0;

    if (!s_ready || s_paused) return;

    while (frame_vdp_t < MSX_T_PER_FRAME) {
        int is_r800 = (s_machine >= BMSX_MACHINE_MSXTR_CBIOS) && t9769c_is_r800(&g_t9769c);
        unsigned long before = s_cpu.cyc;
        if (is_r800) {
            r800_step(&s_cpu);
        } else {
            z80_step(&s_cpu);
        }
        uint32_t delta = (uint32_t)(s_cpu.cyc - before);
        if (!delta) delta = 1;
        uint32_t bus_t = is_r800 ? ((delta + 1) >> 1) : delta;
        if (!bus_t) bus_t = 1;
        frame_vdp_t += bus_t;

        if (s_machine >= BMSX_MACHINE_MSXTR_CBIOS) {
            t9769c_advance(&g_t9769c, bus_t);
        }
        vdp_advance(bus_t);
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
    if (entry && (entry < 0x4000u || entry > 0xBFFFu)) return MSX_ERR_ENTRY;

    slots_set_rom(rom, size);
    s_body_hash = msx_fnv1a64(rom, size);
    return MSX_OK;
}

uint8_t *msx_rom_buffer(uint32_t *cap)
{
    return slots_rom_ptr(cap);
}

/* ---- input -------------------------------------------------------------- */

void msx_kbd_down(uint8_t row, uint8_t col) { kbd_down(row, col); }
void msx_kbd_up(uint8_t row, uint8_t col)   { kbd_up(row, col); }

void msx_joy_set(uint8_t bits)
{
    kbd_set_joy(bits, 0u);
}

/* ---- audio -------------------------------------------------------------- */

void msx_psg_submit_pcm(int16_t *buf, uint32_t *frames_out)
{
    uint32_t n = 0u;
    if (buf) n = msx_audio_render(buf, MSX_PCM_FRAME);
    if (frames_out) *frames_out = n;
}

/* ---- introspection ------------------------------------------------------ */

uint64_t msx_total_cycles(void) { return (uint64_t)s_cpu.cyc; }
uint64_t msx_body_hash(void)    { return s_body_hash; }
const char *msx_version_string(void) { return MSX_VERSION; }
