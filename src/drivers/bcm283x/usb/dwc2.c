/*
 * B-TRON Retro OS — BCM283x DWC2 USB 2.0 Host Controller & HID Driver
 *
 * Cleanroom implementation for Raspberry Pi 2B (BCM2836) under QEMU.
 *
 * QEMU raspi2b specifics:
 *   Peripheral base:  0x20000000  (BCM2835 map — QEMU raspi2b uses BCM2835 periph)
 *   DWC2 OTG base:    0x20980000
 *   USB-kbd → addr 1, USB-mouse → addr 2 after enumeration
 *
 * Enumeration (USB 2.0 §9 + HID §7.2, Boot Protocol):
 *   SET_ADDRESS → SET_CONFIGURATION → SET_PROTOCOL(0) → SET_IDLE(0)
 *
 * Key correctness points (all were bugs in previous versions):
 *   1. HCINTMSK(0) must be set before channel 0 is used — QEMU only updates
 *      HCINT when the matching HCINTMSK bit is enabled.
 *   2. Timeouts must be small (fail-fast) — a TIMEOUT=500000 outer loop
 *      with 100000 inner loop = 50 billion iterations = multi-minute hang.
 *   3. GRXFSIZ / GNPTXFSIZ / HPTXFSIZ must be programmed before DMA transfers.
 *   4. GAHBCFG needs burst length (INCR4) alongside DMA enable.
 *   5. GINTMSK SOF bit must be set — QEMU's DWC2 model advances the frame
 *      counter only when SOF interrupts are enabled, and interrupt-endpoint
 *      channels are scheduled on frame boundaries.
 */

#include <dwc2.h>
#include <btron/event.h>
#include <stdint.h>
#include <stdbool.h>

#if (!defined(__STDC_HOSTED__) || __STDC_HOSTED__ == 0) && (defined(__arm__) || defined(__aarch64__))

/* ── UART helpers (provided by startup_arm.c) ── */
extern void uart_puts(const char *s);
extern void uart_hex32(uint32_t val);

/* ── MMIO helpers ── */
static inline uint32_t mmio_read32(uintptr_t addr) {
    return *(volatile uint32_t *)addr;
}
static inline void mmio_write32(uintptr_t addr, uint32_t val) {
    *(volatile uint32_t *)addr = val;
}
static inline uint32_t dwc2_read(uint32_t reg) {
    return mmio_read32((uintptr_t)(DWC2_BASE_ADDR + reg));
}
static inline void dwc2_write(uint32_t reg, uint32_t val) {
    mmio_write32((uintptr_t)(DWC2_BASE_ADDR + reg), val);
}
static inline void dsb(void) {
    __asm__ volatile("dsb sy" : : : "memory");
}
static inline void delay_cycles(volatile int n) {
    while (n-- > 0) { __asm__ volatile("nop"); }
}

/* ─────────────────────────────────────────────
 * DWC2 channel interrupt bits
 * ───────────────────────────────────────────── */
#define HCINT_XFRC   (1u << 0)
#define HCINT_CHH    (1u << 1)
#define HCINT_STALL  (1u << 3)
#define HCINT_NAK    (1u << 4)
#define HCINT_ACK    (1u << 5)
#define HCINT_TXERR  (1u << 7)
#define HCINT_BBERR  (1u << 8)
#define HCINT_ALL    0x7FFu

/* DWC2 HCTSIZ PID field (bits [30:29]) */
#define PID_DATA0    0u
#define PID_DATA1    2u
#define PID_SETUP    3u

/* ─────────────────────────────────────────────
 * Timeouts — kept small so boot never hangs
 * At ~100 MMIO reads/s in QEMU, 10000 = ~0.1ms
 * ───────────────────────────────────────────── */
#define POLL_TIMEOUT       10000   /* inner poll loop per channel op  */
#define STATUS_RETRIES       100   /* max NAK retries on STATUS phase */

/* ─────────────────────────────────────────────
 * Driver state
 * ───────────────────────────────────────────── */
static bool     g_dwc2_initialized  = false;
static bool     g_kbd_attached      = false;
static bool     g_mouse_attached    = false;
static uint8_t  g_last_kbd_key      = 0;
static uint8_t  g_last_kbd_mod      = 0;
static uint8_t  g_last_mouse_btns   = 0;
static uint8_t  g_kbd_addr          = 1;
static uint8_t  g_mouse_addr        = 2;

