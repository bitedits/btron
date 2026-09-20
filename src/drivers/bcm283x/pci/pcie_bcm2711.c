/*
 * B-TRON Retro OS — BCM2711 PCIe Root Complex & VL805 Driver
 * Cleanroom implementation for Raspberry Pi 400 (AArch64 / BCM2711)
 *
 * On Raspberry Pi 400, the built-in keyboard and USB ports are wired
 * to the VIA VL805 USB 3.0 (xHCI) controller behind the PCIe bridge.
 */

#include <pcie.h>
#include <btron/arm64_mem.h>
#include <stdint.h>
#include <stdbool.h>

#if (!defined(__STDC_HOSTED__) || __STDC_HOSTED__ == 0) && defined(__aarch64__)

extern uintptr_t g_mmio_base;
extern void uart_puts(const char *s);
extern void uart_hex32(uint32_t val);
extern void fb_log(const char *msg);
extern void fb_log_hex32(uint32_t val);
extern void fb_log_hex64(uint64_t val);
extern void fb_log_dec(uint32_t val);

/* MMIO helpers */
static inline uint32_t mmio_read32(uintptr_t addr) {
    return *(volatile uint32_t *)addr;
}

static inline void mmio_write32(uintptr_t addr, uint32_t val) {
    *(volatile uint32_t *)addr = val;
}

static inline void dsb(void) {
    __asm__ volatile("dsb sy" : : : "memory");
}

/* Accurate delays driven by the BCM2835 system timer (1 MHz free-running
 * counter at g_mmio_base + 0x3004).  The previous nop-loop was mis-calibrated
 * (~150 iterations per "us" is closer to 1 us than the requested value and
 * varies with CPU clock), which starved the SerDes/link-training settle times
 * the Broadcom RC actually needs and produced intermittent link failures. */
static inline uint32_t systimer_clo(void) {
    return *(volatile uint32_t *)(g_mmio_base + 0x00003004UL);
}

static inline void delay_us(uint32_t us) {
    uint32_t start = systimer_clo();
    while ((uint32_t)(systimer_clo() - start) < us) {
        __asm__ volatile("nop");
    }
}

static inline void delay_ms(uint32_t ms) {
    uint32_t start = systimer_clo();
    uint32_t target = ms * 1000u;
    while ((uint32_t)(systimer_clo() - start) < target) {
        __asm__ volatile("nop");
    }
}

static inline uint32_t pcie_rc_read(uint32_t reg) {
    return mmio_read32(BCM2711_PCIE_REG_BASE + reg);
}

static inline void pcie_rc_write(uint32_t reg, uint32_t val) {
    mmio_write32(BCM2711_PCIE_REG_BASE + reg, val);
}

/* ─────────────────────────────────────────────────────────────────
 * PCI Configuration Space Access via BCM2711 ECAM / EXT_CFG Window
 *
 * For Bus 0 (Root Complex): Configuration registers are accessed
 * directly at offset & 0x0FFC from RC base (0xFD500000).
 *
 * For Bus > 0 (e.g. Bus 1 Device 00.0 VIA VL805):
 * 1. Write BDF to PCIE_EXT_CFG_INDEX (0x9000):
 *    Bits [27:20] = Bus, [19:15] = Device, [14:12] = Function
 * 2. Read / write data via PCIE_EXT_CFG_DATA window (0x8000 + offset)
 * ───────────────────────────────────────────────────────────────── */

uint32_t pci_read_config32(uint32_t bus, uint32_t dev, uint32_t func, uint32_t offset) {
    if (bus == 0) {
        return pcie_rc_read(offset & 0x0FFC);
    }

    uint32_t bdf_idx = ((bus & 0xFF) << 20) |
                       ((dev & 0x1F) << 15) |
                       ((func & 0x07) << 12);

    pcie_rc_write(PCIE_EXT_CFG_INDEX, bdf_idx);
    dsb();
    return pcie_rc_read(PCIE_EXT_CFG_DATA + (offset & 0x0FFC));
}

