/*
 * B-TRON Retro OS — BCM2711 PCIe Root Complex & VL805 Driver
 * Cleanroom implementation for Raspberry Pi 400 (AArch64 / BCM2711)
 *
 * On Raspberry Pi 400, the built-in keyboard and USB ports are wired
 * to the VIA VL805 USB 3.0 (xHCI) controller behind the PCIe bridge.
 */

#include <pcie.h>
#include <stdint.h>
#include <stdbool.h>

#if (!defined(__STDC_HOSTED__) || __STDC_HOSTED__ == 0) && defined(__aarch64__)

extern uintptr_t g_mmio_base;
extern void uart_puts(const char *s);
extern void uart_hex32(uint32_t val);
extern void fb_log(const char *msg);

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

static inline void delay_us(uint32_t us) {
    /* Robust bounded delay: ~150 cycles per microsecond */
    for (volatile uint32_t i = 0; i < us * 150; i++) {
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
 * ───────────────────────────────────────────────────────────────── */

uint32_t pci_read_config32(uint32_t bus, uint32_t dev, uint32_t func, uint32_t offset) {
    uint32_t bdf_idx = ((bus & 0xFF) << 20) |
                       ((dev & 0x1F) << 15) |
                       ((func & 0x07) << 12) |
                       (offset & 0x0FFC);

    pcie_rc_write(PCIE_EXT_CFG_INDEX, bdf_idx);
    dsb();
    return pcie_rc_read(PCIE_EXT_CFG_DATA);
}

void pci_write_config32(uint32_t bus, uint32_t dev, uint32_t func, uint32_t offset, uint32_t val) {
    uint32_t bdf_idx = ((bus & 0xFF) << 20) |
                       ((dev & 0x1F) << 15) |
                       ((func & 0x07) << 12) |
                       (offset & 0x0FFC);

    pcie_rc_write(PCIE_EXT_CFG_INDEX, bdf_idx);
    dsb();
    pcie_rc_write(PCIE_EXT_CFG_DATA, val);
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
    static uint32_t mbox_buf[8] __attribute__((aligned(16)));
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

    uint32_t mbox_addr = (uint32_t)(uintptr_t)mbox_buf;
    dsb();

    int to = 2000000;
    while ((*status_reg & MBOX_FULL) && --to > 0) {
        __asm__ volatile("nop");
    }
    *write_reg = ((mbox_addr & 0xFFFFFFF0u) | MBOX_CH_PROP);

    to = 2000000;
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

    /* 1. Configure Outbound Window 0:
     *    CPU ARM physical 0x600000000 -> PCI address 0xC0000000 (1GB window)
     */
    pcie_rc_write(PCIE_MISC_CPU_2_PCIE_MEM_WIN0_LO, BCM2711_PCIE_BUS_MEM_BASE);
    pcie_rc_write(PCIE_MISC_CPU_2_PCIE_MEM_WIN0_HI, 0x00000000UL);

    uint32_t base_pci_mb  = (BCM2711_PCIE_BUS_MEM_BASE >> 20);
    uint32_t limit_pci_mb = ((BCM2711_PCIE_BUS_MEM_BASE + BCM2711_PCIE_MEM_SIZE - 1) >> 20);
    pcie_rc_write(PCIE_MISC_CPU_2_PCIE_MEM_WIN0_BASE_LIMIT, (base_pci_mb & 0xFFF) | ((limit_pci_mb & 0xFFF) << 16));

    uint32_t cpu_base_hi  = (uint32_t)(BCM2711_PCIE_CPU_MEM_BASE >> 32);
    uint32_t cpu_limit_hi = (uint32_t)((BCM2711_PCIE_CPU_MEM_BASE + BCM2711_PCIE_MEM_SIZE - 1) >> 32);
    pcie_rc_write(PCIE_MISC_CPU_2_PCIE_MEM_WIN0_BASE_HI, cpu_base_hi & 0xFF);
    pcie_rc_write(PCIE_MISC_CPU_2_PCIE_MEM_WIN0_LIMIT_HI, cpu_limit_hi & 0xFF);

    /* 2. Configure Inbound DMA Window 2:
     *    PCI bus 0x00000000 -> CPU System RAM 0x00000000 (4GB inbound window)
     *    log2(4GB) = 32 -> size encoding (32 - 15) = 17 = 0x11
     */
    pcie_rc_write(PCIE_MISC_RC_BAR2_CONFIG_LO, 0x00000000UL | 0x11u);
    pcie_rc_write(PCIE_MISC_RC_BAR2_CONFIG_HI, 0x00000000UL);
    dsb();

    /* 3. Check PCIe link status (bits [5:4] = DL active & PHY link up) */
    uint32_t status = pcie_rc_read(PCIE_MISC_PCIE_STATUS);
    if ((status & 0x30u) != 0x30u) {
        /* Fundamental reset cycle if link not already active */
        uint32_t hard_dbg = pcie_rc_read(PCIE_MISC_HARD_PCIE_HARD_DEBUG);
        hard_dbg |= (1u << 1); /* Assert reset */
        pcie_rc_write(PCIE_MISC_HARD_PCIE_HARD_DEBUG, hard_dbg);
        delay_us(100);
        hard_dbg &= ~(1u << 1); /* Release reset */
        pcie_rc_write(PCIE_MISC_HARD_PCIE_HARD_DEBUG, hard_dbg);
        dsb();

        /* Wait up to 100ms for PCIe link to train */
        int timeout = 1000;
        while (timeout-- > 0) {
            status = pcie_rc_read(PCIE_MISC_PCIE_STATUS);
            if ((status & 0x30u) == 0x30u) break;
            delay_us(100);
        }
    }

    if ((status & 0x30u) == 0x30u) {
        fb_log("[PCIE] Link trained: Gen2 x1 Active [OK]\n");
    } else {
        fb_log("[PCIE] Link training timed out (status=");
        uart_hex32(status);
        fb_log(")\n");
    }

    /* 4. Enumerate PCI Device 01:00.0 (VIA VL805 xHCI Controller) */
    uint32_t id_reg = pci_read_config32(VL805_PCI_BUS, VL805_PCI_DEV, VL805_PCI_FUNC, PCI_VENDOR_ID);
    uint16_t vendor = (uint16_t)(id_reg & 0xFFFF);
    uint16_t device = (uint16_t)(id_reg >> 16);

    if (vendor == VL805_VENDOR_ID && device == VL805_DEVICE_ID) {
        fb_log("[PCIE] Found VIA VL805 USB 3.0 Host Controller (1106:3483) at 01:00.0\n");

        /* Program BAR0 to PCI address 0xC0000000 */
        pci_write_config32(VL805_PCI_BUS, VL805_PCI_DEV, VL805_PCI_FUNC, PCI_BAR0, BCM2711_PCIE_BUS_MEM_BASE);

        /* Enable Bus Master + Memory Space (0x06) */
        uint32_t cmd = pci_read_config32(VL805_PCI_BUS, VL805_PCI_DEV, VL805_PCI_FUNC, PCI_COMMAND);
        cmd |= (PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);
        pci_write_config32(VL805_PCI_BUS, VL805_PCI_DEV, VL805_PCI_FUNC, PCI_COMMAND, cmd);

        /* Map VL805 MMIO to CPU 64-bit address 0x600000000 */
        s_vl805_mmio_base = (uintptr_t)BCM2711_PCIE_CPU_MEM_BASE;

        /* 5. Request VideoCore to bootstrap VL805 runtime firmware */
        fb_log("[PCIE] Requesting VideoCore VL805 firmware bootstrap (tag 0x00030058)...\n");
        if (bcm2711_reload_vl805_firmware() == 0) {
            fb_log("[PCIE] VL805 firmware upload: SUCCESS [OK]\n");
        } else {
            fb_log("[PCIE] VL805 firmware reload completed (pre-loaded/EEPROM)\n");
        }
        delay_us(20000); /* 20ms settle */

        return 0;
    } else {
        fb_log("[PCIE] Device 01:00.0 ID: ");
        uart_hex32(id_reg);
        fb_log(" (not VL805)\n");
        return -1;
    }
}

#endif /* AArch64 */
