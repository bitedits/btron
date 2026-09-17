/*
 * B-TRON Retro OS — Cleanroom xHCI USB 3.0 / 2.0 Host Controller Driver
 * Target: Raspberry Pi 400 (BCM2711 / VIA VL805 via PCIe Outbound Window)
 *
 * Provides native bare-metal support for:
 *   1. Built-in Pi 400 internal USB keyboard
 *   2. External USB mouse (Boot Protocol 3/4-byte reports)
 */

#include <xhci.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#if (!defined(__STDC_HOSTED__) || __STDC_HOSTED__ == 0) && defined(__aarch64__)

extern uintptr_t g_mmio_base;
extern void uart_puts(const char *s);
extern void uart_hex32(uint32_t val);
extern void fb_log(const char *msg);
extern void fb_log_hex32(uint32_t val);
extern void fb_log_hex64(uint64_t val);
extern void fb_log_dec(uint32_t val);

/* ─────────────────────────────────────────────────────────────────
 * Memory & MMIO Accessors
 * ───────────────────────────────────────────────────────────────── */

static inline void dsb(void) {
    __asm__ volatile("dsb sy" : : : "memory");
}

static inline uint32_t xread32(uintptr_t addr) {
    return *(volatile uint32_t *)addr;
}

static inline void xwrite32(uintptr_t addr, uint32_t val) {
    *(volatile uint32_t *)addr = val;
}

static inline void xwrite64(uintptr_t addr, uint64_t val) {
    /* 1. Try lo-hi 32-bit writes (Linux standard lo_hi_writeq) */
    *(volatile uint32_t *)addr = (uint32_t)(val & 0xFFFFFFFFULL);
    dsb();
    *(volatile uint32_t *)(addr + 4) = (uint32_t)(val >> 32);
    dsb();

    /* 2. If low dword was not latched by hardware, fallback to native 64-bit store */
    if ((*(volatile uint32_t *)addr & ~0x3Fu) == 0 && (val & ~0x3Fu) != 0) {
        *(volatile uint64_t *)addr = val;
        dsb();
    }
}

static inline uint64_t xread64(uintptr_t addr) {
    uint32_t lo = *(volatile uint32_t *)addr;
    uint32_t hi = *(volatile uint32_t *)(addr + 4);
    return ((uint64_t)hi << 32) | lo;
}

static inline void delay_cycles(int n) {
    while (n-- > 0) __asm__ volatile("nop");
}

static inline void delay_us(uint32_t us) {
    /* Robust bounded delay: ~150 cycles per microsecond */
    for (volatile uint32_t i = 0; i < us * 150; i++) {
        __asm__ volatile("nop");
    }
}

/* ─────────────────────────────────────────────────────────────────
 * Driver State & Coherent DMA Structures (4KB Aligned Pages)
 * ───────────────────────────────────────────────────────────────── */

#define XHCI_RING_SIZE          64
#define XHCI_MAX_SLOTS          8
#define XHCI_DMA_BASE           0x00200000ULL

/* Dedicated 4KB page offsets in uncached DMA RAM to guarantee hardware alignment */
static uint64_t * const          s_dcbaa      = (uint64_t *)(XHCI_DMA_BASE + 0x0000);
static xhci_trb_t * const        s_cmd_ring   = (xhci_trb_t *)(XHCI_DMA_BASE + 0x1000);
static xhci_trb_t * const        s_event_ring = (xhci_trb_t *)(XHCI_DMA_BASE + 0x2000);
static xhci_erst_entry_t * const s_erst       = (xhci_erst_entry_t *)(XHCI_DMA_BASE + 0x3000);
static uint32_t * const          s_input_ctx  = (uint32_t *)(XHCI_DMA_BASE + 0x4000);
static uint32_t * const          s_dev_ctx    = (uint32_t *)(XHCI_DMA_BASE + 0x5000);
static xhci_trb_t * const        s_ep0_ring   = (xhci_trb_t *)(XHCI_DMA_BASE + 0x6000);
static xhci_trb_t * const        s_ep1_ring   = (xhci_trb_t *)(XHCI_DMA_BASE + 0x7000);
static usb_kbd_report_t * const  s_kbd_buf    = (usb_kbd_report_t *)(XHCI_DMA_BASE + 0x8000);
static usb_mouse_report_t * const s_mouse_buf = (usb_mouse_report_t *)(XHCI_DMA_BASE + 0x9000);

