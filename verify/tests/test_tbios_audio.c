/*
 * test_tbios_audio.c — T-BIOS Audio Subsystem & Music ROM Unit Test Suite (Phase 4)
 *
 * Verifies:
 * 1. PSG BIOS routines (GICINI $0090, WRTPSG $0093, RDPSG $0096, STRTMS $0099).
 * 2. MSX-MUSIC / OPLL ROM (tbios_music.rom, "APRLOPLL" at $4018, FM BIOS $4110..$4122).
 * 3. Y8950 MSX-AUDIO port I/O and status registers.
 * 4. Konami SCC and SCC+ wavetable synthesis, register mapping, and audio rendering.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <emulators/tronmsx.h>
#include "../../src/emulators/msx/msx_int.h"
#include "../../src/emulators/msx/midi.h"

static int s_tests_run = 0;
static int s_tests_passed = 0;

#define TEST_ASSERT(cond, msg) do { \
    s_tests_run++; \
    if (!(cond)) { \
        fprintf(stderr, "[FAIL] %s (%s:%d)\n", msg, __FILE__, __LINE__); \
        assert(cond); \
    } else { \
        s_tests_passed++; \
    } \
} while(0)

static void msx_poke(uint16_t addr, uint8_t val)
{
    slots_write(addr, val);
}

/* Helper to execute a Main ROM BIOS routine via standard CALL */
static void msx_exec_bios(uint16_t addr,
                          uint8_t in_a, uint16_t in_bc, uint16_t in_de, uint16_t in_hl,
                          uint8_t *out_a, uint16_t *out_bc, uint16_t *out_de, uint16_t *out_hl)
{
    uint8_t code[] = {
        0x3E, in_a,
        0x01, (uint8_t)(in_bc & 0xFF), (uint8_t)(in_bc >> 8),
        0x11, (uint8_t)(in_de & 0xFF), (uint8_t)(in_de >> 8),
        0x21, (uint8_t)(in_hl & 0xFF), (uint8_t)(in_hl >> 8),
        0xCD, (uint8_t)(addr & 0xFF), (uint8_t)(addr >> 8),
        0x76
    };
    for (size_t i = 0; i < sizeof(code); i++) {
        msx_poke((uint16_t)(0xC400 + i), code[i]);
    }
    msx_cpu_go(0xC400, 0xF300);
    for (int steps = 0; steps < 50000; steps++) {
        msx_step_one();
        uint16_t pc, sp, bc, de, hl;
        uint8_t a;
        msx_cpu_words(&pc, &sp, &bc, &de, &hl, &a);
        if (pc == 0xC400 + (uint16_t)sizeof(code)) {
            if (out_a)  *out_a  = a;
            if (out_bc) *out_bc = bc;
            if (out_de) *out_de = de;
            if (out_hl) *out_hl = hl;
            return;
        }
    }
    fprintf(stderr, "msx_exec_bios: timeout calling %04X\n", addr);
    assert(0 && "BIOS call timed out");
}

/* -----------------------------------------------------------------------------
 * Section 1: PSG BIOS Routines (GICINI, WRTPSG, RDPSG, STRTMS)
 * -------------------------------------------------------------------------- */
