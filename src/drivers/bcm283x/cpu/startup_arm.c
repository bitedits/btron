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
#include <btron/arm64_mem.h>

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
 * Bare-metal heap: use a fixed high address (32MB) so it stays clear of:
 *   - kernel text/data/BSS at 0x80000..__bss_end
 *   - the non-cacheable DMA window at 24-26 MB (btron/arm64_mem.h)
 * 32MB gives us 1GB - 32MB = ~992MB of headroom on the far side.
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

/* VideoCore property buffer.  It lives in the non-cacheable window rather than
 * .bss because the property channel writes its response back into this RAM,
 * which a cached read would miss once the MMU is on.  16-byte alignment is the
 * mailbox interface's requirement, satisfied by the window offset. */
static volatile uint32_t * const mbox = (volatile uint32_t *)BTRON_NOCACHE_MBOX_BOOT;
uint32_t *g_pi_fb_ptr = NULL;
uint32_t  g_pi_fb_size = 0;   /* bytes the VideoCore actually allocated */

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
        g_pi_fb_size = fb_sz ? fb_sz : (uint32_t)w * h * 4u;
        return g_pi_fb_ptr;
    }

    uart_puts("[QEMU-ARM] Framebuffer allocation fallback.\n");
    g_pi_fb_ptr = (uint32_t*)0x3c000000;
    g_pi_fb_size = (uint32_t)w * h * 4u;
    return g_pi_fb_ptr;
}

int mailbox_set_virtual_offset(uint32_t x, uint32_t y) {
    uintptr_t mbox_base = g_mmio_base + 0x0000b880UL;
    volatile uint32_t *status_reg = (volatile uint32_t*)(mbox_base + MBOX_STATUS);
    volatile uint32_t *write_reg  = (volatile uint32_t*)(mbox_base + MBOX_WRITE);
    volatile uint32_t *read_reg   = (volatile uint32_t*)(mbox_base + MBOX_READ);

    /* Coherent non-cacheable DMA mailbox buffer */
    volatile uint32_t *mbox_buf = (volatile uint32_t *)BTRON_NOCACHE_MBOX_ARM;
    mbox_buf[0] = 8 * 4;       /* buffer size */
    mbox_buf[1] = 0;           /* request code */
    mbox_buf[2] = 0x00048009;  /* tag: SET_VIRTUAL_OFFSET */
    mbox_buf[3] = 8;           /* value buffer size */
    mbox_buf[4] = 0;           /* request/response size */
    mbox_buf[5] = x;           /* x offset */
    mbox_buf[6] = y;           /* y offset */
    mbox_buf[7] = 0;           /* end tag */

    uint32_t mbox_addr = (uint32_t)BTRON_NOCACHE_MBOX_ARM;
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
        /* Current EL with SP0.  PSTATE.SPSel resets to 0 and _start never
         * changes it, so the kernel actually runs here: exceptions vector to
         * the SP0 group (0x000/0x080/...), NOT the SPx group below.  Route
         * SP0 Sync -> shared fault-skip handler (95) and SP0 IRQ -> full
         * stub (90); the SPx entries are kept mirrored for safety. */
        ".balign 128\n\tb 95f\n\t"   /* 0x000 SP0 Synchronous -> fault skip */
        ".balign 128\n\tb 90f\n\t"   /* 0x080 SP0 IRQ         -> dispatch  */
        ".balign 128\n\tb 90f\n\t"   /* 0x100 SP0 FIQ         -> dispatch  */
        ".balign 128\n\teret\n\t"     /* 0x180 SP0 SError                   */

        /* Current EL with SPx: skip faulting instruction safely */
        ".balign 128\n\t"
        "95:\n\t"
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

        /* Current EL with SPx, IRQ (VBAR+0x280): branch to full stub below */
        ".balign 128\n\tb 90f\n\t"
        /* SPx FIQ (VBAR+0x300): the GIC signals the Group-0 timer as FIQ */
        ".balign 128\n\tb 90f\n\t"
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

        /* IRQ stub (outside the 128-byte vector slots): save the full GP
         * register set, dispatch through the GIC-400 in C, restore, eret.
         * Frame: 272 bytes = x0..x30 (248) + elr (248) + spsr (256). */
        "90:\n\t"
        /* Stack-free entry breadcrumb: bump s_stub_entries and paint a white
         * bar at fb row0 using only scratch regs (their interrupted values are
         * spilled to s_stub_scratch first, so resume state stays intact). */
        "adrp x18, s_stub_scratch\n\t"
        "add  x18, x18, :lo12:s_stub_scratch\n\t"
        "stp  x16, x17, [x18]\n\t"
        "str  x18, [x18, #16]\n\t"
        "adrp x16, s_stub_entries\n\t"
        "add  x16, x16, :lo12:s_stub_entries\n\t"
        "ldr  x17, [x16]\n\t"
        "add  x17, x17, #1\n\t"
        "str  x17, [x16]\n\t"
        "adrp x18, s_stub_scratch\n\t"
        "add  x18, x18, :lo12:s_stub_scratch\n\t"
        "ldp  x16, x17, [x18]\n\t"
        "ldr  x18, [x18, #16]\n\t"
        "sub sp, sp, #272\n\t"
        "stp x0, x1, [sp, #0]\n\t"
        "stp x2, x3, [sp, #16]\n\t"
        "stp x4, x5, [sp, #32]\n\t"
        "stp x6, x7, [sp, #48]\n\t"
        "stp x8, x9, [sp, #64]\n\t"
        "stp x10, x11, [sp, #80]\n\t"
        "stp x12, x13, [sp, #96]\n\t"
        "stp x14, x15, [sp, #112]\n\t"
        "stp x16, x17, [sp, #128]\n\t"
        "stp x18, x19, [sp, #144]\n\t"
        "stp x20, x21, [sp, #160]\n\t"
        "stp x22, x23, [sp, #176]\n\t"
        "stp x24, x25, [sp, #192]\n\t"
        "stp x26, x27, [sp, #208]\n\t"
        "stp x28, x29, [sp, #224]\n\t"
        "str x30, [sp, #240]\n\t"
        "mrs x0, CurrentEL\n\t"
        "lsr x0, x0, #2\n\t"
        "cmp x0, #2\n\t"
        "b.ne 92f\n\t"
        "mrs x0, elr_el2\n\t"
        "mrs x1, spsr_el2\n\t"
        "b 93f\n\t"
        "92:\n\t"
        "mrs x0, elr_el1\n\t"
        "mrs x1, spsr_el1\n\t"
        "93:\n\t"
        "stp x0, x1, [sp, #248]\n\t"
        "bl arm64_trace_pre_dispatch\n\t"
        "bl arm64_irq_dispatch\n\t"
        "bl arm64_trace_post_dispatch\n\t"
        "ldp x0, x1, [sp, #248]\n\t"
        "mrs x2, CurrentEL\n\t"
        "lsr x2, x2, #2\n\t"
        "cmp x2, #2\n\t"
        "b.ne 94f\n\t"
        "msr elr_el2, x0\n\t"
        "msr spsr_el2, x1\n\t"
        "b 95f\n\t"
        "94:\n\t"
        "msr elr_el1, x0\n\t"
        "msr spsr_el1, x1\n\t"
        "95:\n\t"
        "ldp x2, x3, [sp, #16]\n\t"
        "ldp x4, x5, [sp, #32]\n\t"
        "ldp x6, x7, [sp, #48]\n\t"
        "ldp x8, x9, [sp, #64]\n\t"
        "ldp x10, x11, [sp, #80]\n\t"
        "ldp x12, x13, [sp, #96]\n\t"
        "ldp x14, x15, [sp, #112]\n\t"
        "ldp x16, x17, [sp, #128]\n\t"
        "ldp x18, x19, [sp, #144]\n\t"
        "ldp x20, x21, [sp, #160]\n\t"
        "ldp x22, x23, [sp, #176]\n\t"
        "ldp x24, x25, [sp, #192]\n\t"
        "ldp x26, x27, [sp, #208]\n\t"
        "ldp x28, x29, [sp, #224]\n\t"
        "ldr x30, [sp, #240]\n\t"
        "ldp x0, x1, [sp, #0]\n\t"
        "add sp, sp, #272\n\t"
        "eret\n\t"
    );
}