/* DMA buffers — 16-byte aligned */
static usb_kbd_report_t   g_kbd_dma_buf   __attribute__((aligned(16)));
static usb_mouse_report_t g_mouse_dma_buf __attribute__((aligned(16)));

/* PID toggles for interrupt IN channels */
static uint32_t g_kbd_pid   = PID_DATA0;
static uint32_t g_mouse_pid = PID_DATA0;

static bool g_kbd_chan_active   = false;
static bool g_mouse_chan_active = false;

/* Setup packet + scratch buffer for control transfers */
static usb_setup_packet_t g_setup_buf  __attribute__((aligned(16)));
static uint8_t            g_ctrl_data[64] __attribute__((aligned(16)));

/* ─────────────────────────────────────────────
 * dwc2_chan_halt — safely disable a channel
 * ───────────────────────────────────────────── */
static void dwc2_chan_halt(int ch)
{
    uint32_t hcchar = dwc2_read(DWC2_HCCHAR(ch));
    if (hcchar & (1u << 31)) { /* channel is enabled */
        dwc2_write(DWC2_HCCHAR(ch), hcchar | (1u << 30)); /* ChDis */
        for (int i = 0; i < 1000; i++) {
            if (!(dwc2_read(DWC2_HCCHAR(ch)) & (1u << 31))) break;
        }
    }
    dwc2_write(DWC2_HCINT(ch), HCINT_ALL);
}

/* ─────────────────────────────────────────────
 * dwc2_ctrl_out — send a USB control transfer
 *
 * Sends SETUP + zero-length STATUS IN (no data phase).
 * Used for SET_ADDRESS, SET_CONFIGURATION, SET_PROTOCOL, SET_IDLE.
 *
 * Returns 0 on success, -1 on error/timeout.
 * ───────────────────────────────────────────── */
static int dwc2_ctrl_out(uint8_t dev_addr,
                         uint8_t bmRequestType,
                         uint8_t bRequest,
                         uint16_t wValue,
                         uint16_t wIndex,
                         uint16_t wLength)
{
    const int CH  = 0;   /* always use channel 0 for control */
    const int MPS = 8;   /* EP0 MPS = 8 for FS devices */

    /* Build SETUP packet */
    g_setup_buf.bmRequestType = bmRequestType;
    g_setup_buf.bRequest      = bRequest;
    g_setup_buf.wValue        = wValue;
    g_setup_buf.wIndex        = wIndex;
    g_setup_buf.wLength       = wLength;
    dsb();

    /* Clear channel and enable mask for ch0 */
    dwc2_chan_halt(CH);
    dwc2_write(DWC2_HCINTMSK(CH),
               HCINT_XFRC | HCINT_CHH | HCINT_NAK |
               HCINT_STALL | HCINT_TXERR | HCINT_BBERR);

    /* ── SETUP phase (OUT, PID=SETUP) ── */
    uint32_t hcchar_base = ((uint32_t)dev_addr << 22) | (0u << 18) | (uint32_t)MPS;

    dwc2_write(DWC2_HCDMA(CH),  (uint32_t)(uintptr_t)&g_setup_buf);
    dwc2_write(DWC2_HCTSIZ(CH), (PID_SETUP << 29) | (1u << 19) | 8u);
    dwc2_write(DWC2_HCCHAR(CH), (1u << 31) | hcchar_base);
    dsb();

    /* Poll until XFRC or error */
    bool setup_ok = false;
    for (int i = 0; i < POLL_TIMEOUT; i++) {
        uint32_t hcint = dwc2_read(DWC2_HCINT(CH));
        if (hcint & HCINT_XFRC) {
            dwc2_write(DWC2_HCINT(CH), HCINT_ALL);
            setup_ok = true;
            break;
        }
        if (hcint & (HCINT_STALL | HCINT_TXERR | HCINT_BBERR)) {
            dwc2_write(DWC2_HCINT(CH), HCINT_ALL);
            uart_puts("[DWC2] SETUP error\n");
            return -1;
        }
        /* SETUP packets must not NAK per USB spec; ignore if QEMU does */
        if (hcint & HCINT_NAK) {
            dwc2_write(DWC2_HCINT(CH), HCINT_ALL);
        }
    }
    if (!setup_ok) {
        uart_puts("[DWC2] SETUP timeout\n");
        return -1;
    }

    /* ── STATUS phase: zero-length IN (DATA1) — retry on NAK ── */
    dwc2_write(DWC2_HCINT(CH), HCINT_ALL);

    for (int retry = 0; retry < STATUS_RETRIES; retry++) {
        dwc2_chan_halt(CH);  /* ensure previous state is clean */

        dwc2_write(DWC2_HCDMA(CH),  (uint32_t)(uintptr_t)g_ctrl_data);
        dwc2_write(DWC2_HCTSIZ(CH), (PID_DATA1 << 29) | (1u << 19) | 0u);
        dwc2_write(DWC2_HCCHAR(CH), (1u << 31) | (1u << 15) | hcchar_base); /* IN */
        dsb();

        for (int j = 0; j < POLL_TIMEOUT; j++) {
            uint32_t hcint = dwc2_read(DWC2_HCINT(CH));
            if (hcint & HCINT_XFRC) {
                dwc2_write(DWC2_HCINT(CH), HCINT_ALL);
                return 0; /* success */
            }
            if (hcint & HCINT_NAK) {
                dwc2_write(DWC2_HCINT(CH), HCINT_ALL);
                delay_cycles(500); /* device still processing */
                break;             /* retry outer loop */
            }
            if (hcint & (HCINT_STALL | HCINT_TXERR)) {
                dwc2_write(DWC2_HCINT(CH), HCINT_ALL);
                uart_puts("[DWC2] STATUS error\n");
                return -1;
            }
        }
    }

    uart_puts("[DWC2] STATUS timeout — proceeding anyway\n");
    return 0; /* treat timeout as non-fatal for QEMU compatibility */
}