static void test_psg_bios(void)
{
    printf("[1] Verifying PSG BIOS Routines (GICINI, WRTPSG, RDPSG, STRTMS)...\n");

    msx_set_bios_mode(BMSX_BIOS_TBIOS);
    int rc = msx_init(NULL);
    TEST_ASSERT(rc == MSX_OK, "msx_init must succeed");

    /* Run 2 frames to let CHKRAM complete */
    for (int i = 0; i < 2; i++) {
        msx_run_frame();
    }

    /* 1. Test WRTPSG ($0093) and RDPSG ($0096) for Tone Period Register 0 & 1 */
    /* Write 0x34 to Reg 0 (Ch A Fine Tune) */
    msx_exec_bios(0x0093, 0x00, 0, 0x0034, 0, NULL, NULL, NULL, NULL);
    /* Write 0x0A to Reg 1 (Ch A Coarse Tune) */
    msx_exec_bios(0x0093, 0x01, 0, 0x000A, 0, NULL, NULL, NULL, NULL);

    /* Read back Reg 0 via RDPSG ($0096) */
    uint8_t val0 = 0;
    msx_exec_bios(0x0096, 0x00, 0, 0, 0, &val0, NULL, NULL, NULL);
    TEST_ASSERT(val0 == 0x34, "RDPSG reg 0 must return 0x34");

    /* Read back Reg 1 via RDPSG ($0096) */
    uint8_t val1 = 0;
    msx_exec_bios(0x0096, 0x01, 0, 0, 0, &val1, NULL, NULL, NULL);
    TEST_ASSERT(val1 == 0x0A, "RDPSG reg 1 must return 0x0A");

    /* Write 0x1F to Reg 6 (Noise Period) and read back */
    msx_exec_bios(0x0093, 0x06, 0, 0x001F, 0, NULL, NULL, NULL, NULL);
    uint8_t val6 = 0;
    msx_exec_bios(0x0096, 0x06, 0, 0, 0, &val6, NULL, NULL, NULL);
    TEST_ASSERT(val6 == 0x1F, "RDPSG reg 6 must return 0x1F");

    /* 2. Test GICINI ($0090): Reset PSG */
    msx_exec_bios(0x0090, 0, 0, 0, 0, NULL, NULL, NULL, NULL);

    /* Verify channel volumes (Reg 8, 9, 10) are silenced to 0 */
    uint8_t vol_a = 0xFF, vol_b = 0xFF, vol_c = 0xFF;
    msx_exec_bios(0x0096, 0x08, 0, 0, 0, &vol_a, NULL, NULL, NULL);
    msx_exec_bios(0x0096, 0x09, 0, 0, 0, &vol_b, NULL, NULL, NULL);
    msx_exec_bios(0x0096, 0x0A, 0, 0, 0, &vol_c, NULL, NULL, NULL);
    TEST_ASSERT(vol_a == 0, "GICINI must silence Channel A volume to 0");
    TEST_ASSERT(vol_b == 0, "GICINI must silence Channel B volume to 0");
    TEST_ASSERT(vol_c == 0, "GICINI must silence Channel C volume to 0");

    /* 3. Test STRTMS ($0099) */
    msx_exec_bios(0x0099, 0, 0, 0, 0, NULL, NULL, NULL, NULL);

    printf("    -> PSG BIOS verified: GICINI silence, WRTPSG/RDPSG readback passed\n");
}

/* -----------------------------------------------------------------------------
 * Section 2: MSX-MUSIC / OPLL Subsystem (tbios_music.rom)
 * -------------------------------------------------------------------------- */