/* ─────────────────────────────────────────────────────────────────
 * BCM2711 GIC-400 + ARM Generic Timer: real 1 kHz system IRQ
 *
 * ASYNC.txt requires a hardware periodic tick so the input plane is
 * driven by an interrupt, not by elapsed-time checks inside the
 * cooperative GUI loop.  The kernel runs at EL2, so we arm the EL2
 * physical timer (PPI 26) and move it to GIC Group 1 so it is
 * signaled as a plain IRQ (Group 0 would arrive as a masked FIQ).
 * ───────────────────────────────────────────────────────────────── */

#define GICD_BASE_ADDR   0xFF841000UL
#define GICC_BASE_ADDR   0xFF842000UL

#define GICD_CTLR        (*(volatile uint32_t *)(GICD_BASE_ADDR + 0x000))
#define GICD_TYPER       (*(volatile uint32_t *)(GICD_BASE_ADDR + 0x004))
/* GICv2 resets every PPI/SGI to Group 0, which the CPU interface signals as
 * FIQ.  We only unmask DAIF.I, so the timer PPI must be moved to Group 1 to
 * arrive as a plain IRQ; otherwise the tick is swallowed by the empty FIQ
 * vector and arm64_irq_init() reports success while no tick is ever seen. */
#define GICD_IGROUPR0    (*(volatile uint32_t *)(GICD_BASE_ADDR + 0x080))
#define GICD_ISENABLER0  (*(volatile uint32_t *)(GICD_BASE_ADDR + 0x100))
#define GICD_ISPENDR0    (*(volatile uint32_t *)(GICD_BASE_ADDR + 0x200))
#define GICD_ICPENDR0    (*(volatile uint32_t *)(GICD_BASE_ADDR + 0x280))
#define GICC_CTLR        (*(volatile uint32_t *)(GICC_BASE_ADDR + 0x000))
#define GICC_PMR         (*(volatile uint32_t *)(GICC_BASE_ADDR + 0x004))
#define GICC_BPR         (*(volatile uint32_t *)(GICC_BASE_ADDR + 0x008))
#define GICC_IAR         (*(volatile uint32_t *)(GICC_BASE_ADDR + 0x00C))
#define GICC_EOIR        (*(volatile uint32_t *)(GICC_BASE_ADDR + 0x010))
#define GICC_HPPIR       (*(volatile uint32_t *)(GICC_BASE_ADDR + 0x018))
#define GICC_IIDR        (*(volatile uint32_t *)(GICC_BASE_ADDR + 0x0FC))

/* The kernel drops EL2 -> EL1 in _start (firmware configures the GIC for a
 * Non-secure EL1 OS).  At EL1 the PHYSICAL timer/counter is not usable here:
 * CNTHCTL_EL2.EL1PCEN/EL1PCTEN writes do not take effect on this firmware, so
 * the physical counter/timer sysregs trap to EL2.  The VIRTUAL timer (CNTV,
 * PPI 27) is owned by EL1 and needs no CNTHCTL grant (as Haiku arm64 does), so
 * use it.  The EL2 physical timer (CNTHP, PPI 26) is kept for an EL2 boot. */