/* ─────────────────────────────────────────────
 * Enumerate one HID device (Boot Protocol)
 * ───────────────────────────────────────────── */
static int dwc2_enumerate_hid(uint8_t dev_addr, uint8_t iface)
{
    int r;

    uart_puts("[DWC2] SET_ADDRESS → addr=");
    uart_hex32(dev_addr);
    uart_puts("\n");
    r = dwc2_ctrl_out(0, 0x00, USB_REQ_SET_ADDRESS, dev_addr, 0, 0);
    if (r < 0) {
        uart_puts("[DWC2] SET_ADDRESS failed\n");
        return -1;
    }
    delay_cycles(20000); /* 2ms: device applies new address */

    uart_puts("[DWC2] SET_CONFIGURATION(1)\n");
    r = dwc2_ctrl_out(dev_addr, 0x00, USB_REQ_SET_CONFIGURATION, 1, 0, 0);
    if (r < 0) uart_puts("[DWC2] SET_CONFIGURATION failed (non-fatal)\n");
    delay_cycles(5000);

    /* SET_PROTOCOL(0) = Boot Protocol: class request (0x21), iface */
    uart_puts("[DWC2] SET_PROTOCOL → Boot\n");
    r = dwc2_ctrl_out(dev_addr, 0x21, 0x0B, 0, iface, 0);
    if (r < 0) uart_puts("[DWC2] SET_PROTOCOL failed (non-fatal)\n");
    delay_cycles(5000);

    /* SET_IDLE(0): suppress redundant reports */
    r = dwc2_ctrl_out(dev_addr, 0x21, 0x0A, 0, iface, 0);
    (void)r; /* STALL on SET_IDLE is acceptable */
    delay_cycles(5000);

    uart_puts("[DWC2] Enumeration complete for addr=");
    uart_hex32(dev_addr);
    uart_puts("\n");
    return 0;
}

/* ─────────────────────────────────────────────
 * Queue Interrupt IN for keyboard (ch1) / mouse (ch2)
 * ───────────────────────────────────────────── */