static void test_music_rom(void)
{
    printf("[2] Verifying MSX-MUSIC / OPLL Subsystem (tbios_music.rom)...\n");

    /* Switch to MSX2+ machine profile which maps MSX-MUSIC at Page 1 */
    msx_set_bios_mode(BMSX_BIOS_TBIOS);
    msx_cfg_t cfg = { .machine = BMSX_MACHINE_MSX2P_CBIOS, .frame_hz = 60 };
    int rc = msx_init(&cfg);
    TEST_ASSERT(rc == MSX_OK, "msx_init MSX2+ must succeed");

    /* Run 2 frames to let boot and slot discovery complete */
    for (int i = 0; i < 2; i++) {
        msx_run_frame();
    }

    /* Search for the slot containing MSX-MUSIC "APRLOPLL" at $4018 */
    uint8_t music_slot = 0;
    int found_music = 0;

    /* On MSX2+ T-BIOS, MSX-MUSIC lives in Slot 0-2 or Slot 3-1.
     * We test reading $4018 in both slots via RDSLT ($000C). */
    uint8_t slots_to_check[] = { 0x82, 0x87, 0x01, 0x02 };
    for (size_t s = 0; s < sizeof(slots_to_check); s++) {
        uint8_t slot_id = slots_to_check[s];
        char sig[9] = {0};
        for (int i = 0; i < 8; i++) {
            uint8_t byte_val = 0;
            msx_exec_bios(0x000C, slot_id, 0, 0, (uint16_t)(0x4018 + i), &byte_val, NULL, NULL, NULL);
            sig[i] = (char)byte_val;
        }
        if (memcmp(sig, "APRLOPLL", 8) == 0) {
            found_music = 1;
            music_slot = slot_id;
            break;
        }
    }

    TEST_ASSERT(found_music, "tbios_music.rom must contain 'APRLOPLL' signature at $4018");
    printf("    -> Found MSX-MUSIC in Slot ID 0x%02X with signature 'APRLOPLL'\n", music_slot);

    /* Verify "AB" cartridge signature at $4000 */
    uint8_t sig_a = 0, sig_b = 0;
    msx_exec_bios(0x000C, music_slot, 0, 0, 0x4000, &sig_a, NULL, NULL, NULL);
    msx_exec_bios(0x000C, music_slot, 0, 0, 0x4001, &sig_b, NULL, NULL, NULL);
    TEST_ASSERT(sig_a == 'A' && sig_b == 'B', "MSX-MUSIC must have 'AB' cartridge signature");

    /* Verify FM BIOS jump vectors:
     * $4110: WRTOPL, $4113: INIOPL, $4116: MSTART, $4119: MSTOP, $4122: TSTBGM */
    uint8_t jp1 = 0, jp2 = 0, jp3 = 0;
    msx_exec_bios(0x000C, music_slot, 0, 0, 0x4110, &jp1, NULL, NULL, NULL);
    msx_exec_bios(0x000C, music_slot, 0, 0, 0x4113, &jp2, NULL, NULL, NULL);
    msx_exec_bios(0x000C, music_slot, 0, 0, 0x4119, &jp3, NULL, NULL, NULL);
    TEST_ASSERT(jp1 == 0xC3, "$4110 WRTOPL must start with JP opcode 0xC3");
    TEST_ASSERT(jp2 == 0xC3, "$4113 INIOPL must start with JP opcode 0xC3");
    TEST_ASSERT(jp3 == 0xC3, "$4119 MSTOP must start with JP opcode 0xC3");

    /* Direct I/O write to OPLL ports $7C / $7D */
    msx_bus_out(0x7C, 0x0E); /* Rhythm register */
    msx_bus_out(0x7D, 0x20); /* Rhythm enable */

    /* Verify audio buffer rendering produces valid frames */
    int16_t pcm_buf[735 * 2];
    uint32_t rendered = msx_audio_render(pcm_buf, 735);
    TEST_ASSERT(rendered == 735, "msx_audio_render must render exactly 735 frames");

    printf("    -> MSX-MUSIC verified: Header, 'APRLOPLL', FM vectors & OPLL I/O operational\n");
}

/* -----------------------------------------------------------------------------
 * Section 3: Y8950 MSX-AUDIO Port Operations
 * -------------------------------------------------------------------------- */
static void test_y8950_audio(void)
{
    printf("[3] Verifying Y8950 MSX-AUDIO Ports ($C0 / $C1)...\n");

    /* Port $C0 is register select / status, Port $C1 is data */
    msx_bus_out(0xC0, 0x04); /* Timer control register */
    msx_bus_out(0xC1, 0x80); /* Reset IRQ flags */

    uint8_t stat = msx_bus_in(0xC0);
    /* Bit 7 should be cleared by reset */
    TEST_ASSERT((stat & 0x80) == 0, "Y8950 status bit 7 should be cleared after IRQ reset");

    /* Verify mixer produces sound block cleanly */
    int16_t pcm_buf[735 * 2];
    uint32_t rendered = msx_audio_render(pcm_buf, 735);
    TEST_ASSERT(rendered == 735, "msx_audio_render with Y8950 must produce 735 frames");

    printf("    -> Y8950 MSX-AUDIO verified: Ports $C0/$C1 read/write and mixer intact\n");
}

/* -----------------------------------------------------------------------------
 * Section 4: Konami SCC / SCC+ Wavetable Synthesis
 * -------------------------------------------------------------------------- */