#define ARM_TIMER_PPI_EL1 27
#define ARM_TIMER_PPI_EL2 26

/* Set by the kernel core (core_arm64.c) to rpi_timer_tick once the USB
 * host controller is fully enumerated.  Never call heavyweight code here:
 * the hook runs in IRQ context and must stay bounded (ASYNC.txt §4). */
void (*g_arm64_timer_hook)(void) = 0;

static uint32_t s_arm64_timer_reload = 0;
static uint32_t s_arm64_timer_intid  = 0;
static int      s_arm64_at_el2       = 0;
static uint32_t s_irq_cfg_idx        = 0;

/* Incremented at the top of arm64_irq_dispatch() so the boot self-test can
 * tell "CPU never took the IRQ" apart from "timer never asserted its PPI". */
volatile uint32_t g_arm64_irq_dispatch_hits = 0;
/* 1 once a software-forced pending timer PPI was actually delivered. */
static volatile uint32_t s_selftest_seen = 0;
/* 1 once the masked probe confirmed an ack-able GIC config for the timer PPI;
 * gates the DAIF unmask at the end of arm64_irq_init(). */
static volatile uint32_t s_irq_probe_ok = 0;

/* Exception stack.  On entry to EL1 the CPU forces PSTATE.SPSel=1, so IRQ/FIQ/
 * sync handlers run on SP_EL1 even though the kernel thread itself is EL1t on
 * SP_EL0.  Without this the stub's frame push hits an uninitialized SP_EL1 and
 * faults recursively (the silent post-unmask hang).  32 KiB, 64-byte aligned. */
uint64_t s_exc_stack[4096] __attribute__((aligned(64)));

/* Stack-free IRQ-entry breadcrumb (temporary diagnostic).  g_irq_trace_fb is
 * mirrored from fb_log_enable(); the stub paints a white bar with scratch regs
 * and NO stack at its very first instruction, a green bar before bl dispatch
 * and a red bar after dispatch returns, so a stall can be trisected on HDMI:
 *   none        -> exception never reaches the stub (vector/VBAR)
 *   white only  -> dies in the register-save / elr-spsr region
 *   white+green -> dies inside arm64_irq_dispatch
 *   white+grn+red -> dispatch returned; stall is at eret or an IRQ storm
 * Stride hardcoded to 1024 px (BTRON_SCREEN_W); bars sit at rows 0/1/2. */
volatile uint32_t *g_irq_trace_fb = 0;
uint64_t s_stub_scratch[4] __attribute__((aligned(16)));
volatile uint64_t s_stub_entries = 0;

/* Framebuffer paint diagnostics REMOVED (2026-09-20): writing the console fb
 * from IRQ context corrupted the live console and ate log lines once the
 * armstub made interrupts partially live.  Entry counting now relies solely on
 * the s_stub_entries global, read out-of-band by the fallback dump. */
void arm64_trace_pre_dispatch(void)  { }
void arm64_trace_post_dispatch(void) { }

void arm64_irq_dispatch(void) {
    g_arm64_irq_dispatch_hits++;
    /* GICC_IAR / GICC_EOIR are suspected of locking the BCM2711 bus at NS EL1
     * on this firmware: every silent hang traces to the first taken interrupt,
     * and the masked probe only ever exercised HPPIR/PMR/CTLR reads and GICD
     * stores — never IAR/EOIR (we already know this GIC bus-locks on some
     * accesses, e.g. the GICD_ISPENDR0 store).  So the ISR uses only
     * proven-safe accesses: HPPIR read for the intid, the cntv reload store,
     * and a distributor ICPENDR0 store to drop the pending PPI so the edge
     * line deasserts instead of re-storming.  Revisit IAR/EOIR once ticking. */
    uint32_t hppir = *(volatile uint32_t *)(GICC_BASE_ADDR + 0x018);
    uint32_t intid = hppir & 0x3FFu;
    if (intid == s_arm64_timer_intid) {
        if (g_arm64_timer_hook) g_arm64_timer_hook();
        /* One-shot down timer: reload for the next 1 ms tick */
        if (s_arm64_at_el2) {
            __asm__ volatile("msr cnthp_tval_el2, %0"
                             : : "r"((uint64_t)s_arm64_timer_reload));
        } else {
            __asm__ volatile("msr cntv_tval_el0, %0"
                             : : "r"((uint64_t)s_arm64_timer_reload));
        }
        GICD_ICPENDR0 = (1u << intid);  /* drop pending; no IAR/EOIR ack */
    } else if (intid < 1020u) {
        /* Stray Group-1 source: the armstub moved EVERY interrupt to Group 1,
         * so any peripheral line the firmware left enabled now reaches EL1.
         * We never service it, and an un-acked level source would re-assert
         * and storm the stub (starving the cooperative loop: dead keyboard,
         * torn console).  Disable it at the distributor and drop its pending
         * bit so the line goes idle. */
        uint32_t reg = intid / 32u, bit = intid % 32u;
        *(volatile uint32_t *)(GICD_BASE_ADDR + 0x180u + reg * 4u) = (1u << bit);
        *(volatile uint32_t *)(GICD_BASE_ADDR + 0x280u + reg * 4u) = (1u << bit);
    }
}

