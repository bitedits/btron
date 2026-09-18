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

static inline __attribute__((unused)) uint64_t xread64(uintptr_t addr) {
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
#define XHCI_DMA_BASE           0x01000000ULL /* 16MB uncached DMA region (L2 Entry 8) */

/* Base data structures in uncached DMA RAM (16MB region) */
#define XHCI_DMA_BASE           0x01000000ULL /* 16MB uncached DMA region */

static uint64_t * const                   s_dcbaa      = (uint64_t *)(XHCI_DMA_BASE + 0x0000); /* 1KB (32 slots * 8B) */
static volatile xhci_trb_t * const        s_cmd_ring   = (volatile xhci_trb_t *)(XHCI_DMA_BASE + 0x0800); /* 1KB */
static volatile xhci_trb_t * const        s_event_ring = (volatile xhci_trb_t *)(XHCI_DMA_BASE + 0x0C00); /* 1KB */
static xhci_erst_entry_t * const          s_erst       = (xhci_erst_entry_t *)(XHCI_DMA_BASE + 0x1000); /* 64B */
static uint32_t * const                   s_input_ctx  = (uint32_t *)(XHCI_DMA_BASE + 0x1400); /* 2KB */

/* Per-slot structures (Slots 1..8):
 * Each Device Context: 2KB (32 contexts * 64B)
 * Each EP Ring: 1KB (64 TRBs * 16B)
 */
#define DEV_CTX_BASE(slot)   ((volatile uint32_t *)(XHCI_DMA_BASE + 0x2000 + ((slot) - 1) * 0x800))
#define EP0_RING_BASE(slot)  ((volatile xhci_trb_t *)(XHCI_DMA_BASE + 0x6000 + ((slot) - 1) * 0x400))
#define EP1_RING_BASE(slot)  ((volatile xhci_trb_t *)(XHCI_DMA_BASE + 0x8000 + ((slot) - 1) * 0x400))

#define DMA_SCRATCH_BUF      ((volatile uint8_t *)(XHCI_DMA_BASE + 0xB000)) /* 4KB scratch buffer */
static volatile usb_kbd_report_t * const  s_kbd_buf    = (volatile usb_kbd_report_t *)(XHCI_DMA_BASE + 0xC000);

/* Multi-mouse DMA buffers (up to 4 mice): each mouse gets 64 bytes */
#define MOUSE_BUF(idx)       ((volatile uint8_t *)(XHCI_DMA_BASE + 0xC100 + (idx) * 64))

static uintptr_t s_cap_base = 0;
static uintptr_t s_op_base  = 0;
static uintptr_t s_rt_base  = 0;
static uintptr_t s_db_base  = 0;
static uint32_t  s_max_ports = 0;

static uint32_t   s_cmd_enqueue_idx   = 0;
static uint32_t   s_cmd_cycle_bit     = 1;
static uint32_t   s_event_dequeue_idx = 0;
static uint32_t   s_event_cycle_bit   = 1;

#define XHCI_MAX_SLOTS 8
static uint32_t   s_ep0_enqueue_idx[XHCI_MAX_SLOTS + 1] = {0};
static uint32_t   s_ep0_cycle[XHCI_MAX_SLOTS + 1]       = {1, 1, 1, 1, 1, 1, 1, 1, 1};
static uint32_t   s_ep1_enqueue_idx[XHCI_MAX_SLOTS + 1] = {0};
static uint32_t   s_ep1_cycle[XHCI_MAX_SLOTS + 1]       = {1, 1, 1, 1, 1, 1, 1, 1, 1};

static int s_kbd_slot_id   = 0;
static uint32_t s_kbd_mps   = 8;

#define XHCI_MAX_MICE 4
typedef struct {
    int               slot_id;
    uint32_t          mps;
    volatile uint8_t *buf;
    uint8_t           proto_mode; /* 0 = auto-detect, 1 = Boot Protocol (8-bit), 2 = Report ID 1 (16-bit) */
} xhci_mouse_t;

static xhci_mouse_t s_mice[XHCI_MAX_MICE];
static int          s_num_mice = 0;

#define XHCI_KBD_QUEUE_SIZE 32
static usb_kbd_report_t s_kbd_queue[XHCI_KBD_QUEUE_SIZE];
static volatile int s_kbd_q_head = 0;
static volatile int s_kbd_q_tail = 0;
static volatile int s_kbd_q_count = 0;

static volatile int32_t s_accum_dx = 0;
static volatile int32_t s_accum_dy = 0;
static volatile int32_t s_accum_wheel = 0;
static volatile uint8_t s_latest_buttons = 0;
static volatile int s_has_mouse = 0;

static void xhci_decode_mouse_report(xhci_mouse_t *mouse, uint32_t transferred, usb_mouse_report_t *out) {
    if (!mouse || !mouse->buf || !out) return;
    const volatile uint8_t *raw = mouse->buf;

    /* Auto-detect protocol mode if not explicitly locked:
     * - Standard Boot Protocol mouse reports NEVER have a Report ID. When no button
     *   is pressed (the vast majority of cursor moves), raw[0] == 0x00.
     *   A device sending raw[0] == 0x00 can never be a Report ID 1 device.
     * - Gaming mice with Report ID 1 (e.g. Logitech G102/G203 LIGHTSYNC) prepend
     *   Report ID 0x01 on EVERY packet, so raw[0] == 0x01 always. When buttons are
     *   released, raw[1] == 0x00 and motion is in bytes 2..5 (16-bit). */
    if (mouse->proto_mode == 0) {
        if (raw[0] == 0x00) {
            mouse->proto_mode = 1; /* Standard Boot Protocol */
        } else if (raw[0] == 0x01 && transferred >= 6 && raw[1] == 0x00 &&
                   (raw[2] != 0 || raw[3] != 0 || raw[4] != 0 || raw[5] != 0)) {
            mouse->proto_mode = 2; /* Report ID 1 (16-bit deltas) */
        }
    }

    if (mouse->proto_mode == 2) {
        /* Gaming / Multi-Report HID Mouse with Report ID 1 (e.g. Logitech G102/G203 LIGHTSYNC)
         * Byte 0: Report ID (0x01)
         * Byte 1: Buttons (bit 0=Left, bit 1=Right, bit 2=Middle, bit 3=Back, bit 4=Forward)
         * Byte 2..3: X displacement (int16_t little-endian)
         * Byte 4..5: Y displacement (int16_t little-endian)
         * Byte 6: Wheel (optional)
         */
        if (raw[0] == 0x01 && transferred >= 6) {
            out->buttons = raw[1] & 0x07;

            int16_t x16 = (int16_t)((uint16_t)raw[2] | ((uint16_t)raw[3] << 8));
            int16_t y16 = (int16_t)((uint16_t)raw[4] | ((uint16_t)raw[5] << 8));

            /* Clamp deltas to ±512 */
            if (x16 > 512) x16 = 512;
            if (x16 < -512) x16 = -512;
            if (y16 > 512) y16 = 512;
            if (y16 < -512) y16 = -512;

            out->dx = x16;
            out->dy = y16;
            out->wheel = (transferred >= 7) ? (int16_t)(int8_t)raw[6] : 0;
            return;
        }
    }

    /* Standard 3-byte / 4-byte / 8-byte USB Boot Protocol Mouse
     * Byte 0: Buttons (bit 0=Left, bit 1=Right, bit 2=Middle)
     * Byte 1: X displacement (int8_t)
     * Byte 2: Y displacement (int8_t)
     * Byte 3: Wheel (optional)
     */
    out->buttons = raw[0] & 0x07;
    out->dx      = (int16_t)(int8_t)raw[1];
    out->dy      = (int16_t)(int8_t)raw[2];
    out->wheel   = (transferred >= 4) ? (int16_t)(int8_t)raw[3] : 0;
}

static void xhci_queue_ep1_transfer(uint32_t slot_id, uintptr_t buf_addr, uint32_t len);

static void xhci_handle_transfer_event(uint32_t ev_slot, uint32_t ev_epid, uint32_t ev_code, uint32_t ev_status) {
    if (ev_slot == (uint32_t)s_kbd_slot_id && ev_epid == 3) {
        if (ev_code == 1 || ev_code == 13 /* Success or Short Packet */) {
            if (s_kbd_q_count < XHCI_KBD_QUEUE_SIZE) {
                s_kbd_queue[s_kbd_q_tail] = *s_kbd_buf;
                s_kbd_q_tail = (s_kbd_q_tail + 1) % XHCI_KBD_QUEUE_SIZE;
                s_kbd_q_count++;
            }
        }
        xhci_queue_ep1_transfer(s_kbd_slot_id, (uintptr_t)s_kbd_buf, s_kbd_mps);
        return;
    }

    /* Check all registered mice */
    for (int m = 0; m < s_num_mice; m++) {
        if (ev_slot == (uint32_t)s_mice[m].slot_id && ev_epid == 3) {
            if (ev_code == 1 || ev_code == 13) {
                uint32_t rem = ev_status & 0xFFFFFF;
                uint32_t transferred = (rem <= s_mice[m].mps) ? (s_mice[m].mps - rem) : s_mice[m].mps;
                if (transferred == 0) transferred = s_mice[m].mps;

                usb_mouse_report_t temp_rep = {0};
                xhci_decode_mouse_report(&s_mice[m], transferred, &temp_rep);

                /* Zero out the DMA buffer after consumption so stale bytes never leak into subsequent reports */
                for (uint32_t b = 0; b < s_mice[m].mps; b++) {
                    s_mice[m].buf[b] = 0;
                }
                dsb();

                s_accum_dx += temp_rep.dx;
                s_accum_dy += temp_rep.dy;
                s_accum_wheel += temp_rep.wheel;
                s_latest_buttons = temp_rep.buttons;
                s_has_mouse = 1;
            }
            xhci_queue_ep1_transfer(s_mice[m].slot_id, (uintptr_t)s_mice[m].buf, s_mice[m].mps);
            return;
        }
    }
}

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

static int xhci_cmd_submit(uint64_t param, uint32_t status, uint32_t trb_type, uint32_t slot_id, uint32_t *out_slot_id) {
    uint32_t idx = s_cmd_enqueue_idx;
    uint32_t cycle = s_cmd_cycle_bit;

    s_cmd_ring[idx].param   = param;
    s_cmd_ring[idx].status  = status;
    s_cmd_ring[idx].control = (trb_type << 10) | (slot_id << 24) | cycle;
    dsb();

    s_cmd_enqueue_idx++;
    if (s_cmd_enqueue_idx >= (XHCI_RING_SIZE - 1)) {
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
            uint32_t ev_epid = (ev_ctrl >> 16) & 0x1F;
            uint32_t ev_completion_code = (s_event_ring[ev_idx].status >> 24) & 0xFF;

            /* Advance Event Dequeue */
            s_event_dequeue_idx++;
            if (s_event_dequeue_idx >= XHCI_RING_SIZE) {
                s_event_dequeue_idx = 0;
                s_event_cycle_bit ^= 1;
            }

            uintptr_t ir0 = s_rt_base + 0x20;
            xwrite64(ir0 + XHCI_IR_ERDP,
                     ((uint64_t)(uintptr_t)&s_event_ring[s_event_dequeue_idx] & ~0xFULL) | (1u << 3 /* EHB */));
            dsb();

            if (ev_type == XHCI_TRB_EVT_CMD_COMPL) {
                if (out_slot_id) *out_slot_id = ev_slot;
                return (ev_completion_code == 1 /* Success */) ? 0 : (int)ev_completion_code;
            } else if (ev_type == XHCI_TRB_EVT_TRANSFER) {
                uint32_t ev_code = (s_event_ring[ev_idx].status >> 24) & 0xFF;
                xhci_handle_transfer_event(ev_slot, ev_epid, ev_code, s_event_ring[ev_idx].status);
            }
        }
        delay_cycles(20);
    }

    uint32_t sts = xread32(s_op_base + XHCI_OP_USBSTS);
    fb_log("[XHCI] CMD Timeout! USBSTS=");
    fb_log_hex32(sts);
    fb_log("\n");
    return -1;
}

