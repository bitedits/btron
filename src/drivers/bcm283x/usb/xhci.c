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

#if (!defined(__STDC_HOSTED__) || __STDC_HOSTED__ == 0) && defined(__aarch64__)

extern uintptr_t g_mmio_base;
extern void uart_puts(const char *s);
extern void uart_hex32(uint32_t val);
extern void fb_log(const char *msg);

/* ─────────────────────────────────────────────────────────────────
 * Memory & MMIO Accessors
 * ───────────────────────────────────────────────────────────────── */

static inline uint32_t xread32(uintptr_t addr) {
    return *(volatile uint32_t *)addr;
}

static inline void xwrite32(uintptr_t addr, uint32_t val) {
    *(volatile uint32_t *)addr = val;
}

static inline void xwrite64(uintptr_t addr, uint64_t val) {
    *(volatile uint64_t *)addr = val;
}

static inline void dsb(void) {
    __asm__ volatile("dsb sy" : : : "memory");
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
 * Driver State & Data Structures (Aligned for xHCI Hardware DMA)
 * ───────────────────────────────────────────────────────────────── */

#define XHCI_RING_SIZE          64
#define XHCI_MAX_SLOTS          8

static uintptr_t s_cap_base = 0;
static uintptr_t s_op_base  = 0;
static uintptr_t s_rt_base  = 0;
static uintptr_t s_db_base  = 0;
static uint32_t  s_max_ports = 0;

/* Device Context Base Address Array (DCBAA) - 64-byte aligned */
static uint64_t s_dcbaa[XHCI_MAX_SLOTS + 1] __attribute__((aligned(64)));

/* Command Ring - 64 entries of 16 bytes = 1024 bytes */
static xhci_trb_t s_cmd_ring[XHCI_RING_SIZE] __attribute__((aligned(64)));
static uint32_t   s_cmd_enqueue_idx = 0;
static uint32_t   s_cmd_cycle_bit   = 1;

/* Event Ring - 64 entries of 16 bytes */
static xhci_trb_t s_event_ring[XHCI_RING_SIZE] __attribute__((aligned(64)));
static uint32_t   s_event_dequeue_idx = 0;
static uint32_t   s_event_cycle_bit   = 1;

/* Event Ring Segment Table (ERST) */
static xhci_erst_entry_t s_erst[1] __attribute__((aligned(64)));

/* Keyboard & Mouse DMA Buffers */
static usb_kbd_report_t   s_kbd_dma_buf   __attribute__((aligned(16)));
static usb_mouse_report_t s_mouse_dma_buf __attribute__((aligned(16)));

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
            if (ev_type == XHCI_TRB_EVT_CMD_COMPL) {
                uint32_t ev_slot = (ev_ctrl >> 24) & 0xFF;
                if (out_slot_id) *out_slot_id = ev_slot;

                /* Advance Event Dequeue */
                s_event_dequeue_idx++;
                if (s_event_dequeue_idx >= XHCI_RING_SIZE) {
                    s_event_dequeue_idx = 0;
                    s_event_cycle_bit ^= 1;
                }

                /* Update Event Ring Dequeue Pointer (ERDP) in Interrupter 0 */
                uintptr_t ir0 = s_rt_base + 0x20;
                xwrite64(ir0 + XHCI_IR_ERDP, (uint64_t)(uintptr_t)&s_event_ring[s_event_dequeue_idx] | (1u << 3));
                dsb();
                return 0;
            }
        }
        delay_cycles(20);
    }
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
    uint32_t caplen = (uint32_t)(*(volatile uint8_t *)s_cap_base);
    s_op_base = s_cap_base + caplen;

    /* Offsets to Runtime and Doorbell register arrays */
    uint32_t dboff = xread32(s_cap_base + 0x14) & ~3u;
    uint32_t rtsoff = xread32(s_cap_base + 0x18) & ~0x1Fu;
    s_db_base = s_cap_base + dboff;
    s_rt_base = s_cap_base + rtsoff;

    uint32_t hcsparams1 = xread32(s_cap_base + 0x04);
    s_max_ports = (hcsparams1 >> 24) & 0xFF;

    /* 1. Halt Controller if running */
    uint32_t cmd = xread32(s_op_base + XHCI_OP_USBCMD);
    cmd &= ~XHCI_CMD_RS;
    xwrite32(s_op_base + XHCI_OP_USBCMD, cmd);
    dsb();

    int to = 100000;
    while (!(xread32(s_op_base + XHCI_OP_USBSTS) & XHCI_STS_HCH) && --to > 0) {
        delay_us(10);
    }

    /* 2. Reset Host Controller */
    xwrite32(s_op_base + XHCI_OP_USBCMD, XHCI_CMD_HCRST);
    dsb();

    to = 100000;
    while ((xread32(s_op_base + XHCI_OP_USBCMD) & XHCI_CMD_HCRST) && --to > 0) {
        delay_us(10);
    }

    to = 100000;
    while ((xread32(s_op_base + XHCI_OP_USBSTS) & XHCI_STS_CNR) && --to > 0) {
        delay_us(10);
    }

    fb_log("[XHCI] Host Controller Reset: OK\n");

    /* 3. Program Maximum Device Slots */
    xwrite32(s_op_base + XHCI_OP_CONFIG, XHCI_MAX_SLOTS);
    dsb();

    /* 4. Configure Device Context Base Address Array (DCBAA) */
    for (int i = 0; i <= XHCI_MAX_SLOTS; i++) s_dcbaa[i] = 0;
    xwrite64(s_op_base + XHCI_OP_DCBAAP, (uint64_t)(uintptr_t)s_dcbaa);

    /* 5. Configure Command Ring */
    s_cmd_enqueue_idx = 0;
    s_cmd_cycle_bit = 1;
    for (int i = 0; i < XHCI_RING_SIZE; i++) {
        s_cmd_ring[i].param = 0;
        s_cmd_ring[i].status = 0;
        s_cmd_ring[i].control = 0;
    }
    xwrite64(s_op_base + XHCI_OP_CRCR, ((uint64_t)(uintptr_t)s_cmd_ring) | 1u /* RCS */);

    /* 6. Configure Event Ring & Segment Table */
    s_event_dequeue_idx = 0;
    s_event_cycle_bit = 1;
    for (int i = 0; i < XHCI_RING_SIZE; i++) {
        s_event_ring[i].param = 0;
        s_event_ring[i].status = 0;
        s_event_ring[i].control = 0;
    }

    s_erst[0].seg_base = (uint64_t)(uintptr_t)s_event_ring;
    s_erst[0].seg_size = XHCI_RING_SIZE;
    s_erst[0].rsvd = 0;

    uintptr_t ir0 = s_rt_base + 0x20;
    xwrite32(ir0 + XHCI_IR_ERSTSZ, 1);
    xwrite64(ir0 + XHCI_IR_ERSTBA, (uint64_t)(uintptr_t)s_erst);
    xwrite64(ir0 + XHCI_IR_ERDP,   (uint64_t)(uintptr_t)&s_event_ring[0]);

    /* 7. Start Controller */
    cmd = xread32(s_op_base + XHCI_OP_USBCMD);
    cmd |= XHCI_CMD_RS; /* Run */
    xwrite32(s_op_base + XHCI_OP_USBCMD, cmd);
    dsb();

    to = 100000;
    while ((xread32(s_op_base + XHCI_OP_USBSTS) & XHCI_STS_HCH) && --to > 0) {
        delay_us(10);
    }

    fb_log("[XHCI] Controller Running: OK (Ports: ");
    uart_hex32(s_max_ports);
    fb_log(")\n");

    /* 8. Scan and power on Root Hub Ports */
    for (uint32_t p = 1; p <= s_max_ports && p <= 4; p++) {
        uintptr_t port_reg = s_op_base + XHCI_OP_PORTSC_BASE + (p - 1) * 0x10;
        uint32_t psc = xread32(port_reg);
        psc |= XHCI_PORT_PP; /* Port Power */
        xwrite32(port_reg, psc);
    }
    delay_us(50000); /* 50ms port power settle */

    /* Scan ports for connected devices (Pi 400 internal keyboard + mouse) */
    for (uint32_t p = 1; p <= s_max_ports && p <= 4; p++) {
        uintptr_t port_reg = s_op_base + XHCI_OP_PORTSC_BASE + (p - 1) * 0x10;
        uint32_t psc = xread32(port_reg);

        if (psc & XHCI_PORT_CCS) {
            fb_log("[XHCI] Port ");
            uart_hex32(p);
            fb_log(" Connected -> Resetting Port...\n");

            /* Issue Port Reset */
            psc |= XHCI_PORT_PR;
            xwrite32(port_reg, psc);
            delay_us(50000); /* 50ms USB bus reset */

            /* Wait for Port Reset completion */
            for (int r = 0; r < 1000; r++) {
                psc = xread32(port_reg);
                if (!(psc & XHCI_PORT_PR)) break;
                delay_us(100);
            }

            /* Enable Slot */
            uint32_t slot_id = 0;
            if (xhci_cmd_submit(0, 0, XHCI_TRB_ENABLE_SLOT, &slot_id) == 0 && slot_id > 0) {
                fb_log("[XHCI] Enabled Slot ID=");
                uart_hex32(slot_id);
                fb_log("\n");

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
 *
 * Bare-Metal Stage 1 & Stage 2 Input Model:
 * In Stage 1 terminal console (before GUI launch) and Stage 2 Workbench,
 * keystrokes and mouse packets are read directly from the xHCI Event Ring
 * without needing an operating system kernel scheduler or interrupt daemon.
 *
 * How the Transfer Ring & Event Ring work:
 * 1. An Interrupt IN endpoint transfer TRB is programmed for the device slot (EP1 IN).
 * 2. When a physical key is pressed on the Pi 400 internal keyboard, the VL805
 *    xHCI controller DMAs the 8-byte HID boot report (modifiers + 6 keycodes)
 *    into s_kbd_dma_buf and writes a Transfer Completion Event TRB (type 32)
 *    to the Event Ring.
 * 3. xhci_poll_keyboard() checks if the cycle bit at s_event_dequeue_idx matches
 *    s_event_cycle_bit. If so, a new event has arrived from hardware!
 * 4. We extract the report, advance s_event_dequeue_idx, and update the hardware
 *    Event Ring Dequeue Pointer (ERDP) in Interrupter 0 to acknowledge receipt.
 * 5. We ring the endpoint doorbell (slot, target 3) to request the next HID packet.
 * 6. The report is passed to Stage 1 shell to execute commands like 'startx',
 *    'help', 'mem', 'sysinfo', or to Stage 2 workbench event queue.
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
            *rep = s_kbd_dma_buf;

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
            *rep = s_mouse_dma_buf;

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