/* Snapshot of the GIC / timer state for the boot-time fallback dump. */
typedef struct {
    uint32_t el;
    uint32_t timer_intid;
    uint32_t dispatch_hits;
    uint32_t stub_entries;
    uint32_t selftest_seen;
    uint32_t cfg_idx;
    uint32_t gicd_typer;
    uint32_t gicd_ctlr;
    uint32_t gicd_igroupr0;
    uint32_t gicd_isenabler0;
    uint32_t gicd_ispendr0;
    uint32_t gicc_ctlr;
    uint32_t gicc_pmr;
    uint32_t gicc_hppir;
    uint32_t gicc_iidr;
    uint32_t cnthp_ctl;
    uint32_t cntp_ctl;
    uint32_t cntfrq;
} arm64_irq_diag_t;

void arm64_irq_get_diag(arm64_irq_diag_t *d) {
    if (!d) return;
    uint64_t v;
    __asm__ volatile("mrs %0, CurrentEL"    : "=r"(v)); d->el = (uint32_t)(v >> 2) & 3u;
    d->timer_intid   = s_arm64_timer_intid;
    d->dispatch_hits = g_arm64_irq_dispatch_hits;
    d->stub_entries  = (uint32_t)s_stub_entries;
    d->selftest_seen = s_selftest_seen;
    d->cfg_idx       = s_irq_cfg_idx;
    d->gicd_typer    = GICD_TYPER;
    d->gicd_ctlr     = GICD_CTLR;
    d->gicd_igroupr0 = GICD_IGROUPR0;
    d->gicd_isenabler0 = GICD_ISENABLER0;
    d->gicd_ispendr0 = GICD_ISPENDR0;
    d->gicc_ctlr     = GICC_CTLR;
    d->gicc_pmr      = GICC_PMR;
    d->gicc_hppir    = GICC_HPPIR;
    d->gicc_iidr     = GICC_IIDR;
    d->cnthp_ctl     = 0;   /* CNTHP is EL2-only; undefined at EL1 */
    if (d->el == 2u) {
        __asm__ volatile("mrs %0, cnthp_ctl_el2" : "=r"(v)); d->cnthp_ctl = (uint32_t)v;
    }
    /* At EL1 the physical timer sysregs trap (CNTHCTL not granted), so report
     * the virtual timer control we actually use; field name kept for ABI. */
    __asm__ volatile("mrs %0, cntv_ctl_el0"  : "=r"(v)); d->cntp_ctl  = (uint32_t)v;
    __asm__ volatile("mrs %0, cntfrq_el0"    : "=r"(v)); d->cntfrq    = (uint32_t)v;
}

uint32_t arm64_irq_selftest_seen(void) { return s_selftest_seen; }