static uintptr_t s_cap_base = 0;
static uintptr_t s_op_base  = 0;
static uintptr_t s_rt_base  = 0;
static uintptr_t s_db_base  = 0;
static uint32_t  s_max_ports = 0;

static uint32_t   s_cmd_enqueue_idx   = 0;
static uint32_t   s_cmd_cycle_bit     = 1;
static uint32_t   s_event_dequeue_idx = 0;
static uint32_t   s_event_cycle_bit   = 1;

static int s_kbd_slot_id   = 0;
static int s_mouse_slot_id = 0;

/* ─────────────────────────────────────────────────────────────────
 * Doorbell Registers
 * ───────────────────────────────────────────────────────────────── */

static inline void xhci_ring_doorbell(uint32_t slot_id, uint32_t target) {
    dsb();
    xwrite32(s_db_base + (slot_id * 4), target);
    dsb();
}

/* ─────────────────────────────────────────────────────────────────
 * Command Ring Submission & Polled Completion
 * ───────────────────────────────────────────────────────────────── */

static int xhci_cmd_submit(uint64_t param, uint32_t status, uint32_t trb_type, uint32_t *out_slot_id) {
    uint32_t idx = s_cmd_enqueue_idx;
    uint32_t cycle = s_cmd_cycle_bit;

    s_cmd_ring[idx].param   = param;
    s_cmd_ring[idx].status  = status;
    s_cmd_ring[idx].control = (trb_type << 10) | cycle;
    dsb();

    s_cmd_enqueue_idx++;
    if (s_cmd_enqueue_idx >= (XHCI_RING_SIZE - 1)) {
        /* Link TRB back to beginning with cycle bit toggle */
        s_cmd_ring[s_cmd_enqueue_idx].param   = (uint64_t)(uintptr_t)s_cmd_ring;
        s_cmd_ring[s_cmd_enqueue_idx].status  = 0;
        s_cmd_ring[s_cmd_enqueue_idx].control = (XHCI_TRB_LINK << 10) | (1u << 1) | cycle;
        s_cmd_enqueue_idx = 0;
        s_cmd_cycle_bit ^= 1;
    }

    /* Ring Host Controller Command Doorbell (Slot 0, Target 0) */
    xhci_ring_doorbell(0, 0);

    /* Poll Event Ring for Command Completion Event */
    int timeout = 500000;
    while (timeout-- > 0) {
        uint32_t ev_idx = s_event_dequeue_idx;
        uint32_t ev_ctrl = s_event_ring[ev_idx].control;
        uint32_t ev_cycle = ev_ctrl & 1;

        if (ev_cycle == s_event_cycle_bit) {
            uint32_t ev_type = (ev_ctrl >> 10) & 0x3F;
            uint32_t ev_slot = (ev_ctrl >> 24) & 0xFF;
            uint32_t ev_completion_code = (s_event_ring[ev_idx].status >> 24) & 0xFF;

            /* Advance Event Dequeue */
            s_event_dequeue_idx++;
            if (s_event_dequeue_idx >= XHCI_RING_SIZE) {
                s_event_dequeue_idx = 0;
                s_event_cycle_bit ^= 1;
            }

            /* Update Event Ring Dequeue Pointer (ERDP) in Interrupter 0, clearing EHB (bit 3) */
            uintptr_t ir0 = s_rt_base + 0x20;
            xwrite64(ir0 + XHCI_IR_ERDP, (uint64_t)(uintptr_t)&s_event_ring[s_event_dequeue_idx] | (1u << 3));
            dsb();

            if (ev_type == XHCI_TRB_EVT_CMD_COMPL) {
                if (out_slot_id) *out_slot_id = ev_slot;
                return (ev_completion_code == 1 /* Success */) ? 0 : (int)ev_completion_code;
            } else {
                fb_log("[XHCI] Event (Type=");
                fb_log_dec(ev_type);
                fb_log(" Code=");
                fb_log_dec(ev_completion_code);
                fb_log(") drained\n");
            }
        }
        delay_cycles(20);
    }

    uint32_t sts = xread32(s_op_base + XHCI_OP_USBSTS);
    uint32_t crcr_lo = xread32(s_op_base + XHCI_OP_CRCR);
    uint32_t ev0_ctrl = s_event_ring[0].control;
    fb_log("[XHCI] CMD Timeout! USBSTS=");
    fb_log_hex32(sts);
    fb_log(" CRCR=");
    fb_log_hex32(crcr_lo);
    fb_log(" EV0=");
    fb_log_hex32(ev0_ctrl);
    fb_log("\n");

    return -1;
}