/* ─────────────────────────────────────────────────────────────────
 * Endpoint 0 Control Transfer Engine
 * ───────────────────────────────────────────────────────────────── */

static int xhci_ep0_control_transfer(uint32_t slot_id, uint8_t bmRequestType, uint8_t bRequest,
                                     uint16_t wValue, uint16_t wIndex, uint16_t wLength,
                                     void *data_buf)
{
    if (slot_id == 0 || slot_id > XHCI_MAX_SLOTS) return -1;
    volatile xhci_trb_t *ring = EP0_RING_BASE(slot_id);
    uint32_t idx = s_ep0_enqueue_idx[slot_id];
    uint32_t cycle = s_ep0_cycle[slot_id];

    uint32_t trt = 0;
    if (wLength > 0) {
        trt = (bmRequestType & 0x80) ? 3 : 2; /* 3=IN, 2=OUT */
        if (!(bmRequestType & 0x80) && data_buf) {
            uint8_t *src = (uint8_t *)data_buf;
            for (uint32_t i = 0; i < wLength; i++) {
                DMA_SCRATCH_BUF[i] = src[i];
            }
        }
    }

    /* 1. Setup Stage TRB */
    ring[idx].param = (uint64_t)bmRequestType |
                      ((uint64_t)bRequest << 8) |
                      ((uint64_t)wValue << 16) |
                      ((uint64_t)wIndex << 32) |
                      ((uint64_t)wLength << 48);
    ring[idx].status = 8;
    ring[idx].control = (XHCI_TRB_SETUP_STAGE << 10) | (1u << 6 /* IDT */) | (trt << 16) | cycle;
    idx++;

    /* 2. Data Stage TRB (if wLength > 0) */
    if (wLength > 0) {
        ring[idx].param = (uint64_t)(uintptr_t)DMA_SCRATCH_BUF;
        ring[idx].status = wLength;
        ring[idx].control = (XHCI_TRB_DATA_STAGE << 10) |
                            ((bmRequestType & 0x80) ? (1u << 16 /* DIR=IN */) : 0) |
                            cycle;
        idx++;
    }

    /* 3. Status Stage TRB */
    ring[idx].param = 0;
    ring[idx].status = 0;
    ring[idx].control = (XHCI_TRB_STATUS_STAGE << 10) |
                        ((bmRequestType & 0x80) ? 0 : (1u << 16 /* DIR=IN */)) |
                        (1u << 5 /* IOC */) | cycle;
    idx++;

    if (idx >= (XHCI_RING_SIZE - 4)) {
        ring[idx].param = (uint64_t)(uintptr_t)ring;
        ring[idx].status = 0;
        ring[idx].control = (XHCI_TRB_LINK << 10) | (1u << 1 /* TC */) | cycle;
        idx = 0;
        cycle ^= 1;
    }
    s_ep0_enqueue_idx[slot_id] = idx;
    s_ep0_cycle[slot_id] = cycle;
    dsb();

    /* Ring EP0 Doorbell (Target = 1) */
    xhci_ring_doorbell(slot_id, 1);

    /* Poll Event Ring for Transfer Completion Event */
    int timeout = 500000;
    while (timeout-- > 0) {
        uint32_t ev_idx = s_event_dequeue_idx;
        uint32_t ev_ctrl = s_event_ring[ev_idx].control;
        if ((ev_ctrl & 1) == s_event_cycle_bit) {
            uint32_t ev_type = (ev_ctrl >> 10) & 0x3F;
            uint32_t ev_slot = (ev_ctrl >> 24) & 0xFF;
            uint32_t ev_epid = (ev_ctrl >> 16) & 0x1F;
            uint32_t ev_code = (s_event_ring[ev_idx].status >> 24) & 0xFF;

            s_event_dequeue_idx++;
            if (s_event_dequeue_idx >= XHCI_RING_SIZE) {
                s_event_dequeue_idx = 0;
                s_event_cycle_bit ^= 1;
            }

            uintptr_t ir0 = s_rt_base + 0x20;
            xwrite64(ir0 + XHCI_IR_ERDP,
                     ((uint64_t)(uintptr_t)&s_event_ring[s_event_dequeue_idx] & ~0xFULL) | (1u << 3 /* EHB */));
            dsb();

            if (ev_type == XHCI_TRB_EVT_TRANSFER && ev_slot == slot_id && ev_epid == 1) {
                if (ev_code == 1 || ev_code == 13 /* Success or Short Packet */) {
                    if ((bmRequestType & 0x80) && wLength > 0 && data_buf) {
                        uint8_t *dst = (uint8_t *)data_buf;
                        for (uint32_t i = 0; i < wLength; i++) {
                            dst[i] = DMA_SCRATCH_BUF[i];
                        }
                    }
                    return 0;
                }
                fb_log("[XHCI] EP0 Transfer Error Code=");
                fb_log_dec(ev_code);
                fb_log("\n");
                return (int)ev_code;
            } else if (ev_type == XHCI_TRB_EVT_TRANSFER) {
                uint32_t ev_code = (s_event_ring[ev_idx].status >> 24) & 0xFF;
                xhci_handle_transfer_event(ev_slot, ev_epid, ev_code, s_event_ring[ev_idx].status);
            } else {
                fb_log("[XHCI] EP0 Evt (Type=");
                fb_log_dec(ev_type);
                fb_log(" Slot=");
                fb_log_dec(ev_slot);
                fb_log(" EP=");
                fb_log_dec(ev_epid);
                fb_log(" Code=");
                fb_log_dec(ev_code);
                fb_log(")\n");
            }
        }
        delay_cycles(20);
    }

    uint32_t sts = xread32(s_op_base + XHCI_OP_USBSTS);
    fb_log("[XHCI] EP0 Timeout! USBSTS=");
    fb_log_hex32(sts);
    fb_log("\n");
    return -1;
}