int arm64_irq_init(uint32_t hz) {
    extern void fb_log(const char *);
    extern void fb_log_dec(uint32_t);
    extern void fb_log_hex32(uint32_t);
    fb_log("[IRQi] enter hz="); fb_log_dec(hz); fb_log("\n");
    if (hz == 0 || hz > 100000u) { fb_log("[IRQi] bad hz\n"); return -1; }

    uint64_t cntfrq = 0;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(cntfrq));
    fb_log("[IRQi] cntfrq="); fb_log_dec((uint32_t)cntfrq); fb_log("\n");
    /* BCM2711 arch timer runs at 54 MHz; reject absent/bogus frequencies */
    if (cntfrq < 1000000ULL || cntfrq > 200000000ULL) { fb_log("[IRQi] bad cntfrq\n"); return -1; }
    s_arm64_timer_reload = (uint32_t)(cntfrq / hz);
    if (s_arm64_timer_reload == 0) { fb_log("[IRQi] reload 0\n"); return -1; }

    uint64_t el = 0;
    __asm__ volatile("mrs %0, CurrentEL" : "=r"(el));
    s_arm64_at_el2 = (((el >> 2) & 0x3u) == 2u);
    s_arm64_timer_intid = s_arm64_at_el2 ? ARM_TIMER_PPI_EL2 : ARM_TIMER_PPI_EL1;
    fb_log("[IRQi] EL="); fb_log_dec((uint32_t)(el >> 2));
    fb_log(" intid="); fb_log_dec(s_arm64_timer_intid);
    fb_log(" reload="); fb_log_dec(s_arm64_timer_reload); fb_log("\n");

    /* KEEP IRQ/FIQ MASKED for the whole probe.  Every prior attempt unmasked
     * here and armed the real timer, then hung silently right after "armed":
     * the taken interrupt never reached arm64_irq_dispatch (no [IRQd] marker)
     * and never returned to the observe loop, so the vector/entry path itself
     * dies on this firmware.  To break that hang loop and get a decisive
     * answer, the probe now arms the timer with DAIF MASKED and reads the GIC
     * pending state back over MMIO instead of taking the interrupt:
     *   GICD_ISPENDR0 bit27 set  => CNTV timer fired and latched at the GIC.
     *   GICC_HPPIR == 27         => CPU interface sees it (delivery should
     *                               work the moment DAIF is unmasked).
     * This can never hang (no exception is taken) and tells us exactly whether
     * the fault is timer->GIC, GIC->CPU, or the CPU vector entry path. */
    fb_log("[IRQi] probe MASKED (pending readback)\n");
    __asm__ volatile("msr daifset, #3");

    /* The BCM2711 GIC's security configuration (which group the timer PPI
     * lands in, and which GICD/GICC enable bits our access level may write)
     * is not something we can deduce reliably from here: register writes to
     * IGROUPR / EnableGrp1 were observed to be silently ignored on hardware.
     * So probe a small set of known-good configurations and keep the first
     * one whose software-forced pending PPI is actually taken by the CPU. */
    {
        void (*saved_hook)(void) = g_arm64_timer_hook;
        g_arm64_timer_hook = 0;   /* forced IRQs must not fake a real tick */

        /* Config order matters: the probe keeps the FIRST entry whose timer
         * PPI latches (ISPENDR bit27) AND is presented by the CPU interface
         * (HPPIR==27).  Hardware proved that chain works, so the real fault is
         * whether the selected config can be ACKNOWLEDGED at Non-secure EL1.
         * A Group-0 interrupt (FIQ) needs GICC_CTLR.AckCtl (bit2) to be acked
         * via IAR at NS EL1; without it IAR returns 1022, EOI never clears the
         * interrupt, and it re-triggers forever (the old unmasked hang).  The
         * clean NS-EL1 path is Group 1 -> IRQ with GICC EnableGrp1, so try the
         * Group-1 configs first and keep Group-0+AckCtl / Haiku as fallbacks. */
        static const struct { uint32_t gicd, gicc, igroup, pmr, bpr; } cfgs[] = {
            { 0x3, 0x3, 0xFFFFFFFFu, 0xFF, 0x7 },  /* Group1 -> IRQ, EnableGrp0+1 */
            { 0x3, 0x7, 0xFFFFFFFFu, 0xFF, 0x7 },  /* Group1 -> IRQ, + AckCtl     */
            { 0x2, 0x2, 0xFFFFFFFFu, 0xFF, 0x7 },  /* Group1 only enables         */
            { 0x3, 0x5, 0x00000000u, 0xFF, 0x7 },  /* Group0 -> FIQ + AckCtl      */
            { 0x3, 0x1, 0x00000000u, 0xFF, 0x7 },  /* Group0, EnableGrp0 (Haiku)  */
        };
        int found = -1;
        for (uint32_t c = 0; c < (sizeof(cfgs)/sizeof(cfgs[0])) && found < 0; c++) {
            fb_log("[IRQi] probe cfg="); fb_log_dec(c); fb_log("\n");
            GICD_CTLR = 0;
            GICC_CTLR = 0;
            GICD_ICPENDR0 = (1u << s_arm64_timer_intid);
            GICD_IGROUPR0 = cfgs[c].igroup;
            GICC_PMR      = cfgs[c].pmr;
            GICC_BPR      = cfgs[c].bpr;
            {
                volatile uint32_t *ipr = (volatile uint32_t *)
                    (GICD_BASE_ADDR + 0x400u + (s_arm64_timer_intid / 4u) * 4u);
                uint32_t lane = (s_arm64_timer_intid % 4u) * 8u;
                *ipr = (*ipr & ~(0xFFu << lane)) | (0x80u << lane);
            }
            fb_log("[IRQi] c="); fb_log_dec(c); fb_log(" mid\n");
            GICD_ISENABLER0 = (1u << s_arm64_timer_intid);
            GICD_CTLR = cfgs[c].gicd;
            GICC_CTLR = cfgs[c].gicc;
            fb_log("[IRQi] c="); fb_log_dec(c); fb_log(" ctl\n");
            __asm__ volatile("dsb sy" : : : "memory");
            __asm__ volatile("isb");
            fb_log("[IRQi] c="); fb_log_dec(c); fb_log(" regs\n");

            /* Arm the REAL timer for this config.  We do NOT write
             * GICD_ISPENDR0 (force pending): on hardware that store to the
             * timer PPI never completes and locks the BCM2711 bus at EL1. */
            if (s_arm64_at_el2) {
                __asm__ volatile("msr cnthp_tval_el2, %0"
                                 : : "r"((uint64_t)s_arm64_timer_reload));
                __asm__ volatile("msr cnthp_ctl_el2, %0" : : "r"(1ULL));
            } else {
                __asm__ volatile("msr cntv_tval_el0, %0"
                                 : : "r"((uint64_t)s_arm64_timer_reload));
                __asm__ volatile("msr cntv_ctl_el0, %0" : : "r"(1ULL));
            }
            __asm__ volatile("isb");
            fb_log("[IRQi] c="); fb_log_dec(c); fb_log(" armed\n");
            /* Wait ~10 ms via the BCM2835 system timer (1 MHz MMIO; a plain
             * memory read that cannot trap), then read the GIC pending state
             * back over MMIO.  DAIF is masked so no interrupt is taken and this
             * cannot hang.  ISPENDR0 bit<intid> set => the timer fired and
             * latched at the distributor; HPPIR == intid => the CPU interface
             * recognizes it, so delivery should work once DAIF is unmasked. */
            {
                volatile uint32_t *systimer_clo =
                    (volatile uint32_t *)(g_mmio_base + 0x00003004UL);
                uint32_t t0 = *systimer_clo;
                for (uint32_t spin = 0; spin < 4000000u; spin++) {
                    if ((uint32_t)(*systimer_clo - t0) >= 10000u) break;
                }
            }
            uint32_t pend  = GICD_ISPENDR0;
            uint32_t hppir = *(volatile uint32_t *)(GICC_BASE_ADDR + 0x018);
            fb_log("[IRQi] c="); fb_log_dec(c);
            fb_log(" pend=");  fb_log_hex32(pend);
            fb_log(" hppir="); fb_log_hex32(hppir); fb_log("\n");
            /* Always disarm + clear pending before the next config / return. */
            if (s_arm64_at_el2) {
                __asm__ volatile("msr cnthp_ctl_el2, %0" : : "r"(0ULL));
            } else {
                __asm__ volatile("msr cntv_ctl_el0, %0" : : "r"(0ULL));
            }
            if ((pend & (1u << s_arm64_timer_intid)) &&
                ((hppir & 0x3FFu) == s_arm64_timer_intid)) {
                found = (int)c;
                s_irq_cfg_idx = c;
            }
            GICD_ICPENDR0 = (1u << s_arm64_timer_intid);  /* drop leftover */
        }
        s_selftest_seen = (found >= 0) ? 1u : 0u;
        s_irq_probe_ok  = (found >= 0) ? 1u : 0u;
        g_arm64_timer_hook = saved_hook;
        fb_log("[IRQi] probe done found="); fb_log_dec((uint32_t)(found + 1));
        fb_log(" hits="); fb_log_dec(g_arm64_irq_dispatch_hits); fb_log("\n");

        /* Re-assert the chosen config's enables (the loop left GICD/GICC CTLR,
         * IGROUPR, PMR, priority and ISENABLER at cfgs[found], but be explicit)
         * so the unmask below delivers into a fully-programmed interface. */
        if (found >= 0) {
            GICD_IGROUPR0   = cfgs[found].igroup;
            GICC_PMR        = cfgs[found].pmr;
            GICC_BPR        = cfgs[found].bpr;
            GICD_ISENABLER0 = (1u << s_arm64_timer_intid);
            GICD_CTLR       = cfgs[found].gicd;
            GICC_CTLR       = cfgs[found].gicc;
            __asm__ volatile("dsb sy" : : : "memory");
            __asm__ volatile("isb");
        }
    }

    fb_log("[IRQi] arming timer\n");
    if (s_arm64_at_el2) {
        /* Arm the EL2 physical timer (dedicated to EL2, not trapped) */
        __asm__ volatile("msr cnthp_tval_el2, %0"
                         : : "r"((uint64_t)s_arm64_timer_reload));
        __asm__ volatile("msr cnthp_ctl_el2, %0" : : "r"(1ULL)); /* ENABLE, !IMASK */
    } else {
        /* Arm the EL1 virtual timer (EL1-owned, no CNTHCTL grant needed) */
        __asm__ volatile("msr cntv_tval_el0, %0"
                         : : "r"((uint64_t)s_arm64_timer_reload));
        __asm__ volatile("msr cntv_ctl_el0, %0" : : "r"(1ULL)); /* ENABLE, !IMASK */
    }
    __asm__ volatile("isb");
    /* Exception handlers run on SP_EL1 (entry to EL1 forces SPSel=1), but the
     * EL1t thread stack is SP_EL0 and never provides SP_EL1.  Point it at the
     * dedicated exception stack before unmasking, or the first taken interrupt
     * faults on the stub's frame push.  Done here (not in _start) so early boot
     * stays byte-identical to the last build that reached the console. */
    __asm__ volatile("msr sp_el1, %0"
                     : : "r"((uint64_t)(uintptr_t)s_exc_stack +
                             (uint64_t)sizeof(s_exc_stack)));
    __asm__ volatile("isb");
    /* REVERTED 2026-09-20: the EL3 armstub route (SCR_EL3 + Group-1) made the
     * board worse without delivering a tick — stripe-corrupted console and a
     * dead keyboard even with every interrupt but the timer disabled, i.e.
     * damage from the stub's non-interrupt effects, and still zero ticks.
     * Leave the timer disarmed and DAIF masked (the known-good configuration
     * that boots clean with working input); core_arm64's confirm loop sees
     * zero ticks and drives the cooperative input path. */
    if (s_arm64_at_el2) {
        __asm__ volatile("msr cnthp_ctl_el2, %0" : : "r"(0ULL)); /* DISABLE */
    } else {
        __asm__ volatile("msr cntv_ctl_el0, %0" : : "r"(0ULL));  /* DISABLE */
    }
    __asm__ volatile("msr daifset, #3");  /* keep IRQ+FIQ masked */
    __asm__ volatile("isb");
    fb_log("[IRQi] left masked; cooperative input path (armstub route reverted)\n");
    return 0;
}