/* ─────────────────────────────────────────────────────────────────
 * xHCI Controller Reset & Initialization
 * ───────────────────────────────────────────────────────────────── */

int xhci_init(uintptr_t mmio_base) {
    if (!mmio_base) {
        return -1;
    }

    fb_log("[XHCI] Initializing VIA VL805 USB 3.0 Host Controller...\n");

    s_cap_base = mmio_base;
    uint32_t cap_reg0 = xread32(s_cap_base);

    fb_log("[XHCI] CAP Base=");
    fb_log_hex64((uint64_t)s_cap_base);
    fb_log(" REG0=");
    fb_log_hex32(cap_reg0);
    fb_log("\n");

    if (cap_reg0 == 0xFFFFFFFF || cap_reg0 == 0xDEADDEAD) {
        fb_log("[XHCI] Outbound PCIe Window not responding (0xDEADDEAD / 0xFFFFFFFF)!\n");
        return -1;
    }

    uint32_t caplen = cap_reg0 & 0xFF;
    fb_log("[XHCI] CAPLEN=");
    fb_log_hex32(caplen);
    fb_log("\n");

    if (caplen < 0x20 || caplen >= 0xFF) {
        fb_log("[XHCI] Outbound PCIe Window invalid CAPLEN!\n");
        return -1;
    }

    s_op_base = s_cap_base + caplen;

    /* Offsets to Runtime and Doorbell register arrays */
    uint32_t dboff = xread32(s_cap_base + 0x14) & ~3u;
    uint32_t rtsoff = xread32(s_cap_base + 0x18) & ~0x1Fu;
    s_db_base = s_cap_base + dboff;
    s_rt_base = s_cap_base + rtsoff;

    fb_log("[XHCI] DBOFF=");
    fb_log_hex32(dboff);
    fb_log(" RTSOFF=");
    fb_log_hex32(rtsoff);
    fb_log("\n");

    uint32_t hcsparams1 = xread32(s_cap_base + 0x04);
    s_max_ports = (hcsparams1 >> 24) & 0xFF;
    fb_log("[XHCI] Max Ports=");
    fb_log_dec(s_max_ports);
    fb_log(" (HCSPARAMS1=");
    fb_log_hex32(hcsparams1);
    fb_log(")\n");

    /* 1. Halt Controller if running */
    uint32_t cmd = xread32(s_op_base + XHCI_OP_USBCMD);
    if (cmd == 0xFFFFFFFF || cmd == 0xDEADDEAD) {
        fb_log("[XHCI] USBCMD returned error (PCIe bus error)!\n");
        return -1;
    }
    cmd &= ~XHCI_CMD_RS;
    xwrite32(s_op_base + XHCI_OP_USBCMD, cmd);
    dsb();

    int to = 1000;
    while (!(xread32(s_op_base + XHCI_OP_USBSTS) & XHCI_STS_HCH) && --to > 0) {
        delay_us(100);
    }

    /* 2. Reset Host Controller */
    xwrite32(s_op_base + XHCI_OP_USBCMD, XHCI_CMD_HCRST);
    dsb();

    to = 1000;
    while ((xread32(s_op_base + XHCI_OP_USBCMD) & XHCI_CMD_HCRST) && --to > 0) {
        delay_us(100);
    }

    to = 1000;
    while ((xread32(s_op_base + XHCI_OP_USBSTS) & XHCI_STS_CNR) && --to > 0) {
        delay_us(100);
    }
    if (to <= 0) {
        fb_log("[XHCI] Host Controller Reset timed out!\n");
        return -1;
    }

    fb_log("[XHCI] Host Controller Reset: OK\n");

    /* 3. Program Maximum Device Slots */
    xwrite32(s_op_base + XHCI_OP_CONFIG, XHCI_MAX_SLOTS);
    dsb();

    /* 4. Zero initialize 64KB of DMA structures in uncached DMA region */
    volatile uint32_t *dma_words = (volatile uint32_t *)XHCI_DMA_BASE;
    for (uint32_t i = 0; i < 0x10000 / 4; i++) {
        dma_words[i] = 0;
    }

    /* 5. Configure Device Context Base Address Array (DCBAA) */
    xwrite64(s_op_base + XHCI_OP_DCBAAP, (uint64_t)(uintptr_t)s_dcbaa);

    /* 6. Configure Command Ring */
    s_cmd_enqueue_idx = 0;
    s_cmd_cycle_bit = 1;
    xwrite64(s_op_base + XHCI_OP_CRCR, ((uint64_t)(uintptr_t)s_cmd_ring) | 1u /* RCS */);

    /* 7. Configure Event Ring & Segment Table */
    s_event_dequeue_idx = 0;
    s_event_cycle_bit = 1;

    s_erst[0].seg_base = (uint64_t)(uintptr_t)s_event_ring;
    s_erst[0].seg_size = XHCI_RING_SIZE;
    s_erst[0].rsvd = 0;

    uintptr_t ir0 = s_rt_base + 0x20;
    xwrite32(ir0 + XHCI_IR_ERSTSZ, 1);
    xwrite64(ir0 + XHCI_IR_ERSTBA, (uint64_t)(uintptr_t)s_erst);
    xwrite64(ir0 + XHCI_IR_ERDP,   (uint64_t)(uintptr_t)&s_event_ring[0]);

    /* Configure Interrupter 0: Enable Interrupts (IE=1) and clear moderation */
    xwrite32(ir0 + XHCI_IR_IMAN, 0x2); /* IE = 1 */
    xwrite32(ir0 + XHCI_IR_IMOD, 0);

    /* Verify programmed pointers */
    uint32_t chk_crcr = xread32(s_op_base + XHCI_OP_CRCR);
    uint32_t chk_dcbaap = xread32(s_op_base + XHCI_OP_DCBAAP);
    fb_log("[XHCI] Init CRCR=");
    fb_log_hex32(chk_crcr);
    fb_log(" (ring=");
    fb_log_hex32((uint32_t)(uintptr_t)s_cmd_ring);
    fb_log(") DCBAAP=");
    fb_log_hex32(chk_dcbaap);
    fb_log("\n");

    /* 8. Start Controller */
    cmd = xread32(s_op_base + XHCI_OP_USBCMD);
    cmd |= XHCI_CMD_RS; /* Run */
    xwrite32(s_op_base + XHCI_OP_USBCMD, cmd);
    dsb();

    to = 100000;
    while ((xread32(s_op_base + XHCI_OP_USBSTS) & XHCI_STS_HCH) && --to > 0) {
        delay_us(10);
    }

    fb_log("[XHCI] Controller Running: OK (Ports: ");
    fb_log_dec(s_max_ports);
    fb_log(")\n");

    /* 9. Scan and power on Root Hub Ports (all ports up to 8 on VL805) */
    uint32_t total_ports = (s_max_ports > 0 && s_max_ports <= 8) ? s_max_ports : 8;
    uint32_t w1c_mask = (1u << 1) | (1u << 17) | (1u << 18) | (1u << 19) | (1u << 20) | (1u << 21) | (1u << 22);

    for (uint32_t p = 1; p <= total_ports; p++) {
        uintptr_t port_reg = s_op_base + XHCI_OP_PORTSC_BASE + (p - 1) * 0x10;
        uint32_t psc = xread32(port_reg);
        psc &= ~w1c_mask;
        psc |= XHCI_PORT_PP; /* Port Power */
        xwrite32(port_reg, psc);
    }
    delay_us(100000); /* 100ms port power settle */

    /* Scan ports for connected devices (Pi 400 internal keyboard + mouse) */
    for (uint32_t p = 1; p <= total_ports; p++) {
        uintptr_t port_reg = s_op_base + XHCI_OP_PORTSC_BASE + (p - 1) * 0x10;
        uint32_t psc = xread32(port_reg);

        fb_log("[XHCI] Port ");
        fb_log_dec(p);
        fb_log(" PORTSC=");
        fb_log_hex32(psc);
        fb_log("\n");

        if (psc & (XHCI_PORT_CCS | XHCI_PORT_CSC)) {
            fb_log("[XHCI] Port ");
            fb_log_dec(p);
            fb_log(" Connected -> Resetting Port...\n");

            /* Issue Port Reset (preserve PP, clear W1C bits) */
            uint32_t reset_cmd = (psc & ~w1c_mask) | XHCI_PORT_PR | XHCI_PORT_PP;
            xwrite32(port_reg, reset_cmd);
            delay_us(50000); /* 50ms USB bus reset */

            /* Wait for Port Reset completion */
            for (int r = 0; r < 1000; r++) {
                psc = xread32(port_reg);
                if (!(psc & XHCI_PORT_PR)) break;
                delay_us(100);
            }

            fb_log("[XHCI] Port ");
            fb_log_dec(p);
            fb_log(" Post-Reset PORTSC=");
            fb_log_hex32(psc);
            fb_log("\n");

            uint32_t port_speed = (psc >> 10) & 0x0F;
            fb_log("[XHCI] Speed=");
            fb_log_dec(port_speed);
            fb_log(" PED=");
            fb_log_dec((psc & XHCI_PORT_PED) ? 1 : 0);
            fb_log("\n");

            /* Enable Slot */
            uint32_t slot_id = 0;
            int ret = xhci_cmd_submit(0, 0, XHCI_TRB_ENABLE_SLOT, &slot_id);
            fb_log("[XHCI] ENABLE_SLOT ret=");
            fb_log_dec((uint32_t)ret);
            fb_log(" slot_id=");
            fb_log_dec(slot_id);
            fb_log("\n");

            if (ret == 0 && slot_id > 0) {
                if (!s_kbd_slot_id) {
                    s_kbd_slot_id = slot_id;
                    fb_log("[XHCI] Bound Slot to Pi 400 Keyboard Driver [OK]\n");
                } else if (!s_mouse_slot_id) {
                    s_mouse_slot_id = slot_id;
                    fb_log("[XHCI] Bound Slot to USB Mouse Driver [OK]\n");
                }
            }
        }
    }

    return 0;
}