void pci_write_config32(uint32_t bus, uint32_t dev, uint32_t func, uint32_t offset, uint32_t val) {
    if (bus == 0) {
        pcie_rc_write(offset & 0x0FFC, val);
        dsb();
        return;
    }

    uint32_t bdf_idx = ((bus & 0xFF) << 20) |
                       ((dev & 0x1F) << 15) |
                       ((func & 0x07) << 12);

    pcie_rc_write(PCIE_EXT_CFG_INDEX, bdf_idx);
    dsb();
    pcie_rc_write(PCIE_EXT_CFG_DATA + (offset & 0x0FFC), val);
    dsb();
}

/* ─────────────────────────────────────────────────────────────────
 * VideoCore Mailbox VL805 Firmware Bootstrap
 *
 * Mailbox Property Tag: 0x00030058 (RPI_FIRMWARE_NOTIFY_XHCI_RESET)
 * Device Address: 0x00100000 (Bus 1, Device 0, Function 0)
 * ───────────────────────────────────────────────────────────────── */

#define MBOX_CH_PROP      8
#define MBOX_STATUS       0x18
#define MBOX_WRITE        0x20
#define MBOX_READ         0x00
#define MBOX_FULL         0x80000000u
#define MBOX_EMPTY        0x40000000u

static int bcm2711_reload_vl805_firmware(void) {
    /* Use the Non-Cacheable DMA window (Attr 2) so the VideoCore sees coherent memory */
    volatile uint32_t *mbox_buf = (volatile uint32_t *)BTRON_NOCACHE_MBOX_PCIE;
    uintptr_t mbox_base = g_mmio_base + 0x0000B880UL;
    volatile uint32_t *status_reg = (volatile uint32_t *)(mbox_base + MBOX_STATUS);
    volatile uint32_t *write_reg  = (volatile uint32_t *)(mbox_base + MBOX_WRITE);
    volatile uint32_t *read_reg   = (volatile uint32_t *)(mbox_base + MBOX_READ);

    mbox_buf[0] = 7 * sizeof(uint32_t);          /* total buffer length */
    mbox_buf[1] = 0;                             /* request code */
    mbox_buf[2] = RPI_FIRMWARE_NOTIFY_XHCI_RESET;/* tag: 0x00030058 */
    mbox_buf[3] = sizeof(uint32_t);              /* value buffer size */
    mbox_buf[4] = sizeof(uint32_t);              /* req/resp size */
    mbox_buf[5] = VL805_PCI_ADDR;                /* 0x00100000 (Bus 1, Dev 0, Func 0) */
    mbox_buf[6] = 0;                             /* end tag */

    uint32_t mbox_addr = (uint32_t)BTRON_NOCACHE_MBOX_PCIE;
    dsb();

    int to = 2000;
    while ((*status_reg & MBOX_FULL) && --to > 0) {
        __asm__ volatile("nop");
    }
    *write_reg = ((mbox_addr & 0xFFFFFFF0u) | MBOX_CH_PROP);

    to = 2000;
    while (--to > 0) {
        while ((*status_reg & MBOX_EMPTY) && --to > 0) {
            __asm__ volatile("nop");
        }
        if (to <= 0) break;
        uint32_t res = *read_reg;
        if ((res & 0xF) == MBOX_CH_PROP) break;
    }
    dsb();

    if (mbox_buf[1] == 0x80000000u) {
        return 0; /* success */
    }
    return -1;
}

/* ─────────────────────────────────────────────────────────────────
 * BCM2711 PCIe Root Complex Bring-up & VL805 Discovery
 * ───────────────────────────────────────────────────────────────── */

static uintptr_t s_vl805_mmio_base = 0;

uintptr_t bcm2711_pcie_get_vl805_mmio(void) {
    return s_vl805_mmio_base;
}