/* ─────────────────────────────────────────────────────────────────
 * Device Addressing & Context Configuration
 * ───────────────────────────────────────────────────────────────── */

static int xhci_address_device(uint32_t slot_id, uint32_t root_port, uint32_t speed,
                               bool is_split, uint32_t hub_slot, uint32_t hub_port,
                               uint32_t max_packet_size)
{
    if (slot_id == 0 || slot_id > XHCI_MAX_SLOTS) return -1;

    /* Zero 2KB Input Context */
    for (int i = 0; i < 2048 / 4; i++) {
        s_input_ctx[i] = 0;
    }

    /* Zero Device Context for this slot */
    volatile uint32_t *dev_ctx = DEV_CTX_BASE(slot_id);
    for (int i = 0; i < 1024 / 4; i++) {
        dev_ctx[i] = 0;
    }

    /* Zero EP0 ring */
    volatile xhci_trb_t *ep0_ring = EP0_RING_BASE(slot_id);
    for (int i = 0; i < XHCI_RING_SIZE; i++) {
        ep0_ring[i].param = 0;
        ep0_ring[i].status = 0;
        ep0_ring[i].control = 0;
    }
    s_ep0_enqueue_idx[slot_id] = 0;
    s_ep0_cycle[slot_id] = 1;

    /* Zero EP1 ring and pre-initialize static Link TRB at ring boundary */
    volatile xhci_trb_t *ep1_ring = EP1_RING_BASE(slot_id);
    for (int i = 0; i < XHCI_RING_SIZE; i++) {
        ep1_ring[i].param = 0;
        ep1_ring[i].status = 0;
        ep1_ring[i].control = 0;
    }
    ep1_ring[XHCI_RING_SIZE - 1].param = (uint64_t)(uintptr_t)ep1_ring;
    ep1_ring[XHCI_RING_SIZE - 1].status = 0;
    ep1_ring[XHCI_RING_SIZE - 1].control = (XHCI_TRB_LINK << 10) | (1u << 1 /* TC */) | 1u;
    s_ep1_enqueue_idx[slot_id] = 0;
    s_ep1_cycle[slot_id] = 1;

    /* 1. Input Control Context (offset 0) */
    s_input_ctx[0] = 0;                       /* Drop flags */
    s_input_ctx[1] = (1u << 0) | (1u << 1);  /* Add flags: A0 (Slot) + A1 (EP0) */

    /* 2. Slot Context (offset 0x20 = index 8 in 32-bit words)
     * For USB 2.0 / 1.1 devices, route_string MUST be 0 (routing is handled via TT / parent hub). */
    s_input_ctx[8]  = ((speed & 0xF) << 20) | (1u << 27 /* Context Entries = 1 */);
    s_input_ctx[9]  = (root_port & 0xFF) << 16;
    s_input_ctx[10] = is_split ? ((hub_slot & 0xFF) | ((hub_port & 0xFF) << 8)) : 0;
    s_input_ctx[11] = 0;

    /* 3. EP0 Context (offset 0x40 = index 16 in 32-bit words) */
    s_input_ctx[16] = 0; /* ep_info1 */
    s_input_ctx[17] = (3u << 1 /* CErr=3 */) | (4u << 3 /* EP Type = 4 Control */) | ((max_packet_size & 0xFFFF) << 16);
    s_input_ctx[18] = (uint32_t)(uintptr_t)ep0_ring | 1u /* DCS = 1 */;
    s_input_ctx[19] = (uint32_t)((uint64_t)(uintptr_t)ep0_ring >> 32);
    s_input_ctx[20] = 8; /* Average TRB Length = 8 */

    /* Set DCBAA[slot_id] */
    s_dcbaa[slot_id] = (uint64_t)(uintptr_t)dev_ctx;
    dsb();

    /* Submit Address Device Command */
    int ret = xhci_cmd_submit((uint64_t)(uintptr_t)s_input_ctx, 0, XHCI_TRB_ADDRESS_DEV, slot_id, NULL);
    return ret;
}

