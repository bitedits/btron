/*
 * Freestanding ARM Bare-Metal Startup, PL011 UART & BCM283x Framebuffer Video Driver
 * Sakamura T-Kernel 2.0 Real-Time Engine Integration
 * Supports BCM2836 (Raspberry Pi 2B, Cortex-A7 / ARMv7) & BCM2711 (Pi 4B, AArch64)
 */

#include <stdint.h>
#include <stddef.h>
#include <btron/dp.h>
#include <btron/wnd.h>
#include <btron/desktop.h>
#include <btron/troncode.h>
#include <btron/vobj.h>

#if (TYPE_RPI == 1)
#define PL011_BASE      0x20201000u
#define MBOX_BASE_ADDR  0x2000b880u
#elif (TYPE_RPI == 2 || TYPE_RPI == 3)
#define PL011_BASE      0x3f201000u
#define MBOX_BASE_ADDR  0x3f00b880u
#elif (TYPE_RPI == 4)
#define PL011_BASE      0xfe201000u
#define MBOX_BASE_ADDR  0xfe00b880u
#else
#define PL011_BASE      0x3f201000u
#define MBOX_BASE_ADDR  0x3f00b880u
#endif

#define MBOX_READ       0x00
#define MBOX_STATUS     0x18
#define MBOX_WRITE      0x20
#define MBOX_FULL       0x80000000
#define MBOX_EMPTY      0x40000000
#define MBOX_CH_PROP    8

/* PL011 UART register offsets (in 32-bit words) */
#define PL011_DR      0
#define PL011_FR      6   /* 0x18/4 */
#define PL011_IBRD    9   /* 0x24/4 */
#define PL011_FBRD    10  /* 0x28/4 */
#define PL011_LCRH    11  /* 0x2C/4 */
#define PL011_CR      12  /* 0x30/4 */
#define PL011_IMSC    14  /* 0x38/4 */
#define PL011_FR_TXFF (1u << 5)
#define PL011_FR_BUSY (1u << 3)
uintptr_t g_mmio_base = 0x3f000000UL;

#define pl011 ((volatile uint32_t*)(g_mmio_base + 0x00201000UL))

void uart_init(void) {
#if defined(__aarch64__)
    uint64_t midr;
    __asm__ volatile("mrs %0, midr_el1" : "=r"(midr));
    uint32_t part = (midr >> 4) & 0xFFF;
    if (part == 0xD08) {
        /* Cortex-A72: Raspberry Pi 4B (BCM2711) */
        g_mmio_base = 0xfe000000UL;
    } else {
        /* Cortex-A53 / Cortex-A7: Raspberry Pi 3B / 2B */
        g_mmio_base = 0x3f000000UL;
    }
#endif
    /* Disable UART */
    pl011[PL011_CR] = 0;
    /* Wait for UART to finish transmitting with timeout */
    for (volatile int to = 0; to < 10000 && (pl011[PL011_FR] & PL011_FR_BUSY); to++) {
        __asm__ volatile("nop");
    }
    /* Set baud rate: 3MHz UART clock / (16 * 115200) = 1.627 → IBRD=1, FBRD=40 */
    pl011[PL011_IBRD] = 1;
    pl011[PL011_FBRD] = 40;
    /* 8N1, FIFO enable */
    pl011[PL011_LCRH] = (3u << 5) | (1u << 4); /* WLEN=8, FEN=1 */
    /* Mask all interrupts */
    pl011[PL011_IMSC] = 0x7FF;
    /* Enable UART: UARTEN | TXE | RXE */
    pl011[PL011_CR] = (1u << 0) | (1u << 8) | (1u << 9);
}

#define PL011_FR_RXFE (1u << 4) /* Receive FIFO empty */

void uart_putc(char c) {
    int to = 50000;
    while ((pl011[PL011_FR] & PL011_FR_TXFF) && --to > 0) {
        __asm__ volatile("nop");
    }
    if (to > 0) {
        pl011[PL011_DR] = (uint32_t)(unsigned char)c;
    }
}

int uart_has_char(void) {
    return (pl011[PL011_FR] & PL011_FR_RXFE) == 0;
}

int uart_getc(void) {
    while (pl011[PL011_FR] & PL011_FR_RXFE) {}
    return (int)(pl011[PL011_DR] & 0xFF);
}

void uart_puts(const char *s) {
    if (!s) return;
    while (*s) {
        if (*s == '\n') uart_putc('\r');
        uart_putc(*s++);
    }
}