/* Stop the timer PPI and re-mask IRQ.  Used when arm64_irq_init() reported
 * success but no tick is ever observed, so the caller can fall back to the
 * cooperative input path without leaving a half-armed interrupt behind. */
void arm64_irq_disable(void) {
    if (s_arm64_at_el2) {
        __asm__ volatile("msr cnthp_ctl_el2, %0" : : "r"(0ULL)); /* DISABLE */
    } else {
        __asm__ volatile("msr cntv_ctl_el0, %0" : : "r"(0ULL)); /* DISABLE */
    }
    GICD_CTLR = 0;
    GICC_CTLR = 0;
    __asm__ volatile("msr daifset, #3");  /* mask IRQ and FIQ */
    __asm__ volatile("dsb sy" : : : "memory");
    __asm__ volatile("isb");
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

/* MAIR AttrIndx a virtual address resolves to, by walking the same identity
 * tables installed above.  Diagnostic: it names the attribute a buffer really
 * gets, which is the one thing the source text cannot show. */
int arm64_mmu_attr_of(uintptr_t va) {
    uint64_t l1 = s_arm64_l1[(va >> 30) & 511u];
    if (!(l1 & 1u)) return -1;
    if (!(l1 & 2u)) return (int)((l1 >> 2) & 7u);          /* 1 GB block */
    uint64_t l2 = s_arm64_l2[(va >> 21) & 511u];
    if (!(l2 & 1u)) return -1;
    if (!(l2 & 2u)) return (int)((l2 >> 2) & 7u);          /* 2 MB block */
    return -1;                                             /* no L3 mapped */
}

/* Non-zero while the static image, the bump heap and the .nocache output
 * section all stay inside/outside the non-cacheable window as intended.  It
 * used to sit inside .bss, which silently ran the tail of the segment uncached;
 * this is the invariant that must not regress.  The .nocache half guards the
 * other direction: objects there are shared with a caching-less bus master, so
 * drifting past the window end would put them back into write-back RAM. */
int arm64_mmu_layout_ok(void) {
    extern char __bss_end[];
    extern char __nocache_start[];
    extern char __nocache_end[];
    return ((uintptr_t)__bss_end <= (uintptr_t)BTRON_NOCACHE_BASE) &&
           (HEAP_BASE >= (uintptr_t)BTRON_NOCACHE_END) &&
           ((uintptr_t)__nocache_start >= (uintptr_t)BTRON_NOCACHE_BASE) &&
           ((uintptr_t)__nocache_end <= (uintptr_t)BTRON_NOCACHE_END);
}

void arm64_mmu_init(uintptr_t fb_base, uint32_t fb_bytes) {
    /* Invalidate the I-cache before M/C/I go to 1.  There is no all-lines
     * D-cache invalidation available here (AArch64 has DC ISW by set/way only,
     * and a hand-rolled CCSIDR walk is a worse failure mode than the hazard),
     * so btron_main() stamps .bss/stack/heap words and reads them back after
     * this call to prove no stale D-cache line survived. */
    __asm__ volatile("ic iallu\n\t"
                     "dsb sy\n\t"
                     "isb\n\t" ::: "memory");

    /* Attr 0 = Device-nGnRE (0x04)
     * Attr 1 = Normal Cacheable Inner/Outer Write-Back (0xFF)
     * Attr 2 = Normal Non-Cacheable (0x44) for hardware DMA rings/buffers
     */
    uint64_t mair = (0x44ULL << 16) | (0xFFULL << 8) | (0x04ULL << 0);

    /* L2 Table: 512 entries of 2MB (covers 0 to 1GB)
     * Code, data, .bss and the bump heap are all write-back cacheable.  The two
     * RAM exceptions are the BTRON_NOCACHE_BASE window (24MB, 2MB) shared by
     * the xHCI / DMA / mailbox drivers, which sits in the gap between
     * __bss_end and HEAP_BASE on purpose so no static object can land in it,
     * and the scanout framebuffer itself: the VideoCore picks that address and
     * reports it through the mailbox, so it is passed in rather than assumed.
     * With the framebuffer cacheable the CPU's writes would never reach the
     * display, so its whole 2MB-aligned span gets attr2.  Both addresses seen
     * in practice -- 0x3C000000 on the Pi and 0x3C100000 in QEMU -- are 960MB,
     * i.e. inside the 1GB this table covers; the guard below is only for a
     * firmware that hands back an address above 1GB, where L1[3]'s 1GB Device
     * block wins and an L2 entry would be dead weight that makes
     * arm64_mmu_attr_of() lie.
     */
    const uint64_t nocache_first = (uint64_t)BTRON_NOCACHE_BASE >> 21;
    const uint64_t nocache_last  = (uint64_t)BTRON_NOCACHE_END >> 21;
    uint64_t fb_first = 0, fb_last = 0;   /* L2 covers 0-1GB only */
    if (fb_base && fb_base < 0x40000000ULL) {
        /* The 1GB Device block at L1[3] shadows L2 for anything at or above
         * 0x40000000, so an attr2 entry written there would never be reached
         * and would only make the attribute readback disagree with the walk. */
        fb_first = (uint64_t)fb_base >> 21;
        fb_last  = ((uint64_t)fb_base + fb_bytes + 0x1FFFFFull) >> 21;
    }
    /* Block/section descriptor encoding (ARMv8-A, same field positions at every
     * level; cross-checked against Linux's PMD_SECT_* / PTE_* macros):
     *   bits[1:0] = 0b01        block
     *   bits[4:2]   AttrIndx
     *   bit[5]      RES0
     *   bits[7:6]   AP[2:1]     0b00 = EL1 read/write, EL0 no access
     *   bits[9:8]   SH          0b11 = Outer Shareable; 0b00 for Device
     *   bit[10]     AF          must be preset: the CPU is not allowed to
     *                           hardware-update it because TCR_EL1.HA is 0
     *   bit[11]     nG          (RES0 in 1GB blocks)
     *   bits[53]/[54] PXN/UXN   0 = executable, which the kernel needs
     * Getting SH wrong here is not a benign mis-annotation: the previous
     * (3 << 6) landed in AP[1:2] and made every RAM block read-only, so loads
     * worked while stores took a level-2 permission fault (ESR 0x9600004e).
     */
    const uint64_t sh_outer = (3ULL << 8);      /* SH=11, Outer Shareable */
    const uint64_t af       = (1ULL << 10);

    for (uint64_t i = 0; i < 512; i++) {
        const uint64_t uncached =
            (i >= nocache_first && i < nocache_last) || (i >= fb_first && i < fb_last);
        s_arm64_l2[i] = (i * 0x200000ULL) | af | sh_outer | ((uncached ? 2ULL : 1ULL) << 2) | 0x01ULL;
    }

    /* L1 Table: 512 entries of 1GB */
    for (int i = 0; i < 512; i++) {
        s_arm64_l1[i] = 0;
    }

    /* L1[0]: Point to L2 table (bits [1:0] = 11 for Table) */
    s_arm64_l1[0] = ((uint64_t)(uintptr_t)s_arm64_l2) | 0x03ULL;

    /* L1[1]: 1GB to 2GB RAM -> Normal Cacheable */
    s_arm64_l1[1] = 0x40000000ULL | af | sh_outer | (1ULL << 2) | 0x01ULL;

    /* L1[2]: 2GB to 3GB RAM -> Normal Cacheable */
    s_arm64_l1[2] = 0x80000000ULL | af | sh_outer | (1ULL << 2) | 0x01ULL;

    /* L1[3]: 3GB to 4GB (0xC0000000 - 0xFFFFFFFF) -> Device memory (MMIO + PCIe RC).
     * Device blocks take SH=00 (0b11 is reserved for Device), AttrIndx=0. */
    s_arm64_l1[3] = 0xC0000000ULL | af | 0x01ULL;

    /* L1[24]: 24GB (0x600000000ULL, 1GB window) -> Device memory (PCIe Outbound to VL805) */
    s_arm64_l1[24] = 0x600000000ULL | af | 0x01ULL;

    /* _start normalises the boot state to EL1t whichever EL the loader entered
     * at (EL2 on the Pi, EL3 under QEMU), so the EL1 regime is the only one
     * this ever programs.  Writing the EL1 registers from a higher EL looks
     * successful while SCTLR at the executing EL stays M=0, which is why the
     * band reads back CurrentEL and SCTLR M/C/I instead of trusting this call.
     *
     * TCR_EL1: 39-bit VA (T0SZ=25) so the walk starts at level 1, 4KB granule
     * (TG0=0), Inner/Outer Write-Back, Inner+Outer Shareable (SH0=11), 40-bit
     * PA (IPS=2 at bits[34:32]), and EPD1 (bit 22) to park the never-initialised
     * TTBR1 half of the address space. */
    const uint64_t tcr = (25ULL << 0) | (1ULL << 8) | (1ULL << 10) | (3ULL << 12) |
                         (0ULL << 14) | (1ULL << 22) | (2ULL << 32);

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

        /* Drop EL3 -> Non-secure EL2h.  The Pi firmware hands kernel8.img to
         * EL2, but QEMU's raspi4b machine enters at EL3, and skipping the
         * drop there left the whole kernel running at a different EL than the
         * one it ships on -- the EL3 MMU regime is not the hardware path, and
         * exceptions there vector through VBAR_EL3=0.  Landing at EL2h makes
         * both loaders run the identical, hardware-proven EL2 -> EL1t
         * sequence below. */
        "mrs x0, CurrentEL\n\t"
        "lsr x0, x0, #2\n\t"
        "cmp x0, #3\n\t"
        "b.ne 46f\n\t"
        "mov x0, #0x3C9\n\t"                   /* SPSR: EL2h, DAIF masked */
        "msr spsr_el3, x0\n\t"
        "adr x0, 48f\n\t"
        "msr elr_el3, x0\n\t"
        "mov x0, #1\n\t"
        "orr x0, x0, #(1 << 8)\n\t"            /* HCE: EL2 enabled */
        "orr x0, x0, #(1 << 10)\n\t"           /* RW:  AArch64 at EL2/EL1 */
        "msr scr_el3, x0\n\t"
        "isb\n\t"
        "eret\n\t"
        "48:\n\t"
        /* Now at Non-secure EL2h with an undefined SP_EL2; restore it before
         * anything can take an exception here. */
        "adrp x0, __stack_top\n\t"
        "add  x0, x0, :lo12:__stack_top\n\t"
        "and  x0, x0, #~15\n\t"
        "mov  sp, x0\n\t"
        "46:\n\t"

        /* Drop EL2 -> EL1 (AArch64).  Firmware configures the GIC-400 groups
         * and the arch timer for a Non-secure EL1 OS (as Linux/Haiku run on
         * the Pi 4); from EL2 the CPU-interface group/enable writes are
         * ignored and no interrupt is ever delivered.  Land in EL1t so the
         * kernel keeps the SP_EL0 stack set up above. */
        "mrs x0, CurrentEL\n\t"
        "lsr x0, x0, #2\n\t"
        "cmp x0, #2\n\t"
        "b.ne 47f\n\t"
        "mov x0, #0x80000000\n\t"          /* HCR_EL2.RW = AArch64 EL1 */
        "msr hcr_el2, x0\n\t"
        "msr cptr_el2, xzr\n\t"            /* don't trap FP/SIMD to EL2 */
        "msr hstr_el2, xzr\n\t"            /* don't trap CP15 to EL2 */
        "mov x0, #3\n\t"
        "msr cnthctl_el2, x0\n\t"          /* EL1PCTEN|EL1PCEN: EL1 counter/timer access */
        "msr cntvoff_el2, xzr\n\t"
        /* Safety net: if anything still traps to EL2 (counter/timer/FP), land
         * in the real vector table instead of VBAR_EL2=0 -> address 0. */
        "adr x0, arm64_vector_table\n\t"
        "msr vbar_el2, x0\n\t"
        "adr x0, 47f\n\t"
        "msr elr_el2, x0\n\t"
        "mov x0, #0x3C4\n\t"               /* SPSR: EL1t, DAIF masked */
        "msr spsr_el2, x0\n\t"
        "isb\n\t"
        "eret\n\t"
        "47:\n\t"

        /* Re-establish SP at the current EL (EL1t uses SP_EL0; the EL2
         * 'mov sp' above may have targeted SP_EL2). */
        "adrp x0, __stack_top\n\t"
        "add  x0, x0, :lo12:__stack_top\n\t"
        "and  x0, x0, #~15\n\t"
        "mov  sp, x0\n\t"

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

        /* Zero .nocache too.  Its VMA is 25MB into the address space, so it is
         * an image-less NOLOAD section that neither the ELF loader nor the .bss
         * clear above reaches; without this a DWC2 setup packet starts life as
         * whatever the previous boot left at that address. */
        "ldr x0, =__nocache_start\n\t"
        "ldr x1, =__nocache_end\n\t"
        "9: cmp x0, x1\n\t"
        "b.ge 10f\n\t"
        "str xzr, [x0], #8\n\t"
        "b 9b\n\t"
        "10:\n\t"

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