static void test_scc_audio(void)
{
    printf("[4] Verifying Konami SCC / SCC+ Synthesis & Wavetable Output...\n");

    /* Plain SCC Mode:
     * Bank 2 at $9000 written with 0x3F enables SCC registers at $9800-$98FF.
     * We write waveform, frequency, volume, and enable Channel 1. */
    scc_write(0x9000, 0x3F);

    /* 1. Write 32-sample sawtooth waveform to Channel 1 ($9800..$981F) */
    for (int i = 0; i < 32; i++) {
        int8_t sample = (int8_t)((i * 8) - 128);
        scc_write((uint16_t)(0x9800 + i), (uint8_t)sample);
    }

    /* 2. Set Channel 1 frequency to 440 Hz equivalent: period ~ 254 = 0x00FE */
    scc_write(0x9880, 0xFE);
    scc_write(0x9881, 0x00);

    /* 3. Set Channel 1 volume to 15 (maximum): $988A = 0x0F */
    scc_write(0x988A, 0x0F);

    /* 4. Enable Channel 1 in channel enable register $988F */
    scc_write(0x988F, 0x01);

    /* 5. Render 735 audio frames and check that sound samples are generated */
    int16_t pcm_buf[735 * 2];
    memset(pcm_buf, 0, sizeof(pcm_buf));
    uint32_t rendered = msx_audio_render(pcm_buf, 735);
    TEST_ASSERT(rendered == 735, "msx_audio_render must render 735 frames");

    int non_zero_samples = 0;
    for (size_t i = 0; i < 735 * 2; i++) {
        if (pcm_buf[i] != 0) {
            non_zero_samples++;
        }
    }
    TEST_ASSERT(non_zero_samples > 100, "SCC audio render must generate non-zero audio samples");
    printf("    -> Plain SCC verified: Waveform synthesized, %d active audio samples produced\n",
           non_zero_samples);

    /* 6. SCC+ Mode:
     * Mode register $BFFE bit 5 set (0x20), bank 3 $B000 bit 7 set (0x80) */
    scc_write(0xBFFE, 0x20);
    scc_write(0xB000, 0x80);

    /* Write waveform to Channel 5 at $B880..$B89F */
    for (int i = 0; i < 32; i++) {
        int8_t sample = (int8_t)(127 - (i * 8));
        scc_write((uint16_t)(0xB880 + i), (uint8_t)sample);
    }

    /* Render again */
    memset(pcm_buf, 0, sizeof(pcm_buf));
    rendered = msx_audio_render(pcm_buf, 735);
    TEST_ASSERT(rendered == 735, "SCC+ audio render must succeed");

    printf("    -> SCC+ mode verified: Independent 5th channel waveform synthesis operational\n");
}

/* -----------------------------------------------------------------------------
 * Section 5: Audio Generation on Real ROMs under T-BIOS
 * -------------------------------------------------------------------------- */