static void uart_hex8(uint8_t v) {
    const char *h = "0123456789ABCDEF";
    uart_putc(h[(v >> 4) & 0xF]);
    uart_putc(h[v & 0xF]);
}

void uart_hex32(uint32_t v) {
    uart_putc('0'); uart_putc('x');
    uart_hex8((v >> 24) & 0xFF);
    uart_hex8((v >> 16) & 0xFF);
    uart_hex8((v >> 8)  & 0xFF);
    uart_hex8( v        & 0xFF);
}

/*
 * Bare-metal heap: use a fixed high address (16MB) so it stays clear of:
 *   - kernel text/data/BSS at 0x80000..~0x98000
 *   - GPU framebuffer at ~0x300000 (3MB)
 * 16MB gives us 1GB - 16MB = ~1008MB of headroom on the far side.
 */
#define HEAP_BASE ((uintptr_t)0x02000000)  /* 32 MB — clear of all text, data, and BSS */
#define HEAP_LIMIT ((uintptr_t)0x38000000) /* 896 MB — safely below VideoCore GPU FB (~961MB) */
uintptr_t heap_ptr = 0; /* initialized in btron_main before first use */
#include <libstr.h>

void* memcpy(void *dst, const void *src, size_t n) { return tkl_memcpy(dst, src, n); }
void* memset(void *s, int c, size_t n) { return tkl_memset(s, c, n); }
void* memmove(void *dest, const void *src, size_t n) { return tkl_memmove(dest, src, n); }

void __aeabi_memset(void *dest, size_t n, int c) { tkl_memset(dest, c, n); }
void __aeabi_memset4(void *dest, size_t n, int c) { tkl_memset(dest, c, n); }
void __aeabi_memset8(void *dest, size_t n, int c) { tkl_memset(dest, c, n); }
void __aeabi_memclr(void *dest, size_t n) { tkl_memset(dest, 0, n); }
void __aeabi_memclr4(void *dest, size_t n) { tkl_memset(dest, 0, n); }
void __aeabi_memclr8(void *dest, size_t n) { tkl_memset(dest, 0, n); }
void __aeabi_memcpy(void *dest, const void *src, size_t n) { tkl_memcpy(dest, src, n); }
void __aeabi_memcpy4(void *dest, const void *src, size_t n) { tkl_memcpy(dest, src, n); }
void __aeabi_memcpy8(void *dest, const void *src, size_t n) { tkl_memcpy(dest, src, n); }

void* Imalloc(size_t sz) {
    if (heap_ptr == 0) heap_ptr = HEAP_BASE;
    uintptr_t aligned = (heap_ptr + 15) & ~(uintptr_t)15;
    if (aligned + sz > HEAP_LIMIT) return NULL;
    heap_ptr = aligned + sz;
    return (void*)aligned;
}

void Ifree(void *ptr) { (void)ptr; }

void* Icalloc(size_t nmemb, size_t sz) {
    size_t total = nmemb * sz;
    void *ptr = Imalloc(total);
    if (ptr) tkl_memset(ptr, 0, total);
    return ptr;
}

void* malloc(size_t sz) { return Imalloc(sz); }
void* calloc(size_t nmemb, size_t sz) { return Icalloc(nmemb, sz); }
void free(void *ptr) { Ifree(ptr); }

void* IAmalloc(size_t sz, unsigned int attr) { (void)attr; return Imalloc(sz); }
void IAfree(void *ptr, unsigned int attr) { (void)attr; (void)ptr; }


/* Bit manipulation primitives for T-Kernel scheduler */
void BitSet(uint32_t *base, int offset) {
    int idx = offset / 32;
    int bit = offset % 32;
    base[idx] |= (1U << bit);
}

void BitClr(uint32_t *base, int offset) {
    int idx = offset / 32;
    int bit = offset % 32;
    base[idx] &= ~(1U << bit);
}

int BitTest(const uint32_t *base, int offset) {
    int idx = offset / 32;
    int bit = offset % 32;
    return (base[idx] & (1U << bit)) ? 1 : 0;
}

int BitSearch0_w(const uint32_t *base, int offset, int width) {
    for (int i = 0; i < width; i++) {
        int pos = offset + i;
        int idx = pos / 32;
        int bit = pos % 32;
        if (!(base[idx] & (1U << bit))) return i;
    }
    return -1;
}

int BitSearch1_w(const uint32_t *base, int offset, int width) {
    for (int i = 0; i < width; i++) {
        int pos = offset + i;
        int idx = pos / 32;
        int bit = pos % 32;
        if (base[idx] & (1U << bit)) return i;
    }
    return -1;
}