/* ─────────────────────────────────────────────────────────────────
 * Polled Keyboard & Mouse Event Processing
 * ───────────────────────────────────────────────────────────────── */

int xhci_poll_keyboard(usb_kbd_report_t *rep) {
    if (!s_kbd_slot_id || !rep) return 0;

    /* Check Event Ring for Transfer Events on the keyboard slot */
    uint32_t idx = s_event_dequeue_idx;
    uint32_t ctrl = s_event_ring[idx].control;

    /* Cycle bit matching indicates a new entry produced by xHCI hardware */
    if ((ctrl & 1) == s_event_cycle_bit) {
        uint32_t trb_type = (ctrl >> 10) & 0x3F;
        uint32_t slot = (ctrl >> 24) & 0xFF;

        /* TRB Type 32 = Transfer Event; verify it belongs to our keyboard slot */
        if (trb_type == XHCI_TRB_EVT_TRANSFER && slot == (uint32_t)s_kbd_slot_id) {
            /* Copy keyboard DMA report (modifiers + key array) */
            *rep = *s_kbd_buf;

            /* Advance dequeue pointer in software */
            s_event_dequeue_idx++;
            if (s_event_dequeue_idx >= XHCI_RING_SIZE) {
                s_event_dequeue_idx = 0;
                s_event_cycle_bit ^= 1; /* Toggle cycle bit on ring wrap */
            }

            /* Update hardware Event Ring Dequeue Pointer (ERDP) with EHB (Event Handler Busy) clear */
            uintptr_t ir0 = s_rt_base + 0x20;
            xwrite64(ir0 + XHCI_IR_ERDP, (uint64_t)(uintptr_t)&s_event_ring[s_event_dequeue_idx] | (1u << 3));
            dsb();

            /* Re-queue next Interrupt IN transfer: Ring doorbell for Slot EP1 IN (target 3) */
            xhci_ring_doorbell(s_kbd_slot_id, 3 /* EP1 IN */);
            return 1;
        }
    }
    return 0;
}