static void dwc2_queue_kbd_in(void)
{
    dwc2_write(DWC2_HCINT(1),    HCINT_ALL);
    dwc2_write(DWC2_HCINTMSK(1), HCINT_XFRC | HCINT_NAK | HCINT_CHH);
    dwc2_write(DWC2_HCDMA(1),    (uint32_t)(uintptr_t)&g_kbd_dma_buf);
    dwc2_write(DWC2_HCTSIZ(1),   (g_kbd_pid << 29) | (1u << 19) | 8u);
    /* Ch1: Dev=g_kbd_addr EP=1 IN Interrupt MPS=8 */
    uint32_t hcchar = (1u << 31) | (1u << 15) | (3u << 18)
                    | ((uint32_t)g_kbd_addr << 22) | (1u << 11) | 8u;
    dwc2_write(DWC2_HCCHAR(1), hcchar);
    g_kbd_chan_active = true;
}

static void dwc2_queue_mouse_in(void)
{
    dwc2_write(DWC2_HCINT(2),    HCINT_ALL);
    dwc2_write(DWC2_HCINTMSK(2), HCINT_XFRC | HCINT_NAK | HCINT_CHH);
    dwc2_write(DWC2_HCDMA(2),    (uint32_t)(uintptr_t)&g_mouse_dma_buf);
    dwc2_write(DWC2_HCTSIZ(2),   (g_mouse_pid << 29) | (1u << 19) | 4u);
    /* Ch2: Dev=g_mouse_addr EP=1 IN Interrupt MPS=4 */
    uint32_t hcchar = (1u << 31) | (1u << 15) | (3u << 18)
                    | ((uint32_t)g_mouse_addr << 22) | (1u << 11) | 4u;
    dwc2_write(DWC2_HCCHAR(2), hcchar);
    g_mouse_chan_active = true;
}

/* ─────────────────────────────────────────────
 * dwc2_init — full host controller + device init
 * ───────────────────────────────────────────── */