uint32_t disint(void) { return 0; }
uint32_t enaint(uint32_t intsts) { return intsts; }
void DisableInt(uint32_t vec) { (void)vec; }
void EnableInt(int vec) { (void)vec; }
void SetIntMode(uint32_t vec, uint32_t mode) { (void)vec; (void)mode; }
void ClearInt(uint32_t vec) { (void)vec; }
int CheckInt(int vec) { (void)vec; return 0; }

void tm_monitor(void) {}
void tm_putstring(const char *s) { uart_puts(s); }
void tm_exit(int code) { (void)code; while(1); }
void tm_command(const char *cmd) { (void)cmd; }

int _tk_get_cfn(uint8_t *name, int *val, int max) {
    (void)name;
    if (val && max > 0) val[0] = 0;
    return 1;
}

int __tk_get_cfn(uint8_t *name, int *val, int max) {
    return _tk_get_cfn(name, val, max);
}

int GetDevConf(const uint8_t *name, int *val) { (void)name; if (val) val[0] = 0; return 0; }
int GetSysConf(const uint8_t *name, int *val) { (void)name; if (val) val[0] = 0; return 0; }

int SDefDevice(const void *ddev, void *idev, void **sdi) {
    (void)ddev; (void)idev;
    if (sdi) *sdi = (void*)1;
    return 0;
}

int ScreenDrv(int ac, unsigned char *av[]) {
    (void)ac; (void)av;
    return 0;
}

int KbPdDrv(int ac, unsigned char *av[]) {
    (void)ac; (void)av;
    return 0;
}

int LowKbPdDrv(int ac, unsigned char *av[]) {
    (void)ac; (void)av;
    return 0;
}

int MapMemory(const void *paddr, int len, unsigned int attr, void **laddr) {
    (void)len; (void)attr;
    if (laddr) *laddr = (void*)paddr;
    return 0;
}

int CnvPhysicalAddr(const void *vaddr, int len, void **paddr) {
    if (paddr) *paddr = (void*)vaddr;
    return len;
}

int tk_get_smb(void **addr, int nblk, unsigned int attr) {
    (void)attr;
    size_t sz = (size_t)nblk * 4096;
    void *ptr = Imalloc(sz);
    if (!ptr) return -5; /* E_NOMEM */
    if (addr) *addr = ptr;
    return 0;
}

int tk_ref_smb(void *pk_rsmb) {
    (void)pk_rsmb;
    return 0;
}

void *lowmem_top = (void*)0x200000;
void call_entry(void) {}
void dispatch_entry(void) {}
void dispatch_to_schedtsk(void) {}
void rettex_entry(void) {}
void _tk_ret_int(void) {}
void call_dbgspt(void) {}
void timer_handler_startup(void) {}
void defaulthdr_startup(void) {}
void exchdr_startup(void) {}
void inthdr_startup(void) {}
int no_support(void) { return -70; /* E_NOSPT */ }

void *hook_dsp = NULL;
void *unhook_dsp = NULL;
void *hook_int = NULL;
void *unhook_int = NULL;
void *hook_svc = NULL;
void *unhook_svc = NULL;

/* 64-bit integer division runtime helpers for 32-bit ARM (EABI) */
#if !defined(__aarch64__)
__attribute__((naked))
void __aeabi_uldivmod(void) {
    __asm__ volatile(
        "push {r4, r5, r6, r7, lr}\n\t"
        "mov r4, #0\n\t"
        "mov r5, #0\n\t"
        "mov r6, #0\n\t"
        "mov r7, #0\n\t"
        "mov ip, #64\n\t"
        "1:\n\t"
        "lsls r6, r6, #1\n\t"
        "adc r7, r7, r7\n\t"
        "tst r1, #0x80000000\n\t"
        "orrne r6, r6, #1\n\t"
        "lsls r0, r0, #1\n\t"
        "adc r1, r1, r1\n\t"
        "cmp r7, r3\n\t"
        "cmpeq r6, r2\n\t"
        "blo 2f\n\t"
        "subs r6, r6, r2\n\t"
        "sbc r7, r7, r3\n\t"
        "orr r4, r4, #1\n\t"
        "2:\n\t"
        "subs ip, ip, #1\n\t"
        "beq 3f\n\t"
        "lsls r4, r4, #1\n\t"
        "adc r5, r5, r5\n\t"
        "b 1b\n\t"
        "3:\n\t"
        "mov r0, r4\n\t"
        "mov r1, r5\n\t"
        "mov r2, r6\n\t"
        "mov r3, r7\n\t"
        "pop {r4, r5, r6, r7, pc}\n\t"
    );
}