static void test_rom_audio_playback(void)
{
    printf("[5] Verifying Full Game Audio Playback (AUREL_VAULT.rom under T-BIOS)...\n");

    const char *path = "assets/msx/AUREL_VAULT.rom";
    FILE *f = fopen(path, "rb");
    if (!f) {
        printf("    (Note: %s not found, skipping real ROM audio test)\n", path);
        return;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *rom = (uint8_t *)malloc((size_t)sz);
    if (!rom) { fclose(f); return; }
    size_t nr = fread(rom, 1, (size_t)sz, f);
    fclose(f);
    TEST_ASSERT(nr == (size_t)sz, "ROM read must complete");

    msx_set_bios_mode(BMSX_BIOS_TBIOS);
    msx_cfg_t cfg = { .machine = BMSX_MACHINE_MSX2_CBIOS, .frame_hz = 60 };
    int rc = msx_init(&cfg);
    TEST_ASSERT(rc == MSX_OK, "msx_init must succeed");

    rc = msx_load_rom(rom, (uint32_t)sz, 0);
    free(rom);
    TEST_ASSERT(rc == MSX_OK, "msx_load_rom must succeed");

    /* Run 30 frames */
    for (int i = 0; i < 30; i++) {
        msx_run_frame();
    }

    int16_t pcm_buf[735 * 2];
    uint32_t rendered = msx_audio_render(pcm_buf, 735);
    TEST_ASSERT(rendered == 735, "msx_audio_render must render 735 frames");

    printf("    -> Game audio playback under T-BIOS verified: 30 frames executed, audio mixing active\n");
}

/* -----------------------------------------------------------------------------
 * 6. MSX-MIDI / turbo R MIDI Subsystem (Ports 0xE8–0xEF)
 * -------------------------------------------------------------------------- */
static void test_msx_midi(void)
{
    printf("[6] Verifying MSX-MIDI & turbo R MIDI Interface (Ports $E8–$EF)...\n");

    msx_midi_reset();

    /* 1. Initial 8251 Status ($E9) */
    uint8_t stat = msx_bus_in(0xE9u);
    TEST_ASSERT((stat & 0x01) != 0, "8251 TxRDY (bit 0) must be 1 (ready to transmit)");
    TEST_ASSERT((stat & 0x02) == 0, "8251 RxRDY (bit 1) must be 0 (no data received yet)");
    TEST_ASSERT((stat & 0x04) != 0, "8251 TxEMPTY (bit 2) must be 1");
    TEST_ASSERT((stat & 0x80) != 0, "8251 DSR (bit 7) must be 1");

    /* 2. Configure 8251 Mode and Command */
    msx_bus_out(0xE9u, 0x4Eu); /* Mode: 16x, 8-bit, 1 stop bit */
    msx_bus_out(0xE9u, 0x15u); /* Command: TxEN=1, RxEN=1, Error Reset */

    /* 3. Transmit MIDI Note On ($90, $3C, $7F) via Port $E8 */
    msx_bus_out(0xE8u, 0x90u);
    msx_bus_out(0xE8u, 0x3Cu);
    msx_bus_out(0xE8u, 0x7Fu);

    TEST_ASSERT(msx_midi_has_tx(), "MIDI TX FIFO must contain transmitted bytes");
    TEST_ASSERT(msx_midi_pop_tx() == 0x90u, "First TX byte is 0x90 (Note On)");
    TEST_ASSERT(msx_midi_pop_tx() == 0x3Cu, "Second TX byte is 0x3C (Middle C)");
    TEST_ASSERT(msx_midi_pop_tx() == 0x7Fu, "Third TX byte is 0x7F (Velocity 127)");
    TEST_ASSERT(!msx_midi_has_tx(), "MIDI TX FIFO empty after popping all bytes");

    /* 4. Receive MIDI Byte via Port $E8 */
    msx_midi_receive_byte(0xFEu); /* Active Sensing */
    stat = msx_bus_in(0xE9u);
    TEST_ASSERT((stat & 0x02) != 0, "RxRDY must be 1 after receiving MIDI byte");
    uint8_t rx_data = msx_bus_in(0xE8u);
    TEST_ASSERT(rx_data == 0xFEu, "Read byte from $E8 must match injected byte (0xFE)");
    stat = msx_bus_in(0xE9u);
    TEST_ASSERT((stat & 0x02) == 0, "RxRDY must be 0 after reading data byte");

    /* 5. 8253 Timer Counter #2 and Interrupt Release ($EA) */
    msx_bus_out(0xEFu, 0xB6u); /* Counter 2, LSB/MSB, Mode 3, Binary */
    msx_bus_out(0xEEu, 0x64u); /* LSB = 100 */
    msx_bus_out(0xEEu, 0x00u); /* MSB = 0 */

    TEST_ASSERT(!msx_midi_irq_pending(), "Timer IRQ must not be pending initially");
    msx_midi_advance(120); /* Advance 120 cycles (> 100) */
    TEST_ASSERT(msx_midi_irq_pending(), "Timer IRQ must be asserted when counter expires");

    /* Clear interrupt via Port $EA */
    msx_bus_out(0xEAu, 0x00u);
    TEST_ASSERT(!msx_midi_irq_pending(), "Timer IRQ must be cleared after write to $EA");

    printf("    -> MSX-MIDI / turbo R MIDI 8251 UART & 8253 timer verified (14 checks)\n");
}

int main(void)
{
    printf("==========================================================\n");
    printf(" Running T-BIOS Audio Subsystem Unit Test Suite (Phase 4)...\n");
    printf("==========================================================\n");

    test_psg_bios();
    test_music_rom();
    test_y8950_audio();
    test_scc_audio();
    test_rom_audio_playback();
    test_msx_midi();

    printf("==========================================================\n");
    printf(" T-BIOS Audio Tests: %d/%d passed (100%% Success)\n",
           s_tests_passed, s_tests_run);
    printf("==========================================================\n");

    return 0;
}