static int xhci_evaluate_hub_context(uint32_t slot_id, uint32_t num_ports) {
    if (slot_id == 0 || slot_id > XHCI_MAX_SLOTS) return -1;

    for (int i = 0; i < 2048 / 4; i++) {
        s_input_ctx[i] = 0;
    }

    s_input_ctx[0] = 0;
    s_input_ctx[1] = (1u << 0); /* Add Slot Context */

    volatile uint32_t *dev_ctx = DEV_CTX_BASE(slot_id);
    for (int i = 0; i < 8; i++) {
        s_input_ctx[8 + i] = dev_ctx[i];
    }
    s_input_ctx[8] |= (1u << 26); /* Hub = 1 */
    s_input_ctx[9] = (s_input_ctx[9] & ~(0xFFu << 24)) | ((num_ports & 0xFF) << 24);
    dsb();

    int ret = xhci_cmd_submit((uint64_t)(uintptr_t)s_input_ctx, 0, XHCI_TRB_EVAL_CTX, slot_id, NULL);
    return ret;
}

static int xhci_evaluate_ep0_max_packet(uint32_t slot_id, uint32_t max_p) {
    if (slot_id == 0 || slot_id > XHCI_MAX_SLOTS) return -1;

    for (int i = 0; i < 2048 / 4; i++) {
        s_input_ctx[i] = 0;
    }

    s_input_ctx[0] = 0;
    s_input_ctx[1] = (1u << 0) | (1u << 1); /* Add Slot Context (A0) + EP0 Context (A1) */

    volatile uint32_t *dev_ctx = DEV_CTX_BASE(slot_id);
    /* In Device Context, Slot context is at index 0..7 */
    for (int i = 0; i < 8; i++) {
        s_input_ctx[8 + i] = dev_ctx[i];
    }
    /* In Device Context, EP0 context is at offset 0x20 = index 8..15 */
    for (int i = 0; i < 8; i++) {
        s_input_ctx[16 + i] = dev_ctx[8 + i];
    }
    s_input_ctx[17] = (s_input_ctx[17] & ~(0xFFFFu << 16)) | ((max_p & 0xFFFF) << 16);
    dsb();

    int ret = xhci_cmd_submit((uint64_t)(uintptr_t)s_input_ctx, 0, XHCI_TRB_EVAL_CTX, slot_id, NULL);
    return ret;
}

static int xhci_configure_hid_endpoint(uint32_t slot_id, uint32_t speed, uint32_t interval, uint32_t max_packet_size) {
    if (slot_id == 0 || slot_id > XHCI_MAX_SLOTS) return -1;

    for (int i = 0; i < 2048 / 4; i++) {
        s_input_ctx[i] = 0;
    }

    s_input_ctx[0] = 0;                       /* Drop flags */
    s_input_ctx[1] = (1u << 0) | (1u << 3);  /* Add flags: A0 (Slot) + A3 (EP1 IN) */

    volatile uint32_t *dev_ctx = DEV_CTX_BASE(slot_id);
    for (int i = 0; i < 8; i++) {
        s_input_ctx[8 + i] = dev_ctx[i];
    }
    s_input_ctx[8] = (s_input_ctx[8] & ~(0x1Fu << 27)) | (3u << 27 /* Context Entries = 3 */);

    volatile xhci_trb_t *ep1_ring = EP1_RING_BASE(slot_id);
    s_input_ctx[32] = (interval & 0xFF) << 16; /* Interval in frames/ms */
    s_input_ctx[33] = (3u << 1 /* CErr=3 */) | (7u << 3 /* EP Type = 7 Interrupt IN */) | ((max_packet_size & 0xFFFF) << 16);
    s_input_ctx[34] = (uint32_t)(uintptr_t)ep1_ring | 1u /* DCS = 1 */;
    s_input_ctx[35] = (uint32_t)((uint64_t)(uintptr_t)ep1_ring >> 32);
    uint32_t payload = (max_packet_size > 0) ? max_packet_size : 8;
    s_input_ctx[36] = (payload & 0xFFFF) | ((payload & 0xFFFF) << 16); /* Average TRB Length, Max ESIT Payload */
    dsb();

    int ret = xhci_cmd_submit((uint64_t)(uintptr_t)s_input_ctx, 0, XHCI_TRB_CONFIG_EP, slot_id, NULL);
    return ret;
}