__attribute__((naked))
void __aeabi_ldivmod(void) {
    __asm__ volatile("b __aeabi_uldivmod\n\t");
}

__attribute__((naked))
uint32_t __aeabi_uidiv(uint32_t num, uint32_t den) {
    __asm__ volatile(
        "cmp r1, #0\n\t"
        "beq 1f\n\t"
        "udiv r0, r0, r1\n\t"
        "bx lr\n\t"
        "1:\n\t"
        "mov r0, #0\n\t"
        "bx lr\n\t"
    );
}

__attribute__((naked))
int32_t __aeabi_idiv(int32_t num, int32_t den) {
    __asm__ volatile(
        "cmp r1, #0\n\t"
        "beq 1f\n\t"
        "sdiv r0, r0, r1\n\t"
        "bx lr\n\t"
        "1:\n\t"
        "mov r0, #0\n\t"
        "bx lr\n\t"
    );
}

__attribute__((naked))
void __aeabi_uidivmod(void) {
    __asm__ volatile(
        "push {lr}\n\t"
        "mov r2, r0\n\t"
        "mov r3, r1\n\t"
        "bl __aeabi_uidiv\n\t"
        "mul r3, r0, r3\n\t"
        "sub r1, r2, r3\n\t"
        "pop {pc}\n\t"
    );
}

__attribute__((naked))
void __aeabi_idivmod(void) {
    __asm__ volatile(
        "push {lr}\n\t"
        "mov r2, r0\n\t"
        "mov r3, r1\n\t"
        "bl __aeabi_idiv\n\t"
        "mul r3, r0, r3\n\t"
        "sub r1, r2, r3\n\t"
        "pop {pc}\n\t"
    );
}
#endif

/* Mailbox message buffer aligned to 16 bytes */
static volatile uint32_t mbox[36] __attribute__((aligned(16)));
uint32_t *g_pi_fb_ptr = NULL;

uint32_t* init_pi_framebuffer(uint32_t w, uint32_t h) {
    uintptr_t mbox_base = g_mmio_base + 0x0000b880UL;
    volatile uint32_t *status_reg = (volatile uint32_t*)(mbox_base + MBOX_STATUS);
    volatile uint32_t *write_reg  = (volatile uint32_t*)(mbox_base + MBOX_WRITE);
    volatile uint32_t *read_reg   = (volatile uint32_t*)(mbox_base + MBOX_READ);

    mbox[0] = 35 * 4;
    mbox[1] = 0;

    mbox[2] = 0x00048003;  /* set phy wh */
    mbox[3] = 8;
    mbox[4] = 0;          /* request code */
    mbox[5] = w;
    mbox[6] = h;

    mbox[7] = 0x00048004;  /* set virt wh */
    mbox[8] = 8;
    mbox[9] = 0;          /* request code */
    mbox[10] = w;
    /* A single scanout page is deliberately used.  Full-frame page flipping
     * via the mailbox can stall for hundreds of milliseconds on the Pi 400. */
    mbox[11] = h;            /* Physical height (1024x768) */

    mbox[12] = 0x00048005; /* set depth */
    mbox[13] = 4;
    mbox[14] = 0;          /* request code */
    mbox[15] = 32;

    mbox[16] = 0x00048006; /* set pixel order */
    mbox[17] = 4;
    mbox[18] = 0;          /* request code */
    mbox[19] = 0;          /* 0: BGR (matches ARGB 0xAARRGGBB in little-endian RAM) */

    mbox[20] = 0x00048009; /* set virt offset */
    mbox[21] = 8;
    mbox[22] = 0;          /* request code */
    mbox[23] = 0;
    mbox[24] = 0;

    mbox[25] = 0x00040001; /* allocate framebuffer */
    mbox[26] = 8;
    mbox[27] = 0;          /* request code */
    mbox[28] = 4096;       /* alignment */
    mbox[29] = 0;          /* response: size in bytes */

    mbox[30] = 0x00040008; /* get pitch */
    mbox[31] = 4;
    mbox[32] = 0;          /* request code */
    mbox[33] = 0;

    mbox[34] = 0;          /* end tag */

    uint32_t mbox_addr = (uint32_t)(uintptr_t)mbox;

    __asm__ volatile("dsb sy" : : : "memory");

    /* Send mailbox message to Channel 8 with timeout protection */
    int to = 2000000;
    while ((*status_reg & MBOX_FULL) && --to > 0) {
        __asm__ volatile("nop");
    }
    *write_reg = ((mbox_addr & 0xFFFFFFF0) | MBOX_CH_PROP);

    /* Read mailbox response from Channel 8 */
    to = 2000000;
    while (--to > 0) {
        while ((*status_reg & MBOX_EMPTY) && --to > 0) {
            __asm__ volatile("nop");
        }
        if (to <= 0) break;
        uint32_t res = *read_reg;
        if ((res & 0xF) == MBOX_CH_PROP) {
            break;
        }
    }

    __asm__ volatile("dsb sy" : : : "memory");

    if (mbox[1] == 0x80000000 && mbox[28] != 0) {
        uint32_t fb_phys = mbox[28] & 0x3FFFFFFF;
        uint32_t fb_sz   = mbox[29];
        uart_puts("[QEMU-ARM] Framebuffer Allocated by VideoCore GPU!\n");
        uart_puts("[QEMU-ARM] FB Address: ");
        uart_hex32(fb_phys);
        uart_puts(" Size: ");
        uart_hex32(fb_sz);
        uart_puts("\n");
        g_pi_fb_ptr = (uint32_t*)(uintptr_t)fb_phys;
        return g_pi_fb_ptr;
    }

    uart_puts("[QEMU-ARM] Framebuffer allocation fallback.\n");
    g_pi_fb_ptr = (uint32_t*)0x3c000000;
    return g_pi_fb_ptr;
}

