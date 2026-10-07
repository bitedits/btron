/*
 * tronMSX — ZEXALL, the Z80 instruction exerciser (Frank D. Cringle).
 * See https://ru.wikipedia.org/wiki/ZEXALL and the .src beside the .cim.
 *
 * This is a host test for the *vendored* core in src/emulators/msx/z80.c, not
 * the pristine upstream copy: the two have diverged, so the emulator's own CPU
 * is what gets proven here.  ZEXALL runs every documented Z80 instruction over
 * a large operand set, accumulates a CRC per instruction group and prints
 * "...OK" or "...ERROR **** crc expected:.. found:..".  Correctness is the CRC
 * verdict in the text, so that is what this harness checks; the cycle count is
 * reported for information and compared against the z80emu reference.
 *
 * The program is a CP/M .cim: it is loaded at 0x0100 and talks to the OS
 * through the BDOS call gate at 0x0005 and the warm-boot vector at 0x0000.
 * Both are synthesised the way the upstream superzazu harness does:
 *   0x0000: OUT (0),A   -> ends the run          (WBOOT)
 *   0x0005: IN A,(0) / RET -> BDOS, function in C (2=putchar E, 9=puts DE)
 */

#include <stdio.h>
#include <string.h>

#include "z80.h"

#define ZEX_MEM_SIZE  0x10000
#define ZEX_LOAD_ADDR 0x0100
#define ZEX_CYC_REF   46734978649UL   /* z80emu reference for zexall.cim */

/* Output is captured rather than streamed so the verdict can be scanned for
 * CRC errors; ZEXALL emits only a few KB. */
#define ZEX_LOG_CAP   (64u * 1024u)

static uint8_t s_mem[ZEX_MEM_SIZE];
static char    s_log[ZEX_LOG_CAP];
static size_t  s_log_len;
static int     s_finished;

static uint8_t rb(void *ud, uint16_t addr) { (void)ud; return s_mem[addr]; }
static void    wb(void *ud, uint16_t addr, uint8_t v) { (void)ud; s_mem[addr] = v; }

static void log_char(char c)
{
    if (s_log_len + 1u < ZEX_LOG_CAP) s_log[s_log_len++] = c;
}

static uint8_t port_in(z80 *z, uint8_t port)
{
    uint8_t fn;
    (void)port;
    fn = z->c;

    if (fn == 2u) {                       /* C_WRITE: char in E */
        putchar(z->e);
        log_char((char)z->e);
    } else if (fn == 9u) {                /* C_WRITESTR: bytes at DE until '$' */
        uint16_t a = (uint16_t)((z->d << 8) | z->e);
        while (s_mem[a] != '$') {
            putchar(s_mem[a]);
            log_char((char)s_mem[a]);
            a++;
        }
    }
    return 0xFFu;
}

static void port_out(z80 *z, uint8_t port, uint8_t val)
{
    (void)z; (void)port; (void)val;
    s_finished = 1;                       /* WBOOT at 0x0000 ends the run */
}

static int load_cim(const char *path)
{
    char full[512];
    FILE *f = NULL;
    size_t n;
    int i;

    for (i = 0; i < 2 && !f; i++) {
        const char *pre = i ? "./" : "";
        snprintf(full, sizeof(full), "%s%s", pre, path);
        f = fopen(full, "rb");
    }
    if (!f) { fprintf(stderr, "zexall: cannot open %s\n", path); return 0; }

    n = fread(&s_mem[ZEX_LOAD_ADDR], 1, ZEX_MEM_SIZE - ZEX_LOAD_ADDR, f);
    fclose(f);
    if (n == 0) { fprintf(stderr, "zexall: %s is empty\n", path); return 0; }
    return 1;
}

int main(void)
{
    static const char *cim = "third_party/superzazu-z80/roms/zexall.cim";
    z80 cpu;
    unsigned long instr = 0;
    int crc_errors;

    memset(s_mem, 0, sizeof(s_mem));
    if (!load_cim(cim)) return 2;

    z80_init(&cpu);
    cpu.read_byte  = rb;
    cpu.write_byte = wb;
    cpu.port_in    = port_in;
    cpu.port_out   = port_out;
    cpu.userdata   = NULL;

    s_mem[0x0000] = 0xD3; s_mem[0x0001] = 0x00;   /* OUT (0),A  : WBOOT */
    s_mem[0x0005] = 0xDB; s_mem[0x0006] = 0x00;   /* IN A,(0)   : BDOS  */
    s_mem[0x0007] = 0xC9;                         /* RET               */
    cpu.pc = ZEX_LOAD_ADDR;

    s_finished = 0;
    s_log_len = 0;
    while (!s_finished) { instr++; z80_step(&cpu); }
    s_log[s_log_len] = '\0';

    crc_errors = (strstr(s_log, "ERROR") != NULL);

    printf("\n==== ZEXALL (vendored src/emulators/msx/z80.c) ====\n");
    printf("instructions : %lu\n", instr);
    printf("cycles       : %lu (reference %lu, diff %ld)\n",
           cpu.cyc, ZEX_CYC_REF, (long)cpu.cyc - (long)ZEX_CYC_REF);
    if (crc_errors) {
        printf("RESULT: FAIL (CRC error in output above)\n");
        return 1;
    }
    printf("RESULT: PASS (all instruction-group CRCs OK)\n");
    return 0;
}