int dwc2_init(void)
{
#if defined(__aarch64__)
    extern uintptr_t g_mmio_base;
    if (g_mmio_base == 0xFE000000UL) {
        /* BCM2711 / Pi 4 / Pi 400: DWC2 is only used for USB-C OTG and is disabled in DTB.
         * The keyboard and USB ports are routed via PCIe to the VIA VL805 xHCI controller.
         * Accessing 0xFE980000 while unclocked/disabled by firmware causes a bus stall.
         */
        uart_puts("[DWC2] Pi 4/400 (BCM2711) detected: DWC2 disabled in DTB (uses xHCI/PCIe).\n");
        return 0;
    }
#endif

    uart_puts("[QEMU-ARM] DWC2 USB 2.0 init: base=0x");
    uart_hex32(DWC2_BASE_ADDR);
    uart_puts("\n");

    /* Read core ID — should be 0x4F54XXXX for DesignWare */
    uint32_t snpsid = dwc2_read(0x040);
    uart_puts("[DWC2] GSNPSID=0x");
    uart_hex32(snpsid);
    if ((snpsid & 0xFFFF0000) != 0x4F540000) {
        uart_puts(" [DWC2 offline / not found]\n");
        return -1;
    }
    uart_puts(" (OK)\n");

    /* ── 1. Core Soft Reset ── */
    dwc2_write(DWC2_GRSTCTL, (1u << 0));
    for (int i = 0; i < 10000; i++) {
        if ((dwc2_read(DWC2_GRSTCTL) & (1u << 0)) == 0) break;
    }
    for (int i = 0; i < 10000; i++) {
        if (dwc2_read(DWC2_GRSTCTL) & (1u << 31)) break; /* AHB idle */
    }

    /* ── 2. Force Host Mode, FS PHY ── */
    uint32_t usbcfg = dwc2_read(DWC2_GUSBCFG);
    usbcfg &= ~((1u << 29) | (1u << 30));
    usbcfg |=  (1u << 29);  /* ForceHstMode */
    usbcfg |=  (1u << 6);   /* PHYSel FS */
    dwc2_write(DWC2_GUSBCFG, usbcfg);
    delay_cycles(5000);      /* wait for mode switch */

    /* ── 3. Configure RX/TX FIFOs (required before any DMA transfer) ──
     *  GRXFSIZ:    256 DWORDs = 1024 bytes  (receive FIFO)
     *  GNPTXFSIZ:  start=0x100, depth=0x100 (non-periodic = control/bulk)
     *  HPTXFSIZ:   start=0x200, depth=0x100 (periodic = interrupt/iso)  */
    dwc2_write(DWC2_GRXFSIZ,   0x0100u);
    dwc2_write(DWC2_GNPTXFSIZ, (0x0100u << 16) | 0x0100u);
    dwc2_write(DWC2_HPTXFSIZ,  (0x0100u << 16) | 0x0200u);

    /* ── 4. GAHBCFG: DMA enable + INCR4 burst + global interrupt ── */
    dwc2_write(DWC2_GAHBCFG,
               (1u << 5) |   /* DMAEn */
               (2u << 1) |   /* HBSTLEN = INCR4 */
               (1u << 0));   /* GlblIntrMsk = enable */

    /* ── 5. GINTMSK: enable host port + channel + SOF interrupts ──
     *  SOF (bit2) is required so QEMU's DWC2 model advances the
     *  internal frame counter and schedules periodic (interrupt) EPs */
    dwc2_write(DWC2_GINTMSK,
               (1u << 2)  |  /* SOF */
               (1u << 24) |  /* HPRTINT */
               (1u << 25));  /* HCINT (all channels) */

    /* ── 6. HCFG: FS PHY clock = 30/60 MHz ── */
    dwc2_write(DWC2_HCFG, 0u);

    /* ── 7. Enable interrupts for ALL channels in HAINTMSK ── */
    dwc2_write(DWC2_HAINTMSK, 0xFFFFu);

    /* ── 8. Port power ON ── */
    uint32_t hprt = dwc2_read(DWC2_HPRT0) & ~(7u << 1); /* mask W1C bits */
    hprt |= (1u << 12); /* PrtPwr */
    dwc2_write(DWC2_HPRT0, hprt);
    delay_cycles(50000);

    /* ── 9. USB bus reset (≥10ms per USB 2.0 spec) ── */
    hprt  = dwc2_read(DWC2_HPRT0) & ~(7u << 1);
    hprt |= (1u << 8); /* PrtRst */
    dwc2_write(DWC2_HPRT0, hprt);
    delay_cycles(500000);

    hprt  = dwc2_read(DWC2_HPRT0) & ~((7u << 1) | (1u << 8));
    dwc2_write(DWC2_HPRT0, hprt);
    delay_cycles(50000); /* post-reset settle */

    /* ── 10. Wait for port enable (PrtEna bit 2 must be 1) ── */
    bool port_enabled = false;
    for (int i = 0; i < 50000; i++) {
        uint32_t p = dwc2_read(DWC2_HPRT0);
        if (p & (1u << 2)) { port_enabled = true; break; }
        delay_cycles(10);
    }
    uart_puts(port_enabled
        ? "[DWC2] Port enabled (FS device connected)\n"
        : "[DWC2] Port not enabled after reset — USB device may not be attached\n");

    uart_puts("[DWC2] HPRT0=0x"); uart_hex32(dwc2_read(DWC2_HPRT0)); uart_puts("\n");
    uart_puts("[DWC2] GINTSTS=0x"); uart_hex32(dwc2_read(DWC2_GINTSTS)); uart_puts("\n");

    /* ── 11. Enumerate root device & downstream devices ── */
    uart_puts("[DWC2] Setting root device addr=1...\n");
    if (dwc2_ctrl_out(0, 0x00, USB_REQ_SET_ADDRESS, 1, 0, 0) == 0) {
        delay_cycles(20000);
        dwc2_ctrl_out(1, 0x00, USB_REQ_SET_CONFIGURATION, 1, 0, 0);
        delay_cycles(5000);

        /* Test if device 1 is a USB Hub by sending SET_FEATURE(PORT_POWER, port=1) */
        /* bmRequestType=0x23 (Class, Other/Port), bRequest=3 (SET_FEATURE), wValue=8 (PORT_POWER), wIndex=1 */
        int is_hub = (dwc2_ctrl_out(1, 0x23, 3, 8, 1, 0) == 0);

        if (is_hub) {
            uart_puts("[DWC2] USB Hub detected! Powering ports & enumerating devices...\n");
            /* Power port 2 */
            dwc2_ctrl_out(1, 0x23, 3, 8, 2, 0);
            delay_cycles(50000);

            /* Reset Hub Port 1 (Keyboard) */
            uart_puts("[DWC2] Resetting Hub Port 1 (Keyboard)...\n");
            dwc2_ctrl_out(1, 0x23, 3, 4 /* PORT_RESET */, 1, 0);
            delay_cycles(200000);

            /* Enumerate Keyboard at address 2 */
            uart_puts("[DWC2] Enumerating keyboard (addr=2, Boot Protocol)...\n");
            if (dwc2_enumerate_hid(2, 0) == 0) {
                g_kbd_attached = true;
                g_kbd_addr = 2;
            } else {
                g_kbd_attached = true;
                g_kbd_addr = 2;
            }

            /* Reset Hub Port 2 (Mouse) */
            uart_puts("[DWC2] Resetting Hub Port 2 (Mouse)...\n");
            dwc2_ctrl_out(1, 0x23, 3, 4 /* PORT_RESET */, 2, 0);
            delay_cycles(200000);

            /* Enumerate Mouse at address 3 */
            uart_puts("[DWC2] Enumerating mouse (addr=3, Boot Protocol)...\n");
            if (dwc2_enumerate_hid(3, 0) == 0) {
                g_mouse_attached = true;
                g_mouse_addr = 3;
            } else {
                g_mouse_attached = true;
                g_mouse_addr = 3;
            }
        } else {
            /* Direct device (no hub attached) */
            uart_puts("[DWC2] Direct device detected on root port\n");
            dwc2_ctrl_out(1, 0x21, 0x0B, 0, 0, 0); /* SET_PROTOCOL Boot */
            dwc2_ctrl_out(1, 0x21, 0x0A, 0, 0, 0); /* SET_IDLE */
            g_kbd_attached = true;
            g_kbd_addr = 1;
        }
    } else {
        uart_puts("[DWC2] Root device enumeration failed — polling anyway\n");
        g_kbd_attached = true;
        g_kbd_addr = 1;
        g_mouse_attached = true;
        g_mouse_addr = 2;
    }

    /* ── 13. Prime initial interrupt IN requests ── */
    g_dwc2_initialized = true;
    dwc2_queue_kbd_in();
    dwc2_queue_mouse_in();

    uart_puts("[DWC2] Init complete. Keyboard & Mouse polling active.\n");
    return 0;
}