int mailbox_set_virtual_offset(uint32_t x, uint32_t y) {
    uintptr_t mbox_base = g_mmio_base + 0x0000b880UL;
    volatile uint32_t *status_reg = (volatile uint32_t*)(mbox_base + MBOX_STATUS);
    volatile uint32_t *write_reg  = (volatile uint32_t*)(mbox_base + MBOX_WRITE);
    volatile uint32_t *read_reg   = (volatile uint32_t*)(mbox_base + MBOX_READ);

    /* Coherent non-cacheable DMA mailbox buffer */
    volatile uint32_t *mbox_buf = (volatile uint32_t *)(0x01000000UL + 0xF600UL);
    mbox_buf[0] = 8 * 4;       /* buffer size */
    mbox_buf[1] = 0;           /* request code */
    mbox_buf[2] = 0x00048009;  /* tag: SET_VIRTUAL_OFFSET */
    mbox_buf[3] = 8;           /* value buffer size */
    mbox_buf[4] = 0;           /* request/response size */
    mbox_buf[5] = x;           /* x offset */
    mbox_buf[6] = y;           /* y offset */
    mbox_buf[7] = 0;           /* end tag */

    uint32_t mbox_addr = 0x01000000U + 0xF600U;
    __asm__ volatile("dsb sy" : : : "memory");

    int to = 1000;
    while ((*status_reg & MBOX_FULL) && --to > 0) {
        __asm__ volatile("nop");
    }
    if (to <= 0) return -1;
    *write_reg = ((mbox_addr & 0xFFFFFFF0) | MBOX_CH_PROP);

    to = 1000;
    while (--to > 0) {
        while ((*status_reg & MBOX_EMPTY) && --to > 0) {
            __asm__ volatile("nop");
        }
        if (to <= 0) break;
        uint32_t res = *read_reg;
        if ((res & 0xF) == MBOX_CH_PROP) break;
    }
    __asm__ volatile("dsb sy" : : : "memory");
    return 0;
}

int bcm283x_power_usb(void) {
    uintptr_t mbox_base = g_mmio_base + 0x0000b880UL;
    volatile uint32_t *status_reg = (volatile uint32_t*)(mbox_base + MBOX_STATUS);
    volatile uint32_t *write_reg  = (volatile uint32_t*)(mbox_base + MBOX_WRITE);
    volatile uint32_t *read_reg   = (volatile uint32_t*)(mbox_base + MBOX_READ);

    mbox[0] = 8 * 4;       /* buffer size in bytes */
    mbox[1] = 0;           /* request code */
    mbox[2] = 0x00028001;  /* tag: SET_POWER_STATE */
    mbox[3] = 8;           /* value buffer size */
    mbox[4] = 8;           /* req/resp size */
    mbox[5] = 3;           /* device id: 3 = USB_HCD */
    mbox[6] = 3;           /* state: bit 0 = ON, bit 1 = WAIT */
    mbox[7] = 0;           /* end tag */

    uint32_t mbox_addr = (uint32_t)(uintptr_t)mbox;
    __asm__ volatile("dsb sy" : : : "memory");

    int to = 2000;
    while ((*status_reg & MBOX_FULL) && --to > 0) {
        __asm__ volatile("nop");
    }
    *write_reg = ((mbox_addr & 0xFFFFFFF0) | MBOX_CH_PROP);

    to = 2000;
    while (--to > 0) {
        while ((*status_reg & MBOX_EMPTY) && --to > 0) {
            __asm__ volatile("nop");
        }
        if (to <= 0) break;
        uint32_t res = *read_reg;
        if ((res & 0xF) == MBOX_CH_PROP) break;
    }
    __asm__ volatile("dsb sy" : : : "memory");
    return (mbox[1] == 0x80000000) ? 0 : -1;
}