int bcm2711_pcie_init(void) {
    if (g_mmio_base != 0xFE000000UL) {
        /* Not a BCM2711 / Pi 4/400 */
        return -1;
    }

    fb_log("[PCIE] Initializing Broadcom STB PCIe Root Complex (0xFD500000)...\n");

    /* 1. Controller Reset Sequence (Linux pcie-brcmstb.c style)
     *
     * RGR1_SW_INIT_1 (0x9210):
     *   bit 1 = BRIDGE_INIT (bridge software reset)   -- resets RC logic
     *   bit 0 = PERST#      (PCIe Fundamental Reset)  -- resets VL805 endpoint
     *
     * Sequence:
     *   a) Assert both BRIDGE_INIT and PERST# simultaneously
     *   b) Enable SERDES (clear HARD_PCIE_HARD_DEBUG bit 27 = SERDES_IDDQ)
     *   c) De-assert BRIDGE_INIT (RC logic comes out of reset)
     *   d) Wait for SERDES PLL lock (~100us)
     *   e) De-assert PERST# (VL805 comes out of fundamental reset)
     */
    uint32_t val;

    /* a) Assert BRIDGE_INIT (bit 1) + PERST# (bit 0) */
    val = pcie_rc_read(0x9210);
    val |= 0x3u;   /* bits [1:0] = 11 */
    pcie_rc_write(0x9210, val);
    delay_us(100);

    /* b) Enable SERDES — clear SERDES_IDDQ (bit 27) in HARD_DEBUG */
    val = pcie_rc_read(0x4204);
    val &= ~(1u << 27); /* SERDES_IDDQ = 0 (powered on) */
    pcie_rc_write(0x4204, val);
    delay_us(100);

    /* c) De-assert BRIDGE_INIT (bit 1), keep PERST# asserted */
    val = pcie_rc_read(0x9210);
    val &= ~0x2u;  /* clear BRIDGE_INIT only */
    pcie_rc_write(0x9210, val);
    delay_us(100); /* U-Boot udelay(100): let RC logic + SERDES PLL stabilize */

    /* d) De-assert PERST# (bit 0) — VL805 begins reset de-assertion sequence */
    val = pcie_rc_read(0x9210);
    val &= ~0x1u;  /* clear PERST# */
    pcie_rc_write(0x9210, val);
    delay_us(100);

    fb_log("[PCIE] PERST# de-asserted, waiting for link training...\n");

    /* 2. Configure Inbound DMA Window (Linux pcie-brcmstb.c brcm_pcie_setup)
     *
     *  RC_BAR2 = 4GB inbound window: PCI 0x00000000 -> CPU RAM 0x00000000
     *
     *  PCIE_MISC_RC_BAR2_CONFIG_LO  (0x4034): SIZE field = 0x11 (4GB)
     *  PCIE_MISC_RC_BAR2_CONFIG_HI  (0x4038): base high = 0
     *  PCIE_MISC_MISC_CTRL          (0x4008): SCB0_SIZE (17<<27) + flags
     *  PCIE_MISC_UBUS_BAR2_CONFIG_REMAP (0x40B4): ACCESS_ENABLE = 1
     *  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
     *  THIS REGISTER ACTIVATES THE INBOUND DMA PATH ON THE UBUS FABRIC.
     *  Without it, RC_BAR2 size is set but DMA from VL805 to ARM RAM
     *  is DISABLED — all TRB/DCBAA reads by xHCI hardware return garbage.
     */
    pcie_rc_write(0x4034, 0x11);   /* RC_BAR2_CONFIG_LO: 4GB (size code 17) */
    pcie_rc_write(0x4038, 0x00);   /* RC_BAR2_CONFIG_HI: PCI base addr = 0 */
    /* SCB0_SIZE=17 (4GB) at bits[31:27], SCB_ACCESS_EN, CFG_READ_UR_MODE, RCB_MPS_MODE, RCB_64B_MODE */
    pcie_rc_write(0x4008, (17u << 27) | 0x2000u | 0x1000u | 0x400u | 0x80u);
    pcie_rc_write(0x402C, 0);      /* RC_BAR1: disable */
    pcie_rc_write(0x403C, 0);      /* RC_BAR3: disable */

    /* CRITICAL: Enable UBUS access for the inbound BAR2 window.
     * Ref: Linux pcie-brcmstb.c PCIE_MISC_UBUS_BAR2_CONFIG_REMAP_ACCESS_ENABLE_MASK
     * This bit connects the inbound PCIe DMA path to the UBUS interconnect
     * so that the VL805 can DMA-read DCBAA, Command Ring, Event Ring from RAM. */
    pcie_rc_write(0x40B4, 0x1);    /* UBUS_BAR2_CONFIG_REMAP: ACCESS_ENABLE = 1 */
    dsb();

    fb_log("[PCIE] Inbound DMA: RC_BAR2=4GB UBUS_REMAP=ENABLED\n");

    /* Set Little-Endian mode for Inbound Window BAR2 (0x0188 bits[3:2] = 0) */
    uint32_t vend_spec = pcie_rc_read(0x0188);
    vend_spec &= ~0x0Cu;
    pcie_rc_write(0x0188, vend_spec);
    dsb();

    /* (Controller already enabled by PERST# de-assertion above) */

    /* 4. Wait for controller and link training.
     *    PCIE_MISC_PCIE_STATUS (0x4068): bit 4 = PHYLINKUP, bit 5 = DL_ACTIVE.
     *    U-Boot polls this for up to ~100 ms (mdelay(5) x 20); PCIe link
     *    training routinely takes tens of ms, so the previous 1 ms budget was
     *    far too short and let enumeration start against a down link. */
    int to = 20;
    while (to-- > 0) {
        if ((pcie_rc_read(0x4068) & 0x30) == 0x30) break;
        delay_ms(5);
    }
    uint32_t state = pcie_rc_read(0x4068);
    uint32_t link_speed = pcie_rc_read(0x00BC) >> 16;
    fb_log("[PCIE] Bridge State=");
    fb_log_hex32(state);
    fb_log(" LinkSpeed=");
    fb_log_hex32(link_speed);
    fb_log("\n");

    /* 5. Set CPU->PCI Outbound memory window (0x600000000 -> 0xC0000000, 1GB) */
    pcie_rc_write(0x400C, BCM2711_PCIE_BUS_MEM_BASE); /* 0xC0000000 */
    pcie_rc_write(0x4010, 0x00000000u);
    pcie_rc_write(0x4070, 0x3FF00000u);               /* encode_cpu_window_low */
    pcie_rc_write(0x4080, 0x06u);                      /* encode_cpu_window_start_high */
    pcie_rc_write(0x4084, 0x06u);                      /* encode_cpu_window_end_high */
    dsb();

    /* 6. Configure Root Complex Class Code to PCI-to-PCI Bridge (0x060400) */
    pcie_rc_write(0x043C, (0x06 << 16) | (0x04 << 8)); /* PCI_ID_VAL3 */
    dsb();

    /* 7. Configure CLKREQ and L1SS in REG_PCIE_HARD_DEBUG (0x4204) */
    uint32_t hd = pcie_rc_read(0x4204);
    hd |= 0x2;        /* CLKREQ_ENABLE */
    hd |= 0x00200000; /* L1SS_ENABLE */
    pcie_rc_write(0x4204, hd);
    delay_us(100);

    /* 8. Configure Root Port Bridge Type 1 Header (Bus 0, Dev 0, Func 0) */
    pci_write_config32(0, 0, 0, 0x18, 0x00010100u);   /* Primary=0, Secondary=1, Sub=1 */
    pci_write_config32(0, 0, 0, 0x20, 0xFFFFC000u);   /* Memory Base=0xC000, Limit=0xFFFF */
    pci_write_config32(0, 0, 0, 0x24, 0xFFFFC000u);   /* Prefetchable Base=0xC000, Limit=0xFFFF */

    uint32_t rc_cmd = pci_read_config32(0, 0, 0, PCI_COMMAND);
    rc_cmd |= (PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER | (1u << 8));
    pci_write_config32(0, 0, 0, PCI_COMMAND, rc_cmd);
    dsb();

    /* 9. Enumerate VL805 at 01:00.0 */
    uint32_t id_reg = pci_read_config32(VL805_PCI_BUS, VL805_PCI_DEV, VL805_PCI_FUNC, PCI_VENDOR_ID);
    uint16_t vendor = (uint16_t)(id_reg & 0xFFFF);
    uint16_t device = (uint16_t)(id_reg >> 16);

    if (vendor == VL805_VENDOR_ID && device == VL805_DEVICE_ID) {
        fb_log("[PCIE] Found VIA VL805 USB 3.0 Host Controller (1106:3483) at 01:00.0\n");

        /* 10. Request VideoCore to bootstrap VL805 runtime firmware */
        fb_log("[PCIE] Requesting VideoCore VL805 firmware bootstrap (tag 0x00030058)...\n");
        if (bcm2711_reload_vl805_firmware() == 0) {
            fb_log("[PCIE] VL805 firmware upload: SUCCESS [OK]\n");
        } else {
            fb_log("[PCIE] VL805 firmware reload completed (pre-loaded/EEPROM)\n");
        }
        delay_ms(50); /* real settle for VL805 controller reboot after fw notify */

        /* 11. Program BAR0 to PCI address 0xC0000000 (after firmware reload) */
        pci_write_config32(VL805_PCI_BUS, VL805_PCI_DEV, VL805_PCI_FUNC, PCI_BAR0, BCM2711_PCIE_BUS_MEM_BASE);
        pci_write_config32(VL805_PCI_BUS, VL805_PCI_DEV, VL805_PCI_FUNC, PCI_BAR0 + 4, 0x00000000UL);

        /* 12. Enable Bus Master + Memory Space in VL805 PCI Command Register */
        uint32_t cmd = (PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER | (1u << 8) /* SERR */);
        pci_write_config32(VL805_PCI_BUS, VL805_PCI_DEV, VL805_PCI_FUNC, PCI_COMMAND, cmd);
        dsb();

        /* Verify configuration space readback */
        uint32_t check_bar0 = pci_read_config32(VL805_PCI_BUS, VL805_PCI_DEV, VL805_PCI_FUNC, PCI_BAR0);
        uint32_t check_cmd = pci_read_config32(VL805_PCI_BUS, VL805_PCI_DEV, VL805_PCI_FUNC, PCI_COMMAND);
        fb_log("[PCIE] VL805 BAR0: ");
        fb_log_hex32(check_bar0);
        fb_log(" CMD: ");
        fb_log_hex32(check_cmd);
        fb_log("\n");

        /* Ensure PCI-to-PCI Bridge Command has Master + Memory enabled */
        uint32_t bridge_cmd = pci_read_config32(0, 0, 0, PCI_COMMAND);
        bridge_cmd |= (PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER | (1u << 8));
        pci_write_config32(0, 0, 0, PCI_COMMAND, bridge_cmd);
        dsb();

        /* Map VL805 MMIO to CPU 64-bit address 0x600000000 */
        s_vl805_mmio_base = (uintptr_t)BCM2711_PCIE_CPU_MEM_BASE;

        /* Test MMIO Read */
        uint32_t test_read = *(volatile uint32_t *)s_vl805_mmio_base;
        fb_log("[PCIE] VL805 MMIO Test Read: ");
        fb_log_hex32(test_read);
        fb_log("\n");

        return 0;
    } else {
        fb_log("[PCIE] Device 01:00.0 ID: ");
        fb_log_hex32(id_reg);
        fb_log(" (not VL805)\n");
        return -1;
    }
}

#endif /* AArch64 */