bool dwc2_has_devices(void)
{
    return g_dwc2_initialized && (g_kbd_attached || g_mouse_attached);
}

/* ─────────────────────────────────────────────
 * USB HID Boot-Protocol → B-TRON key translation
 * ───────────────────────────────────────────── */
uint32_t dwc2_usb_to_btron_key(uint8_t scancode, uint8_t modifiers)
{
    bool shift = (modifiers & 0x22u) != 0;
    bool ctrl  = (modifiers & 0x11u) != 0;

    if (scancode >= 0x04 && scancode <= 0x1D) {
        if (ctrl) return (uint32_t)(scancode - 0x04 + 1); /* Ctrl+A=1..Z=26 */
        return (uint32_t)(shift ? 'A' : 'a') + (scancode - 0x04);
    }
    if (scancode >= 0x1E && scancode <= 0x26) {
        static const char shift_num[] = "!@#$%^&*(";
        return shift ? (uint32_t)(unsigned char)shift_num[scancode - 0x1E]
                     : (uint32_t)('1' + (scancode - 0x1E));
    }
    if (scancode == 0x27) return shift ? ')' : '0';

    switch (scancode) {
        case 0x28: return BTRON_KEY_RETURN;
        case 0x29: return BTRON_KEY_ESCAPE;
        case 0x2A: return BTRON_KEY_BACKSPACE;
        case 0x2B: return BTRON_KEY_TAB;
        case 0x2C: return ' ';
        case 0x2D: return shift ? '_' : '-';
        case 0x2E: return shift ? '+' : '=';
        case 0x2F: return shift ? '{' : '[';
        case 0x30: return shift ? '}' : ']';
        case 0x31: return shift ? '|' : '\\';
        case 0x33: return shift ? ':' : ';';
        case 0x34: return shift ? '"' : '\'';
        case 0x35: return shift ? '~' : '`';
        case 0x36: return shift ? '<' : ',';
        case 0x37: return shift ? '>' : '.';
        case 0x38: return shift ? '?' : '/';
        case 0x4F: return BTRON_KEY_RIGHT;
        case 0x50: return BTRON_KEY_LEFT;
        case 0x51: return BTRON_KEY_DOWN;
        case 0x52: return BTRON_KEY_UP;
        case 0x4A: return BTRON_KEY_HOME;
        case 0x4D: return BTRON_KEY_END;
        case 0x4B: return BTRON_KEY_PAGE_UP;
        case 0x4E: return BTRON_KEY_PAGE_DOWN;
        case 0x4C: return BTRON_KEY_DELETE;
        case 0x58: return BTRON_KEY_KP_ENTER;
        case 0x3A: return BTRON_KEY_F1;
        case 0x3B: return BTRON_KEY_F2;
        case 0x3C: return BTRON_KEY_F3;
        case 0x3D: return BTRON_KEY_F4;
        case 0x3E: return BTRON_KEY_F5;
        case 0x3F: return BTRON_KEY_F6;   /* Hiragana */
        case 0x40: return BTRON_KEY_F7;   /* Katakana */
        case 0x41: return BTRON_KEY_F8;   /* Halfwidth */
        case 0x42: return BTRON_KEY_F9;   /* Fullwidth Alpha */
        case 0x43: return BTRON_KEY_F10;  /* EN↔JP Toggle */
        case 0x44: return BTRON_KEY_F11;
        case 0x45: return BTRON_KEY_F12;
        default: break;
    }
    return 0;
}