uint32_t bcm283x_get_board_revision(void) {
    uintptr_t mbox_base = g_mmio_base + 0x0000b880UL;
    volatile uint32_t *status_reg = (volatile uint32_t*)(mbox_base + MBOX_STATUS);
    volatile uint32_t *write_reg  = (volatile uint32_t*)(mbox_base + MBOX_WRITE);
    volatile uint32_t *read_reg   = (volatile uint32_t*)(mbox_base + MBOX_READ);

    mbox[0] = 7 * 4;       /* buffer size in bytes */
    mbox[1] = 0;           /* request code */
    mbox[2] = 0x00010002;  /* tag: GET_BOARD_REVISION */
    mbox[3] = 4;           /* value buffer size */
    mbox[4] = 0;           /* req size */
    mbox[5] = 0;           /* output value */
    mbox[6] = 0;           /* end tag */

    uint32_t mbox_addr = (uint32_t)(uintptr_t)mbox;
    __asm__ volatile("dsb sy" : : : "memory");

    int to = 1000;
    while ((*status_reg & MBOX_FULL) && --to > 0) {
        __asm__ volatile("nop");
    }
    *write_reg = ((mbox_addr & 0xFFFFFFF0) | MBOX_CH_PROP);

    to = 1000;
    while (--to > 0) {
        while ((*status_reg & MBOX_EMPTY) && --to > 0) {
            __asm__ volatile("nop");
        }
        if (to <= 0) break;
        uint32_t res = *read_reg;
        if ((res & 0xF) == MBOX_CH_PROP) break;
    }
    __asm__ volatile("dsb sy" : : : "memory");
    return mbox[5];
}

#define ARGB(a,r,g,b) (((uint32_t)(a)<<24)|((uint32_t)(r)<<16)|((uint32_t)(g)<<8)|(uint32_t)(b))

#if defined(__aarch64__)
__attribute__((section(".text"), aligned(2048)))
void arm64_vector_table(void) {
    __asm__ volatile(
        /* Current EL with SP0 */
        ".balign 128\n\teret\n\t"
        ".balign 128\n\teret\n\t"
        ".balign 128\n\teret\n\t"
        ".balign 128\n\teret\n\t"

        /* Current EL with SPx: skip faulting instruction safely */
        ".balign 128\n\t"
        "mrs x18, CurrentEL\n\t"
        "lsr x18, x18, #2\n\t"
        "cmp x18, #2\n\t"
        "b.ne 91f\n\t"
        "mrs x18, elr_el2\n\t"
        "add x18, x18, #4\n\t"
        "msr elr_el2, x18\n\t"
        "eret\n\t"
        "91:\n\t"
        "mrs x18, elr_el1\n\t"
        "add x18, x18, #4\n\t"
        "msr elr_el1, x18\n\t"
        "eret\n\t"
        ".balign 128\n\teret\n\t"
        ".balign 128\n\teret\n\t"
        ".balign 128\n\teret\n\t"

        /* Lower EL using AArch64 */
        ".balign 128\n\teret\n\t"
        ".balign 128\n\teret\n\t"
        ".balign 128\n\teret\n\t"
        ".balign 128\n\teret\n\t"

        /* Lower EL using AArch32 */
        ".balign 128\n\teret\n\t"
        ".balign 128\n\teret\n\t"
        ".balign 128\n\teret\n\t"
        ".balign 128\n\teret\n\t"
    );
}

/* ─────────────────────────────────────────────────────────────────
 * AArch64 MMU & L1/L2 Cache Identity Mapping
 *
 * Configures:
 *   - Normal Inner/Outer Write-Back Cacheable RAM (0 to 3GB) -> Full CPU speed!
 *   - Device-nGnRE for Framebuffer & MMIO (3GB to 4GB, 0xFD500000 PCIe, 0xFE000000 MMIO)
 *   - Device-nGnRE for Outbound PCIe Window (24GB = 0x600000000ULL)
 * ───────────────────────────────────────────────────────────────── */
static uint64_t s_arm64_l1[512] __attribute__((aligned(4096)));
static uint64_t s_arm64_l2[512] __attribute__((aligned(4096)));