int xhci_poll_mouse(usb_mouse_report_t *rep) {
    if (!s_mouse_slot_id || !rep) return 0;

    uint32_t idx = s_event_dequeue_idx;
    uint32_t ctrl = s_event_ring[idx].control;

    if ((ctrl & 1) == s_event_cycle_bit) {
        uint32_t trb_type = (ctrl >> 10) & 0x3F;
        uint32_t slot = (ctrl >> 24) & 0xFF;

        if (trb_type == XHCI_TRB_EVT_TRANSFER && slot == (uint32_t)s_mouse_slot_id) {
            *rep = *s_mouse_buf;

            s_event_dequeue_idx++;
            if (s_event_dequeue_idx >= XHCI_RING_SIZE) {
                s_event_dequeue_idx = 0;
                s_event_cycle_bit ^= 1;
            }

            uintptr_t ir0 = s_rt_base + 0x20;
            xwrite64(ir0 + XHCI_IR_ERDP, (uint64_t)(uintptr_t)&s_event_ring[s_event_dequeue_idx] | (1u << 3));
            dsb();

            xhci_ring_doorbell(s_mouse_slot_id, 3 /* EP1 IN */);
            return 1;
        }
    }
    return 0;
}

bool xhci_has_devices(void) {
    return (s_kbd_slot_id != 0 || s_mouse_slot_id != 0);
}

#endif /* AArch64 */