/* ─────────────────────────────────────────────
 * dwc2_poll_keyboard — returns 1 on new event
 * ───────────────────────────────────────────── */
int dwc2_poll_keyboard(usb_kbd_report_t *report)
{
    if (!g_dwc2_initialized || !report) return -1;

    if (!g_kbd_chan_active) {
        dwc2_queue_kbd_in();
    }

    uint32_t hcint = dwc2_read(DWC2_HCINT(1));

    if (hcint & HCINT_XFRC) {
        dwc2_write(DWC2_HCINT(1), HCINT_ALL);
        g_kbd_chan_active = false;
        dsb();

        *report = g_kbd_dma_buf;
        g_kbd_pid = (g_kbd_pid == PID_DATA0) ? PID_DATA1 : PID_DATA0;

        bool changed = (report->keys[0] != g_last_kbd_key ||
                        report->modifiers != g_last_kbd_mod);
        g_last_kbd_key = report->keys[0];
        g_last_kbd_mod = report->modifiers;

        dwc2_queue_kbd_in();
        return changed ? 1 : 0;
    }

    if (hcint & (HCINT_NAK | HCINT_CHH)) {
        dwc2_write(DWC2_HCINT(1), HCINT_ALL);
        g_kbd_chan_active = false;
        /* Do not re-queue immediately on NAK; will be queued on next poll interval */
    }

    return 0;
}

/* ─────────────────────────────────────────────
 * dwc2_poll_mouse — returns 1 on new event
 * ───────────────────────────────────────────── */
int dwc2_poll_mouse(usb_mouse_report_t *report)
{
    if (!g_dwc2_initialized || !report) return -1;

    if (!g_mouse_chan_active) {
        dwc2_queue_mouse_in();
    }

    uint32_t hcint = dwc2_read(DWC2_HCINT(2));

    if (hcint & HCINT_XFRC) {
        dwc2_write(DWC2_HCINT(2), HCINT_ALL);
        g_mouse_chan_active = false;
        dsb();

        *report = g_mouse_dma_buf;
        g_mouse_pid = (g_mouse_pid == PID_DATA0) ? PID_DATA1 : PID_DATA0;

        bool changed = (report->dx != 0 || report->dy != 0 ||
                        report->buttons != g_last_mouse_btns);
        g_last_mouse_btns = report->buttons;

        dwc2_queue_mouse_in();
        return changed ? 1 : 0;
    }

    if (hcint & (HCINT_NAK | HCINT_CHH)) {
        dwc2_write(DWC2_HCINT(2), HCINT_ALL);
        g_mouse_chan_active = false;
        /* Do not re-queue immediately on NAK; will be queued on next poll interval */
    }

    return 0;
}

#endif /* Freestanding ARM */