void arm64_mmu_init(void) {
    /* Attr 0 = Device-nGnRE (0x04)
     * Attr 1 = Normal Cacheable Inner/Outer Write-Back (0xFF)
     * Attr 2 = Normal Non-Cacheable (0x44) for hardware DMA rings/buffers
     */
    uint64_t mair = (0x44ULL << 16) | (0xFFULL << 8) | (0x04ULL << 0);

    /* L2 Table: 512 entries of 2MB (covers 0 to 1GB)
     * Entries 0..7: 0 - 16MB -> Normal Cacheable RAM (kernel code, data, BSS, desktop backbuffer)
     * Entry 8: 16MB - 18MB (0x01000000 - 0x011FFFFF) -> Normal Non-Cacheable (Coherent DMA memory)
     * Entries 9..479: 18MB - 960MB -> Normal Cacheable RAM
     * Entries 480..511: 960MB - 1GB (0x3C000000 - 0x3FFFFFFF) -> GPU Framebuffer (Normal Non-Cacheable, Write-Combining)
     */
    for (uint64_t i = 0; i < 480; i++) {
        if (i == 8) {
            s_arm64_l2[i] = (i * 0x200000ULL) | (1ULL << 10) | (3ULL << 8) | (2ULL << 2) | 0x01ULL; /* Non-cacheable DMA (16MB-18MB) */
        } else {
            s_arm64_l2[i] = (i * 0x200000ULL) | (1ULL << 10) | (3ULL << 8) | (1ULL << 2) | 0x01ULL; /* Normal Cacheable */
        }
    }
    for (uint64_t i = 480; i < 512; i++) {
        s_arm64_l2[i] = (i * 0x200000ULL) | (1ULL << 10) | (3ULL << 8) | (2ULL << 2) | 0x01ULL; /* GPU FB: Normal Non-Cacheable (Write-Combining) */
    }

    /* L1 Table: 512 entries of 1GB */
    for (int i = 0; i < 512; i++) {
        s_arm64_l1[i] = 0;
    }

    /* L1[0]: Point to L2 table (bits [1:0] = 11 for Table) */
    s_arm64_l1[0] = ((uint64_t)(uintptr_t)s_arm64_l2) | 0x03ULL;

    /* L1[1]: 1GB to 2GB RAM -> Normal Cacheable */
    s_arm64_l1[1] = 0x40000000ULL | (1ULL << 10) | (3ULL << 8) | (1ULL << 2) | 0x01ULL;

    /* L1[2]: 2GB to 3GB RAM -> Normal Cacheable */
    s_arm64_l1[2] = 0x80000000ULL | (1ULL << 10) | (3ULL << 8) | (1ULL << 2) | 0x01ULL;

    /* L1[3]: 3GB to 4GB (0xC0000000 - 0xFFFFFFFF) -> Device memory (MMIO + PCIe RC) */
    s_arm64_l1[3] = 0xC0000000ULL | (1ULL << 10) | (2ULL << 8) | (0ULL << 2) | 0x01ULL;

    /* L1[24]: 24GB (0x600000000ULL, 1GB window) -> Device memory (PCIe Outbound to VL805) */
    s_arm64_l1[24] = 0x600000000ULL | (1ULL << 10) | (2ULL << 8) | (0ULL << 2) | 0x01ULL;

    uint64_t el;
    __asm__ volatile("mrs %0, CurrentEL" : "=r"(el));
    el >>= 2;

    if (el == 2) {
        /* TCR_EL2: 39-bit VA (T0SZ=25), 4KB granule, Inner/Outer WB, 40-bit PA (PS=2) */
        uint64_t tcr = (25ULL << 0) | (1ULL << 8) | (1ULL << 10) | (3ULL << 12) | (0ULL << 14) | (2ULL << 16);

        __asm__ volatile(
            "msr mair_el2, %0\n\t"
            "msr tcr_el2, %1\n\t"
            "msr ttbr0_el2, %2\n\t"
            "isb\n\t"
            "tlbi alle2\n\t"
            "dsb sy\n\t"
            "isb\n\t"
            "mrs x0, sctlr_el2\n\t"
            "orr x0, x0, #(1 << 0)\n\t"   /* M: MMU enable */
            "orr x0, x0, #(1 << 2)\n\t"   /* C: Data Cache enable */
            "orr x0, x0, #(1 << 12)\n\t"  /* I: Instruction Cache enable */
            "msr sctlr_el2, x0\n\t"
            "isb\n\t"
            : : "r"(mair), "r"(tcr), "r"(s_arm64_l1) : "x0", "memory"
        );
    } else {
        /* TCR_EL1: 39-bit VA (T0SZ=25), 4KB granule, Inner/Outer WB, 40-bit PA (IPS=2 at bit 32) */
        uint64_t tcr = (25ULL << 0) | (1ULL << 8) | (1ULL << 10) | (3ULL << 12) | (0ULL << 14) | (2ULL << 32);

        __asm__ volatile(
            "msr mair_el1, %0\n\t"
            "msr tcr_el1, %1\n\t"
            "msr ttbr0_el1, %2\n\t"
            "isb\n\t"
            "tlbi vmalle1\n\t"
            "dsb sy\n\t"
            "isb\n\t"
            "mrs x0, sctlr_el1\n\t"
            "orr x0, x0, #(1 << 0)\n\t"   /* M: MMU enable */
            "orr x0, x0, #(1 << 2)\n\t"   /* C: Data Cache enable */
            "orr x0, x0, #(1 << 12)\n\t"  /* I: Instruction Cache enable */
            "msr sctlr_el1, x0\n\t"
            "isb\n\t"
            : : "r"(mair), "r"(tcr), "r"(s_arm64_l1) : "x0", "memory"
        );
    }
}
#endif

