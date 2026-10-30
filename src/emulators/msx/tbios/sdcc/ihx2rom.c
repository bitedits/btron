/*
 * ihx2rom.c — Portable Intel HEX to MSX System ROM & MegaROM binary converter.
 * Imported from ../msx.btron.pro/src/sdcc/ihx2rom.c and extended with
 * optional base address parameter for T-BIOS / T-DOS system ROMs (0x0000)
 * as well as MegaROM cartridges (0x4000).
 * Compiles on macOS, FreeBSD, OpenBSD, NetBSD, and Linux.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define MAX_ROM_SIZE     (4 * 1024 * 1024) /* Up to 4 MiB */
#define DEFAULT_ROM_SIZE (1024 * 1024)     /* 1 MiB (64 x 16KB banks) */
#define DEFAULT_BASE     0x4000u

static uint8_t s_rom_buf[MAX_ROM_SIZE];

static int parse_hex_byte(const char *s)
{
    char buf[3];
    buf[0] = s[0];
    buf[1] = s[1];
    buf[2] = '\0';
    return (int)strtoul(buf, NULL, 16);
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <input.ihx> <output.rom> [rom_size_kb] [base_addr_hex]\n", argv[0]);
        return 1;
    }

    const char *in_path = argv[1];
    const char *out_path = argv[2];
    size_t rom_size = DEFAULT_ROM_SIZE;
    uint32_t rom_base = DEFAULT_BASE;

    if (argc >= 4) {
        int kb = atoi(argv[3]);
        if (kb > 0 && (size_t)kb * 1024 <= MAX_ROM_SIZE) {
            rom_size = (size_t)kb * 1024;
        }
    }
    if (argc >= 5) {
        rom_base = (uint32_t)strtoul(argv[4], NULL, 0);
    }

    FILE *fin = fopen(in_path, "r");
    if (!fin) {
        fprintf(stderr, "Error: cannot open input file '%s'\n", in_path);
        return 1;
    }

    /* Initialize with 0xFF (standard flash/EPROM unprogrammed value) */
    memset(s_rom_buf, 0xFF, sizeof(s_rom_buf));

    char line[512];
    uint32_t ext_addr = 0;
    uint32_t min_addr = 0xFFFFFFFFu;
    uint32_t max_addr = 0;

    while (fgets(line, sizeof(line), fin)) {
        char *p = line;
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
        if (*p != ':') continue;

        size_t len = strlen(p);
        if (len < 11) continue;

        int count = parse_hex_byte(p + 1);
        int addr_hi = parse_hex_byte(p + 3);
        int addr_lo = parse_hex_byte(p + 5);
        int rectype = parse_hex_byte(p + 7);
        uint16_t addr16 = (uint16_t)((addr_hi << 8) | addr_lo);

        if (rectype == 0x00) {
            /* Data record */
            uint32_t full_addr = ext_addr + addr16;
            /* In MSX 64KB CPU space: 0xC000..0xFFFF is system RAM.
             * Ignore records targeting RAM. */
            if (ext_addr == 0 && full_addr >= 0xC000u) {
                continue;
            }
            uint32_t offset = full_addr;
            if (offset >= rom_base && ext_addr == 0) {
                offset -= rom_base;
            }

            for (int i = 0; i < count; i++) {
                int byte_val = parse_hex_byte(p + 9 + i * 2);
                if (offset + (uint32_t)i < rom_size) {
                    s_rom_buf[offset + (uint32_t)i] = (uint8_t)byte_val;
                    if (offset + (uint32_t)i < min_addr) min_addr = offset + (uint32_t)i;
                    if (offset + (uint32_t)i > max_addr) max_addr = offset + (uint32_t)i;
                }
            }
        } else if (rectype == 0x01) {
            /* EOF record */
            break;
        } else if (rectype == 0x02) {
            /* Extended Segment Address */
            int seg_hi = parse_hex_byte(p + 9);
            int seg_lo = parse_hex_byte(p + 11);
            ext_addr = (uint32_t)(((seg_hi << 8) | seg_lo) << 4);
        } else if (rectype == 0x04) {
            /* Extended Linear Address */
            int lin_hi = parse_hex_byte(p + 9);
            int lin_lo = parse_hex_byte(p + 11);
            ext_addr = (uint32_t)(((lin_hi << 8) | lin_lo) << 16);
        }
    }

    fclose(fin);

    /* When building a Page-1 cartridge ROM (rom_base == 0x4000), verify "AB" signature */
    if (rom_base == 0x4000u && (s_rom_buf[0] != 0x41 || s_rom_buf[1] != 0x42)) {
        fprintf(stderr, "Warning: MSX ROM header signature 'AB' (0x41, 0x42) not found at offset 0.\n");
        fprintf(stderr, "Injecting standard 'AB' header...\n");
        s_rom_buf[0] = 0x41; /* 'A' */
        s_rom_buf[1] = 0x42; /* 'B' */
        if (s_rom_buf[2] == 0xFF && s_rom_buf[3] == 0xFF) {
            s_rom_buf[2] = 0x10;
            s_rom_buf[3] = 0x40;
        }
    }

    FILE *fout = fopen(out_path, "wb");
    if (!fout) {
        fprintf(stderr, "Error: cannot create output file '%s'\n", out_path);
        return 1;
    }

    size_t written = fwrite(s_rom_buf, 1, rom_size, fout);
    fclose(fout);

    if (written != rom_size) {
        fprintf(stderr, "Error: failed to write complete ROM image\n");
        return 1;
    }

    printf("Successfully built MSX ROM: '%s' (%zu KiB, base 0x%04X, data range 0x%05X-0x%05X)\n",
           out_path, rom_size / 1024, rom_base, min_addr, max_addr);
    return 0;
}