static void xhci_queue_ep1_transfer(uint32_t slot_id, uintptr_t buf_addr, uint32_t len) {
    if (slot_id == 0 || slot_id > XHCI_MAX_SLOTS) return;
    volatile xhci_trb_t *ring = EP1_RING_BASE(slot_id);
    uint32_t idx = s_ep1_enqueue_idx[slot_id];
    uint32_t cycle = s_ep1_cycle[slot_id];

    ring[idx].param = (uint64_t)buf_addr;
    ring[idx].status = len; /* TD Size = 0 for single-TRB TD */
    ring[idx].control = (XHCI_TRB_NORMAL << 10) | (1u << 5 /* IOC */) | (1u << 2 /* ISP */) | cycle;
    dsb();

    idx++;
    if (idx >= (XHCI_RING_SIZE - 1)) {
        ring[idx].param = (uint64_t)(uintptr_t)ring;
        ring[idx].status = 0;
        ring[idx].control = (XHCI_TRB_LINK << 10) | (1u << 1 /* TC */) | cycle;
        dsb();
        idx = 0;
        cycle ^= 1;
    }
    s_ep1_enqueue_idx[slot_id] = idx;
    s_ep1_cycle[slot_id] = cycle;

    /* Ring Doorbell for EP1 IN (Target = 3) */
    xhci_ring_doorbell(slot_id, 3);
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

    uint32_t dboff = xread32(s_cap_base + 0x14) & ~3u;
    uint32_t rtsoff = xread32(s_cap_base + 0x18) & ~0x1Fu;
    s_db_base = s_cap_base + dboff;
    s_rt_base = s_cap_base + rtsoff;

    uint32_t hcsparams1 = xread32(s_cap_base + 0x04);
    s_max_ports = (hcsparams1 >> 24) & 0xFF;

    fb_log("[XHCI] DBOFF="); fb_log_hex32(dboff);
    fb_log(" RTSOFF=");     fb_log_hex32(rtsoff);
    fb_log("\n");
    fb_log("[XHCI] Max Ports="); fb_log_dec(s_max_ports);
    fb_log(" (HCSPARAMS1=");    fb_log_hex32(hcsparams1);
    fb_log(")\n");

    s_kbd_slot_id = 0;
    s_kbd_mps = 8;
    for (int i = 0; i < XHCI_MAX_MICE; i++) {
        s_mice[i].slot_id = 0;
        s_mice[i].mps = 0;
        s_mice[i].buf = NULL;
        s_mice[i].proto_mode = 0;
    }
    s_num_mice = 0;
    s_kbd_q_head = 0;
    s_kbd_q_tail = 0;
    s_kbd_q_count = 0;
    s_accum_dx = 0;
    s_accum_dy = 0;
    s_accum_wheel = 0;
    s_latest_buttons = 0;
    s_has_mouse = 0;

    /* 1. Stop Controller */
    uint32_t cmd = xread32(s_op_base + XHCI_OP_USBCMD);
    cmd &= ~XHCI_CMD_RS;
    xwrite32(s_op_base + XHCI_OP_USBCMD, cmd);
    dsb();

    int to = 1000;
    while (!(xread32(s_op_base + XHCI_OP_USBSTS) & XHCI_STS_HCH) && --to > 0) {
        delay_us(1);
    }

    /* 2. Reset Controller */
    xwrite32(s_op_base + XHCI_OP_USBCMD, XHCI_CMD_HCRST);
    dsb();

    to = 2000;
    while ((xread32(s_op_base + XHCI_OP_USBCMD) & XHCI_CMD_HCRST) && --to > 0) {
        delay_us(1);
    }
    to = 2000;
    while ((xread32(s_op_base + XHCI_OP_USBSTS) & XHCI_STS_CNR) && --to > 0) {
        delay_us(1);
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

    /* 4a. Configure Scratchpad Buffers (required if HCSPARAMS2 Max Scratchpad > 0) */
    uint32_t hcsparams2 = xread32(s_cap_base + 0x08);
    uint32_t max_scratchpad = (hcsparams2 >> 27) & 0x1F;
    if (max_scratchpad > 32) max_scratchpad = 32;
    fb_log("[XHCI] HCSPARAMS2="); fb_log_hex32(hcsparams2);
    fb_log(" Scratchpads="); fb_log_dec(max_scratchpad);
    fb_log("\n");

    if (max_scratchpad > 0) {
        uint64_t *scratch_array = (uint64_t *)(XHCI_DMA_BASE + 0x10000);
        uintptr_t scratch_pages = (uintptr_t)(XHCI_DMA_BASE + 0x20000);
        for (uint32_t i = 0; i < max_scratchpad; i++) {
            scratch_array[i] = (uint64_t)(scratch_pages + i * 4096);
        }
        s_dcbaa[0] = (uint64_t)(uintptr_t)scratch_array;
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

    xwrite32(ir0 + XHCI_IR_IMAN, 0x2); /* IE = 1 */
    xwrite32(ir0 + XHCI_IR_IMOD, 0);

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

    to = 1000;
    while ((xread32(s_op_base + XHCI_OP_USBSTS) & XHCI_STS_HCH) && --to > 0) {
        delay_us(1);
    }

    fb_log("[XHCI] Controller Running: OK (Ports: ");
    fb_log_dec(s_max_ports);
    fb_log(")\n");

    /* 9. Power on Root Hub Ports */
    uint32_t total_ports = (s_max_ports > 0 && s_max_ports <= 8) ? s_max_ports : 8;
    uint32_t w1c_mask = (1u << 1) | (1u << 17) | (1u << 18) | (1u << 19) | (1u << 20) | (1u << 21) | (1u << 22);

    for (uint32_t p = 1; p <= total_ports; p++) {
        uintptr_t port_reg = s_op_base + XHCI_OP_PORTSC_BASE + (p - 1) * 0x10;
        uint32_t psc = xread32(port_reg);
        psc &= ~w1c_mask;
        psc |= XHCI_PORT_PP;
        xwrite32(port_reg, psc);
    }
    delay_us(20000); /* root port power settle */

    /* 10. Check Root Port 1 (High-Speed USB 2.0 Hub on Pi 400) */
    uintptr_t port1_reg = s_op_base + XHCI_OP_PORTSC_BASE;
    uint32_t psc = xread32(port1_reg);
    fb_log("[XHCI] Port 1 PORTSC=");
    fb_log_hex32(psc);
    fb_log("\n");

    if (psc & (XHCI_PORT_CCS | XHCI_PORT_CSC)) {
        fb_log("[XHCI] Port 1 Connected -> Resetting Port...\n");

        uint32_t reset_cmd = (psc & ~w1c_mask) | XHCI_PORT_PR | XHCI_PORT_PP;
        xwrite32(port1_reg, reset_cmd);
        delay_us(5000);

        for (int r = 0; r < 200; r++) {
            psc = xread32(port1_reg);
            if (!(psc & XHCI_PORT_PR)) break;
            delay_us(100);
        }

        fb_log("[XHCI] Port 1 Post-Reset: PORTSC=");
        fb_log_hex32(psc);
        fb_log("\n");

        uint32_t port_speed = (psc >> 10) & 0x0F;
        fb_log("[XHCI] Speed=");
        fb_log_dec(port_speed);
        fb_log(" PED=");
        fb_log_dec((psc & XHCI_PORT_PED) ? 1 : 0);
        fb_log("\n");

        /* Enable Slot 1 for the Internal USB 2.0 Hub */
        uint32_t hub_slot = 0;
        int ret = xhci_cmd_submit(0, 0, XHCI_TRB_ENABLE_SLOT, 0, &hub_slot);
        fb_log("[XHCI] ENABLE_SLOT (Hub) ret=");
        fb_log_dec((uint32_t)ret);
        fb_log(" slot_id=");
        fb_log_dec(hub_slot);
        fb_log("\n");

        if (ret == 0 && hub_slot >= 1) {
            /* Address Hub Device (Root Port 1, High-Speed, Max Packet 64) */
            ret = xhci_address_device(1, 1, 3 /* High-Speed */, false, 0, 0, 64);
            fb_log("[XHCI] ADDRESS_DEV (Hub) ret=");
            fb_log_dec((uint32_t)ret);
            fb_log("\n");

            delay_us(20000);

            /* Read Device Descriptor */
            usb_device_desc_t dev_desc = {0};
            ret = xhci_ep0_control_transfer(1, 0x80, USB_REQ_GET_DESCRIPTOR, (USB_DT_DEVICE << 8), 0, sizeof(dev_desc), &dev_desc);
            fb_log("[XHCI] Hub GET_DESC ret=");
            fb_log_dec((uint32_t)ret);
            fb_log(" Class=");
            fb_log_hex32(dev_desc.bDeviceClass);
            fb_log(" VID=");
            fb_log_hex32(dev_desc.idVendor);
            fb_log(" PID=");
            fb_log_hex32(dev_desc.idProduct);
            fb_log("\n");

            delay_us(10000);

            /* Set Configuration 1 */
            ret = xhci_ep0_control_transfer(1, 0x00, USB_REQ_SET_CONFIGURATION, 1, 0, 0, NULL);
            fb_log("[XHCI] Hub SET_CONFIG ret=");
            fb_log_dec((uint32_t)ret);
            fb_log("\n");

            delay_us(10000);

            /* Evaluate Hub Context (Hub=1, Ports=4) */
            ret = xhci_evaluate_hub_context(1, 4);
            fb_log("[XHCI] EVAL_CTX (Hub 4 ports) ret=");
            fb_log_dec((uint32_t)ret);
            fb_log("\n");

            /* Power on all 4 Hub ports */
            for (uint32_t hp = 1; hp <= 4; hp++) {
                xhci_ep0_control_transfer(1, 0x23, USB_REQ_SET_FEATURE, HUB_FEAT_PORT_POWER, hp, 0, NULL);
            }
            delay_us(250000); /* 250ms USB hub port power settle */

            /* Scan Hub Ports 1..4 */
            for (uint32_t hp = 1; hp <= 4; hp++) {
                usb_port_status_t pstat = {0};
                ret = xhci_ep0_control_transfer(1, 0xA3, USB_REQ_GET_STATUS, 0, hp, 4, &pstat);
                if (ret != 0) continue;

                fb_log("[XHCI] Hub Port ");
                fb_log_dec(hp);
                fb_log(" Status=");
                fb_log_hex32(pstat.wPortStatus);
                fb_log("\n");

                if (pstat.wPortStatus & HUB_PORT_STAT_CONNECTION) {
                    fb_log("[XHCI] Hub Port ");
                    fb_log_dec(hp);
                    fb_log(" Device Attached -> Resetting...\n");

                    /* Issue Hub Port Reset (minimum 50-60ms as per USB 2.0 spec) */
                    xhci_ep0_control_transfer(1, 0x23, USB_REQ_SET_FEATURE, HUB_FEAT_PORT_RESET, hp, 0, NULL);
                    delay_us(60000);

                    /* Clear reset change and re-read Port Status */
                    xhci_ep0_control_transfer(1, 0x23, USB_REQ_CLEAR_FEATURE, HUB_FEAT_C_PORT_RESET, hp, 0, NULL);
                    delay_us(10000); /* 10ms reset recovery time (T_RSTRCY) */
                    xhci_ep0_control_transfer(1, 0xA3, USB_REQ_GET_STATUS, 0, hp, 4, &pstat);

                    uint32_t dev_speed = 1; /* Default Full-Speed */
                    if (pstat.wPortStatus & HUB_PORT_STAT_LOW_SPEED) {
                        dev_speed = 2; /* Low-Speed (1.5 Mbps) */
                    } else if (pstat.wPortStatus & HUB_PORT_STAT_HIGH_SPEED) {
                        dev_speed = 3; /* High-Speed (480 Mbps) */
                    }

                    fb_log("[XHCI] Hub Port ");
                    fb_log_dec(hp);
                    fb_log(" Speed=");
                    fb_log_dec(dev_speed);
                    fb_log("\n");

                    /* Enable Slot for downstream device */
                    uint32_t dev_slot = 0;
                    ret = xhci_cmd_submit(0, 0, XHCI_TRB_ENABLE_SLOT, 0, &dev_slot);
                    if (ret != 0 || dev_slot == 0) continue;

                    fb_log("[XHCI] Allocated Slot ");
                    fb_log_dec(dev_slot);
                    fb_log(" for Hub Port ");
                    fb_log_dec(hp);
                    fb_log("\n");

                    /* Address Device (Split-Transaction via Hub Slot 1, Port hp)
                     * For Full/Low-Speed, initialize EP0 with 8 bytes.
                     * For High-Speed, initialize EP0 with 64 bytes. */
                    uint32_t ep0_init_mps = (dev_speed == 3) ? 64 : 8;
                    ret = xhci_address_device(dev_slot, 1, dev_speed, true, 1, hp, ep0_init_mps);
                    if (ret != 0) {
                        fb_log("[XHCI] AddressDevice failed for Slot ");
                        fb_log_dec(dev_slot);
                        fb_log(" ret=");
                        fb_log_dec((uint32_t)ret);
                        fb_log("\n");
                        continue;
                    }
                    delay_us(10000);

                    /* Step 1: Read first 8 bytes of Device Descriptor to learn true bMaxPacketSize0 */
                    usb_device_desc_t ddesc = {0};
                    ret = xhci_ep0_control_transfer(dev_slot, 0x80, USB_REQ_GET_DESCRIPTOR, (USB_DT_DEVICE << 8), 0, 8, &ddesc);
                    if (ret != 0) {
                        fb_log("[XHCI] Read initial 8B desc failed for Slot ");
                        fb_log_dec(dev_slot);
                        fb_log("\n");
                        continue;
                    }

                    uint32_t real_mps = ddesc.bMaxPacketSize0;
                    if (real_mps < 8 || real_mps > 64) real_mps = 8;
                    if (real_mps != ep0_init_mps) {
                        ret = xhci_evaluate_ep0_max_packet(dev_slot, real_mps);
                        fb_log("[XHCI] Slot ");
                        fb_log_dec(dev_slot);
                        fb_log(" Update EP0 MPS=");
                        fb_log_dec(real_mps);
                        fb_log(" EVAL ret=");
                        fb_log_dec((uint32_t)ret);
                        fb_log("\n");
                        delay_us(5000);
                    }

                    /* Step 2: Read full 18-byte Device Descriptor */
                    ret = xhci_ep0_control_transfer(dev_slot, 0x80, USB_REQ_GET_DESCRIPTOR, (USB_DT_DEVICE << 8), 0, sizeof(ddesc), &ddesc);

                    uint8_t proto = 0;
                    uint8_t mouse_boot_proto = 0;
                    uint32_t ep1_mps = 8;
                    uint32_t ep1_interval = 6; /* default 8ms */
                    bool is_keyboard = false;
                    bool is_mouse = false;

                    if (ddesc.idVendor == 0x04D9 /* Holtek Pi 400 Keyboard */) {
                        is_keyboard = true;
                        proto = 1;
                        ep1_mps = 8;
                        ep1_interval = 6;
                    } else {
                        /* For external USB devices (e.g. Logitech mouse, Raspberry mouse):
                         * Read 9-byte Configuration Header first to inspect true wTotalLength */
                        uint8_t cfg_hdr[9] = {0};
                        ret = xhci_ep0_control_transfer(dev_slot, 0x80, USB_REQ_GET_DESCRIPTOR, (USB_DT_CONFIGURATION << 8), 0, 9, cfg_hdr);
                        uint32_t tot_len = (ret == 0 && cfg_hdr[0] >= 9) ? ((uint32_t)cfg_hdr[2] | ((uint32_t)cfg_hdr[3] << 8)) : 0;
                        if (tot_len > 128) tot_len = 128;

                        uint8_t cfg_buf[128] = {0};
                        if (tot_len > 0) {
                            ret = xhci_ep0_control_transfer(dev_slot, 0x80, USB_REQ_GET_DESCRIPTOR, (USB_DT_CONFIGURATION << 8), 0, tot_len, cfg_buf);
                        }

                        uint8_t cur_if_proto = 0;
                        for (uint32_t off = 0; off + 2 <= tot_len; ) {
                            uint8_t len = cfg_buf[off];
                            uint8_t type = cfg_buf[off + 1];
                            if (len < 2 || off + len > tot_len) break;

                            if (type == USB_DT_INTERFACE && len >= 9) {
                                cur_if_proto = cfg_buf[off + 7]; /* 1=Keyboard, 2=Mouse */
                                if (cur_if_proto == 1) is_keyboard = true;
                                if (cur_if_proto == 2) is_mouse = true;
                            } else if (type == USB_DT_ENDPOINT && len >= 7) {
                                uint8_t ep_addr = cfg_buf[off + 2];
                                if (ep_addr & 0x80) { /* IN endpoint */
                                    uint32_t mps = cfg_buf[off + 4] | ((uint32_t)cfg_buf[off + 5] << 8);
                                    if (mps < 4 || mps > 64) mps = 8;

                                    if (cur_if_proto == 2) {
                                        is_mouse = true;
                                        proto = 2;
                                        mouse_boot_proto = 1;
                                        ep1_mps = mps;
                                        ep1_interval = 3; /* Force 1 ms (1000 Hz) polling for instant response */
                                    } else if (cur_if_proto == 1 && !is_mouse) {
                                        is_keyboard = true;
                                        proto = 1;
                                        ep1_mps = 8;
                                        ep1_interval = 4; /* 2 ms (500 Hz) polling */
                                    } else if (!is_keyboard && !is_mouse) {
                                        ep1_mps = mps;
                                        ep1_interval = 3; /* Default to 1 ms polling */
                                    }
                                }
                            }
                            off += len;
                        }

                        if (!is_keyboard && !is_mouse) {
                            if (dev_speed == 2 && !s_kbd_slot_id) {
                                is_keyboard = true;
                                proto = 1;
                                ep1_mps = 8;
                                ep1_interval = 4;
                            } else {
                                is_mouse = true;
                                proto = 2;
                                if (ep1_mps < 8) ep1_mps = 8;
                                ep1_interval = 3;
                            }
                        }
                    }

                    fb_log("[XHCI] Slot ");
                    fb_log_dec(dev_slot);
                    fb_log(" VID=");
                    fb_log_hex32(ddesc.idVendor);
                    fb_log(" PID=");
                    fb_log_hex32(ddesc.idProduct);
                    fb_log(" Proto=");
                    fb_log_dec(proto);
                    fb_log(" EP1_MPS=");
                    fb_log_dec(ep1_mps);
                    fb_log(" Int=");
                    fb_log_dec(ep1_interval);
                    fb_log("\n");

                    /* Set Configuration 1 */
                    xhci_ep0_control_transfer(dev_slot, 0x00, USB_REQ_SET_CONFIGURATION, 1, 0, 0, NULL);
                    delay_us(10000); /* 10ms settle time after SET_CONFIGURATION */

                    /* Configure EP1 Interrupt IN */
                    ret = xhci_configure_hid_endpoint(dev_slot, dev_speed, ep1_interval, ep1_mps);
                    if (ret != 0) {
                        fb_log("[XHCI] ConfigureEndpoint failed for Slot ");
                        fb_log_dec(dev_slot);
                        fb_log("\n");
                        continue;
                    }

                    /* Bind Driver based on USB HID Interface Protocol / Device ID */
                    if (is_keyboard || proto == 1) {
                        s_kbd_slot_id = dev_slot;
                        s_kbd_mps = ep1_mps;
                        fb_log("[XHCI] Bound Slot ");
                        fb_log_dec(dev_slot);
                        fb_log(" to Pi 400 Keyboard Driver [OK]\n");
                    } else if (is_mouse || proto == 2) {
                        if (s_num_mice < XHCI_MAX_MICE) {
                            s_mice[s_num_mice].slot_id = dev_slot;
                            s_mice[s_num_mice].mps = ep1_mps;
                            s_mice[s_num_mice].buf = MOUSE_BUF(s_num_mice);
                            /* Standard mice (PixArt 0x093A, Boot Protocol proto 2) lock to mode 1 (Standard Boot Protocol).
                             * Only Logitech gaming mice (VID 0x046D) start in mode 0 auto-detect. */
                            if (ddesc.idVendor == 0x093A || mouse_boot_proto == 1 || proto == 2) {
                                s_mice[s_num_mice].proto_mode = 1;
                            } else if (ddesc.idVendor == 0x046D) {
                                s_mice[s_num_mice].proto_mode = 0;
                            } else {
                                s_mice[s_num_mice].proto_mode = 1;
                            }
                            fb_log("[XHCI] Bound Slot ");
                            fb_log_dec(dev_slot);
                            fb_log(" as Mouse ");
                            fb_log_dec(s_num_mice + 1);
                            fb_log(" [OK]\n");
                            s_num_mice++;
                        }
                    }
                }
            }
        }
    }

    /* Log Ports 2..5 (SuperSpeed root ports) */
    for (uint32_t p = 2; p <= total_ports; p++) {
        uintptr_t port_reg = s_op_base + XHCI_OP_PORTSC_BASE + (p - 1) * 0x10;
        uint32_t psc_ext = xread32(port_reg);
        fb_log("[XHCI] Port ");
        fb_log_dec(p);
        fb_log(" PORTSC=");
        fb_log_hex32(psc_ext);
        fb_log("\n");
    }

    /* Arm Interrupt IN transfer rings for keyboard and all connected mice */
    if (s_kbd_slot_id) {
        xhci_queue_ep1_transfer(s_kbd_slot_id, (uintptr_t)s_kbd_buf, s_kbd_mps);
    }
    for (int m = 0; m < s_num_mice; m++) {
        xhci_queue_ep1_transfer(s_mice[m].slot_id, (uintptr_t)s_mice[m].buf, s_mice[m].mps);
    }

    return 0;
}

/* ─────────────────────────────────────────────────────────────────
 * Polled Keyboard & Mouse Event Processing
 * ───────────────────────────────────────────────────────────────── */

 /* Public entry point: drain the xHCI event ring once per polling cycle.
 * Must be called ONCE before xhci_poll_keyboard() / xhci_poll_mouse().
 * Calling it multiple times per cycle risks re-processing already-handled TRBs. */

void xhci_process_events(void) {
    for (uint32_t trb_count = 0; trb_count < XHCI_RING_SIZE; trb_count++) {
        uint32_t ev_idx = s_event_dequeue_idx;
        uint32_t ev_ctrl = s_event_ring[ev_idx].control;
        if ((ev_ctrl & 1) != s_event_cycle_bit) {
            break; /* No more events */
        }

        uint32_t ev_type = (ev_ctrl >> 10) & 0x3F;
        uint32_t ev_slot = (ev_ctrl >> 24) & 0xFF;
        uint32_t ev_epid = (ev_ctrl >> 16) & 0x1F;
        uint32_t ev_status = s_event_ring[ev_idx].status;
        uint32_t ev_code = (ev_status >> 24) & 0xFF;

        s_event_dequeue_idx++;
        if (s_event_dequeue_idx >= XHCI_RING_SIZE) {
            s_event_dequeue_idx = 0;
            s_event_cycle_bit ^= 1;
        }

        uintptr_t ir0 = s_rt_base + 0x20;
        xwrite64(ir0 + XHCI_IR_ERDP,
                 ((uint64_t)(uintptr_t)&s_event_ring[s_event_dequeue_idx] & ~0xFULL) | (1u << 3 /* EHB */));
        dsb();

        if (ev_type == XHCI_TRB_EVT_TRANSFER) {
            xhci_handle_transfer_event(ev_slot, ev_epid, ev_code, ev_status);
        }
    }
}

int xhci_poll_keyboard(usb_kbd_report_t *rep) {
    if (!s_kbd_slot_id || !rep) return 0;
    /* NOTE: do NOT call xhci_process_events() here.
     * The caller (usb_poll_devices) must call xhci_process_events() once
     * before calling xhci_poll_keyboard / xhci_poll_mouse.
     * Calling it again here causes double-processing of the event ring. */
    if (s_kbd_q_count > 0) {
        *rep = s_kbd_queue[s_kbd_q_head];
        s_kbd_q_head = (s_kbd_q_head + 1) % XHCI_KBD_QUEUE_SIZE;
        s_kbd_q_count--;
        return 1;
    }
    return 0;
}

int xhci_poll_mouse(usb_mouse_report_t *rep) {
    if (s_num_mice == 0 || !rep) return 0;
    /* NOTE: do NOT call xhci_process_events() here.
     * Caller must invoke xhci_process_events() once before polling. */
    if (s_has_mouse) {
        int32_t dx = s_accum_dx;
        int32_t dy = s_accum_dy;
        if (dx > 512) dx = 512;
        if (dx < -512) dx = -512;
        if (dy > 512) dy = 512;
        if (dy < -512) dy = -512;

        rep->dx = (int16_t)dx;
        rep->dy = (int16_t)dy;
        rep->wheel = (int16_t)s_accum_wheel;
        rep->buttons = s_latest_buttons;

        s_accum_dx = 0;
        s_accum_dy = 0;
        s_accum_wheel = 0;
        s_has_mouse = 0;
        return 1;
    }
    return 0;
}

bool xhci_has_devices(void) {
    return (s_kbd_slot_id != 0 || s_num_mice > 0);
}

#endif /* AArch64 */