__attribute__((section(".text._start"), naked))
void _start(void) {
#if defined(__aarch64__)
    __asm__ volatile(
        "adrp x0, __stack_top\n\t"
        "add  x0, x0, :lo12:__stack_top\n\t"
        "and  x0, x0, #~15\n\t"
        "mov  sp, x0\n\t"
        "mrs  x0, mpidr_el1\n\t"
        "and  x0, x0, #0xFF\n\t"
        "cbz  x0, 1f\n\t"
        "2: wfe\n\t"
        "b 2b\n\t"
        "1:\n\t"

        /* Install exception vector table */
        "adr x0, arm64_vector_table\n\t"
        "msr vbar_el1, x0\n\t"

        /* Check CurrentEL and enable FP/SIMD (NEON) */
        "mrs x0, CurrentEL\n\t"
        "lsr x0, x0, #2\n\t"
        "cmp x0, #3\n\t"
        "b.ne 6f\n\t"
        "msr cptr_el3, xzr\n\t"
        "b 8f\n\t"
        "6:\n\t"
        "cmp x0, #2\n\t"
        "b.ne 7f\n\t"
        "adr x1, arm64_vector_table\n\t"
        "msr vbar_el2, x1\n\t"
        "msr cptr_el2, xzr\n\t"
        "mrs x1, cpacr_el1\n\t"
        "orr x1, x1, #(3 << 20)\n\t"
        "msr cpacr_el1, x1\n\t"
        "b 8f\n\t"
        "7:\n\t"
        "mrs x0, cpacr_el1\n\t"
        "orr x0, x0, #(3 << 20)\n\t"
        "msr cpacr_el1, x0\n\t"
        "8:\n\t"
        "isb\n\t"

        /* Zero .bss section */
        "ldr x0, =__bss_start\n\t"
        "ldr x1, =__bss_end\n\t"
        "4: cmp x0, x1\n\t"
        "b.ge 5f\n\t"
        "str xzr, [x0], #8\n\t"
        "b 4b\n\t"
        "5:\n\t"

        "bl btron_main\n\t"
        "3: wfe\n\t"
        "b 3b\n\t"
    );
#else
    __asm__ volatile(
        "ldr sp, =__stack_top\n\t"
        "bic sp, sp, #7\n\t"
        "mrc p15, 0, r0, c0, c0, 5\n\t"
        "ands r0, r0, #3\n\t"
        "beq 1f\n\t"
        "2: wfe\n\t"
        "b 2b\n\t"
        "1:\n\t"
        "mrc p15, 0, r0, c1, c0, 2\n\t"
        "orr r0, r0, #(0xF << 20)\n\t"
        "mcr p15, 0, r0, c1, c0, 2\n\t"
        "isb\n\t"
        "mov r0, #(1 << 30)\n\t"
        "vmsr fpexc, r0\n\t"
        "ldr r0, =__bss_start\n\t"
        "ldr r1, =__bss_end\n\t"
        "mov r2, #0\n\t"
        "4: cmp r0, r1\n\t"
        "bge 5f\n\t"
        "str r2, [r0], #4\n\t"
        "b 4b\n\t"
        "5:\n\t"
        "bl btron_main\n\t"
        "3: wfe\n\t"
        "b 3b\n\t"
    );
#endif
}
