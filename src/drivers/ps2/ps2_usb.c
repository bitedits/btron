/*
 * ps2_usb.c — Cleanroom Sony PlayStation 2 USB Host Controller (OHCI) Driver
 *
 * Controller presence probe plus standard USB HID Boot Protocol packet
 * decoding for keyboard and mouse.  Specification-derived only (OHCI 1.0/1.1
 * and the USB HID 1.1 boot protocol); no vendor code is imported.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include "ps2_usb.h"
#include "ps2_iopram.h"

/* External hooks implemented in core_ps2.c */
extern void ps2_usb_on_key(uint32_t btron_key, int down);
extern void ps2_usb_on_mouse(int dx, int dy, uint8_t buttons);

/* EE wall clock helpers, implemented in boot_ps2.s */
extern uint32_t ps2_count_read(void);
extern void ps2_delay_cycles(uint32_t count);

/* Hardware presence flag */
static int s_usb_available = 0;
static int s_shadow_lost = 0;
static uint8_t s_caps_lock = 0;
static uint8_t s_prev_keys[6] = {0};

static int s_verdict = PS2_OHCI_NONE;
static int s_report_seen;            /* the first landed report has been announced */
static uint32_t s_kbd_reports = 0;
static uint32_t s_mouse_reports = 0;
static ps2_ohci_probe_t s_probe;

#define OHCI_REG(offset) (*(volatile uint32_t *)((uintptr_t)(OHCI_BASE_ADDR + (offset))))

/* OHCI's Host Controller Communications Area, as this controller lays it out:
 * a 128-byte Interrupt Endpoint Table indexed by the low 5 bits of the frame
 * number, then the u16 FrameNumber and u16 DoneHead it rewrites at every frame
 * boundary.  Those two words are the cheapest bus-master writeback there is,
 * which is why the probe uses them: they only come back stamped if the
 * controller masters the memory we are pointing it at -- and that memory has
 * to be IOP RAM, since the controller has no path to RDRAM. */
#define HCCA_INTR_WORDS   32u                      /* 128-byte ED table */
#define HCCA_FRAME_WORD   HCCA_INTR_WORDS          /* u16 frame + u16 pad */
#define HCCA_DONE_WORD    (HCCA_INTR_WORDS + 1u)   /* u32 DoneHead */
#define HCCA_SENTINEL     0xA5A5A5A5u

/* CP0 Count increments at half the 294.912 MHz core clock */
#define EE_COUNTS_PER_US 147u

static int s_count_runs = -1;

static int count_running(void)
{
    if (s_count_runs < 0) {
        uint32_t a = ps2_count_read();
        for (volatile int i = 0; i < 8000; i++) { }
        s_count_runs = (ps2_count_read() != a) ? 1 : 0;
    }
    return s_count_runs;
}

/* Microsecond wait.  Falls back to a counted loop only when CP0 Count is
 * proven stuck, and even then errs long instead of truncating a reset. */
static void ohci_wait_us(uint32_t us)
{
    if (!count_running()) {
        ps2_delay_cycles(us * 300u);
        return;
    }
    uint32_t start = ps2_count_read();
    uint32_t budget = us * EE_COUNTS_PER_US;
    for (uint32_t spins = 0; (uint32_t)(ps2_count_read() - start) < budget; spins++) {
        if (spins > 20000000u) break;
    }
}

static int regs_look_live(uint32_t rev, uint32_t fminterval, uint32_t rh_a)
{
    /* A window that no bus decoder claims reads either all-ones or all-zeros
     * on every register; a real OHCI has a nonzero port count and a frame
     * interval whose bit-period field is close to 11999 (0x2ED3). */
    if (rev == 0xFFu) return 0;
    if ((fminterval & 0xFFFFu) == 0xFFFFu || fminterval == 0u) return 0;
    if ((rh_a & 0x1Fu) == 0u) return 0;   /* NDP == 0 */
    return 1;
}

/* Run the controller for a few frames with its HCCA at `iop_off`, an offset
 * inside IOP RAM, and report what the frame-boundary writeback left there. */
static void hcca_writeback_test(uint32_t iop_off, ps2_ohci_probe_t *p)
{
    volatile uint32_t *h = ps2_iopram_at(iop_off);

    /* The Interrupt Endpoint Table has to stay NULL: FrameNumber is rewritten
     * every frame regardless, but a table full of sentinels would be walked as
     * ED pointers the moment the periodic list is enabled. */
    for (uint32_t i = 0; i < HCCA_INTR_WORDS; i++) h[i] = 0u;
    h[HCCA_FRAME_WORD] = HCCA_SENTINEL;
    h[HCCA_DONE_WORD]  = HCCA_SENTINEL;
    __asm__ volatile("" : : : "memory");

    OHCI_REG(OHCI_REG_HCCA) = iop_off;
    /* HcFMS is the only bit that changes here, and a state change is what puts
     * SOF tokens on the bus: the frame boundary runs from there. */
    OHCI_REG(OHCI_REG_CONTROL) = OHCI_CTRL_HCFS_OPER;

    ohci_wait_us(20000);   /* twenty frame periods */

    p->hcca_frame = h[HCCA_FRAME_WORD] & 0xFFFFu;
    p->hcca_done  = h[HCCA_DONE_WORD];
    p->int_after  = OHCI_REG(OHCI_REG_INTSTATUS);

    /* Left running, and left pointing at this HCCA: the whole state a reset
     * would have to be undone for is one the host engine wants anyway. */
    p->iop_dma_ok = (p->hcca_frame != 0xA5A5u);
}

int ps2_usb_probe(ps2_ohci_probe_t *out)
{
    ps2_ohci_probe_t p;
    for (unsigned i = 0; i < sizeof(p); i++) ((uint8_t *)&p)[i] = 0;

    p.rev        = OHCI_REG(OHCI_REG_REVISION) & 0xFF;
    p.ctrl       = OHCI_REG(OHCI_REG_CONTROL);
    p.cmd        = OHCI_REG(OHCI_REG_CMDSTATUS);
    p.intstatus  = OHCI_REG(OHCI_REG_INTSTATUS);
    p.fminterval = OHCI_REG(OHCI_REG_FMINTERVAL);
    p.rh_a       = OHCI_REG(OHCI_REG_RHDESCRIPTORA);
    p.regs_alive = regs_look_live(p.rev, p.fminterval, p.rh_a);

    /* Disable every interrupt source: the probe polls, and leaving IntMaster
     * enable set would make the controller assert a line nobody services. */
    OHCI_REG(OHCI_REG_INTDISABLE) = 0xFFFFFFFFu;
    p.intenable = OHCI_REG(OHCI_REG_INTENABLE);

    /* No HcCommandStatus reset here, on purpose.  A controller reset also
     * resets the root hub, which zeroes every HcRhPortStatus -- and the connect
     * bit in it is not a bit a driver can put back, only the emulator's own
     * attach callback sets it.  Nothing has touched this controller before us,
     * so the state a reset would ask for (lists off, HCCA null, done queue
     * empty) is the state it came up in, and asking for it by force would cost
     * the devices this probe exists to find. */
    OHCI_REG(OHCI_REG_INTSTATUS) = 0xFFFFFFFFu;   /* acknowledge anything pending */

    /* Read the ports before anything this function does can disturb them, and
     * again after: a pair that comes back equal says the probe left them be. */
    p.port1 = OHCI_REG(OHCI_REG_RHPORT1);
    p.port2 = OHCI_REG(OHCI_REG_RHPORT2);

    /* The PS2-specific enable block, per the Linux ps2 OHCI glue. */
    OHCI_REG(OHCI_REG_PS2_ENABLE) = OHCI_PS2_ENABLE_VALUE;
    ohci_wait_us(1000);
    p.rev_enabled = OHCI_REG(OHCI_REG_REVISION) & 0xFF;

    p.fm_before = OHCI_REG(OHCI_REG_FMNUMBER);
    ohci_wait_us(5000);
    p.fm_after  = OHCI_REG(OHCI_REG_FMNUMBER);
    p.frames_run = (p.fm_after != p.fm_before);

    p.rh_status = OHCI_REG(OHCI_REG_RHSTATUS);
    p.port1_after = OHCI_REG(OHCI_REG_RHPORT1);
    p.port2_after = OHCI_REG(OHCI_REG_RHPORT2);

    p.hcca_iop = ps2_iopram_shadow_off();
    if (p.regs_alive && p.hcca_iop) hcca_writeback_test(p.hcca_iop, &p);

    if (!p.regs_alive)                 s_verdict = PS2_OHCI_NONE;
    else if (p.iop_dma_ok)             s_verdict = PS2_OHCI_IOP_DMA;
    else                               s_verdict = PS2_OHCI_ALIVE;

    s_probe = p;
    if (out) *out = p;
    s_usb_available = (s_verdict == PS2_OHCI_IOP_DMA);
    return s_verdict;
}

int ps2_usb_verdict(void) { return s_verdict; }

const ps2_ohci_probe_t *ps2_usb_last_probe(void) { return &s_probe; }

const char *ps2_usb_verdict_name(int verdict)
{
    switch (verdict) {
    case PS2_OHCI_NONE:    return "registers unreachable from the EE";
    case PS2_OHCI_ALIVE:   return "registers live, no descriptor writeback yet";
    case PS2_OHCI_IOP_DMA: return "OHCI masters IOP RAM the EE can read";
    default:               return "unknown";
    }
}

uint32_t ps2_usb_kbd_reports(void)   { return s_kbd_reports; }
uint32_t ps2_usb_mouse_reports(void) { return s_mouse_reports; }

void ps2_usb_reset_probe(void)
{
    s_usb_available = 0;
    s_shadow_lost = 0;
    s_verdict = PS2_OHCI_NONE;
}

void ps2_usb_init(void)
{
    (void)ps2_usb_probe(&s_probe);
}

int ps2_usb_shadow_lost(void) { return s_shadow_lost; }

/* ── Host engine ─────────────────────────────────────────────────
 *
 * Everything from here down is the part that has to be right for a keystroke to
 * arrive: the descriptors the controller reads for itself, the control
 * transfers that put a device into an addressed and configured state, and the
 * interrupt endpoint that carries the reports.
 *
 * Two rules from OHCI drive the design.  A bus-master address given to this
 * controller is decoded inside IOP RAM, so every descriptor and every payload
 * lives in the one borrowed block and nowhere else.  And the controller only
 * revisits its rings at a frame boundary, so a driver cannot watch a transfer
 * complete -- it queues descriptors, and reads them back afterwards.  That
 * means every wait here is bounded in frames and every failure has to leave the
 * ring consistent enough to try the next one. */

/* Offsets inside the claimed block.  The HCCA has to start 256-byte aligned,
 * which the block itself is, and each descriptor is 16 bytes. */
#define BLK_HCCA          0x000u
#define BLK_ED            0x100u   /* 8 endpoint descriptors */
#define BLK_TD            0x180u   /* 26 transfer descriptors: 10 control + 8 per device */
#define BLK_CTRL_PAY      0x340u   /* 10 x 64 B, one per control TD */
#define BLK_INT_PAY       0x600u   /* 16 x 16 B, eight per device */

#define ED_SLOTS          8u
#define TD_SLOTS          26u
#define CTRL_TD_BASE      0u
#define CTRL_TD_SLOTS     10u
#define INT_TD_BASE       10u      /* the control TDs end here; see the block map above */
#define INT_TD_PER_DEV    8u
#define DEV_SLOTS         2u

/* The control endpoint of the device being addressed right now.  Only one
 * control transfer is in flight, so one endpoint descriptor serves every
 * device: its function address field is rewritten between transfers. */
#define ED_CTRL           0u
#define ED_INTR(dev)      (1u + (uint32_t)(dev))

#define PAY_CTRL(i)       (BLK_CTRL_PAY + (uint32_t)(i) * 64u)
#define PAY_INT(dev, s)   (BLK_INT_PAY + (((uint32_t)(dev) * INT_TD_PER_DEV) + (uint32_t)(s)) * 16u)

/* How deep the interrupt rings go is set by the borrowed block, not by taste:
 * the last 0x100 bytes of it are the canary ps2_iopram_shadow_intact() re-reads
 * to tell us the IOP took the RAM back, so a ring that runs past 0x700 would be
 * reading its own descriptors as evidence that nobody else wrote here. */
#if (BLK_INT_PAY + DEV_SLOTS * INT_TD_PER_DEV * 16u) > PS2_IOP_GUARD_OFF
#error "interrupt TD ring outgrew the borrowed IOP block"
#endif

#define USB_REQ_GET_STATUS      0x00u
#define USB_REQ_SET_ADDRESS     0x05u
#define USB_REQ_GET_DESCRIPTOR  0x06u
#define USB_REQ_SET_CONFIGURATION 0x09u
#define USB_REQ_SET_PROTOCOL    0x0Bu
#define USB_DIR_IN              0x80u
#define USB_RT_DEVICE           0x00u
#define USB_RT_INTERFACE        0x01u
#define USB_DT_DEVICE           0x01u
#define USB_DT_CONFIG           0x02u
#define USB_TYPE_INTERFACE      0x04u
#define USB_TYPE_ENDPOINT       0x05u

#define MPS0_FALLBACK     8u
#define CTRL_WAIT_FRAMES  30u
/* Two is the budget for the host finishing a list it already drained: it needs
 * one frame boundary to notice, and a third would mean the flag is being held
 * by something other than the pass that clears it. */
#define CTRL_QUIET_FRAMES 2u

/* Stores, stores, then the ring pointer that publishes them: the controller can
 * pick the descriptors up at the next frame boundary without any help from us,
 * so the order the compiler leaves them in is the order it sees. */
#define OHCI_SYNC() __asm__ volatile("" : : : "memory")

static uint32_t s_block;                  /* IOP RAM offset of the block, 0 = none */
static int s_host_up;
static ps2_usb_dev_t s_devs[DEV_SLOTS];
static uint32_t s_live_devs;
static uint32_t s_host_us;
static uint32_t s_engine_rearms;   /* transfers that had to restart the frame engine */
static uint32_t s_async_releases;  /* transfers that had to cancel a stranded descriptor */
/* The function address whose device holds the one polling ring it is answered
 * for, or ADDR_UNPOLLED while nothing is armed.  See PS2_USB_STEP_DUPADDR. */
#define ADDR_UNPOLLED   0xFFFFFFFFu
static uint32_t s_polled_addr;

typedef struct {
    /* A circle of INT_TD_PER_DEV descriptors with one permanently unfilled:
     * head equal to tail is how this controller reads an empty endpoint, so the
     * ring holds one less report than it has descriptors for.  `get` is the
     * oldest descriptor the controller may still be holding and `arm` the next
     * free one, with `queued` the count of TDs between them. */
    uint8_t get;
    uint8_t arm;
    uint8_t queued;
    uint32_t burst;   /* most reports one pump pass ever retired: at the ring's
                       * capacity the pump, not the bus, is what sets the rate */
} intr_state_t;
static intr_state_t s_intr[DEV_SLOTS];

/* The block is addressed through the uncached KSEG1 alias of IOP RAM, so a
 * pointer inside it converts back to the byte offset the controller needs by
 * dropping that base -- no table, nothing to keep in sync. */
static volatile void *blk_ptr(uint32_t off)
{
    return (volatile void *)ps2_iopram_at(s_block + off);
}

/* Byte view of the same window: payload buffers are byte objects, and writing
 * one through a uint32_t pointer reaches three bytes it was not asked to. */
static volatile uint8_t *blk_bytes(uint32_t off)
{
    return (volatile uint8_t *)ps2_iopram_at(s_block + off);
}

static uint32_t iop_of(const volatile void *p)
{
    return (uint32_t)((uintptr_t)p - (uintptr_t)PS2_IOP_KSEG1);
}

static volatile ps2_ohci_ed_t *ed_at(uint32_t i)
{
    return (volatile ps2_ohci_ed_t *)blk_ptr(BLK_ED + i * sizeof(ps2_ohci_ed_t));
}

static volatile ps2_ohci_td_t *td_at(uint32_t i)
{
    return (volatile ps2_ohci_td_t *)blk_ptr(BLK_TD + i * sizeof(ps2_ohci_td_t));
}

static uint32_t ed_iop(uint32_t i) { return iop_of(ed_at(i)); }
static uint32_t td_iop(uint32_t i) { return iop_of(td_at(i)); }

/* One past the last TD of a queue: the address the head reaches when the ring
 * has drained. */
static uint32_t td_after(uint32_t base, uint32_t n)
{
    return s_block + BLK_TD + (base + n) * sizeof(ps2_ohci_td_t);
}

static uint32_t td_cc(uint32_t i)
{
    return (td_at(i)->flags >> TD_CC_SHIFT) & 0xFu;
}

/* Whether a descriptor came back from the controller with its buffer fully
 * transferred.  A completed transfer zeroes the buffer pointer and leaves the
 * buffer-end pointer alone, and we never queue a buffer without both. */
static int td_completed(uint32_t i)
{
    volatile ps2_ohci_td_t *td = td_at(i);
    return td->be != 0u && td->cbp == 0u;
}

/* Whether the controller has written a descriptor back at all, given the next
 * pointer we programmed into it.  Retirement replaces that pointer with the head
 * of the done queue, so a word that differs is proof it was touched.
 *
 * Neither of the two obvious signals works on this host: a successful condition
 * code is zero, so it is indistinguishable from a descriptor nobody looked at,
 * and the toggle bit retirement flips is set and then XORed out of the same
 * word, so a completed descriptor comes back with a flags field identical to the
 * one that went in. */
static int td_retired(uint32_t i, uint32_t prog_next)
{
    return td_at(i)->next != prog_next || td_completed(i);
}

/* Four bytes as one number, so a log row says what a payload held without
 * spending four fields on it. */
static uint32_t le32_of(const volatile uint8_t *b)
{
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
           ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

uint32_t ps2_usb_frame_number(void)
{
    return OHCI_REG(OHCI_REG_FMNUMBER);
}

uint32_t ps2_usb_reg(uint32_t off)
{
    return OHCI_REG(off);
}

/* Wait for one frame boundary, the unit in which this controller does anything
 * at all.  Returns 0 if none came inside the budget. */
static int ohci_wait_frame(uint32_t budget_us)
{
    uint32_t fm0 = ps2_usb_frame_number();

    for (uint32_t waited = 0; waited < budget_us; waited += 200u) {
        ohci_wait_us(200u);
        if (ps2_usb_frame_number() != fm0) return 1;
    }
    return 0;
}

/* Wait for a control ring to finish: the last descriptor of the queue coming
 * back with the host's mark on it is the only news that means the transfer
 * moved.
 *
 * The endpoint head reaching the tail used to be taken as the same news, which
 * is what a well-behaved host would give, but it lies here: a transfer the host
 * never delivered to the device can still leave a head that reads as drained,
 * and a SET_ADDRESS believed on that evidence is a device left answering at
 * address zero with every later transfer to its own refused in silence.
 *
 * Returns the frames it took, -1 if the controller declared itself dead, -2 if
 * it never finished. */
static int td_wait_written(uint32_t last, uint32_t prog_next, uint32_t frames)
{
    for (uint32_t f = 1; f <= frames; f++) {
        ohci_wait_us(1050u);
        if (OHCI_REG(OHCI_REG_INTSTATUS) & OHCI_INTR_UE) return -1;
        if (td_retired(last, prog_next)) return (int)f;
    }
    return -2;
}

/* Cancel the descriptor the controller still believes is in flight.
 *
 * A packet the host hands to a device and services later is remembered as the
 * one outstanding packet for the whole controller, and the memory of it
 * outlives the transfer it came from: while it is held, every descriptor on
 * every list is turned away before the device is even looked at -- no delivery,
 * no writeback, no condition code, nothing in any register but the list-filled
 * flag that never clears.  Nothing reaches the bus again after that, on any
 * port.
 *
 * The one transition that clears the record without disturbing the root hub is
 * spending a frame with the control list disabled.  A functional-state RESET
 * clears it too and would also zero HcRhPortStatus, whose connect bit is one no
 * driver write can put back. */
static void ohci_release_async(void)
{
    uint32_t ctl = OHCI_REG(OHCI_REG_CONTROL);

    if (!(ctl & OHCI_CTRL_CLE)) return;
    s_async_releases++;
    OHCI_REG(OHCI_REG_CONTROL) = ctl & ~OHCI_CTRL_CLE;
    ohci_wait_frame(4000u);
    OHCI_REG(OHCI_REG_CONTROL) = ctl | OHCI_CTRL_CLE;
}

/* Whether the control list is still flagged full, which is the host's own words
 * for "this list is not finished".  The flag is ours to set and the host's to
 * clear, and it clears only when a pass walks the list to its end, so a flag
 * that outlives a transfer names a transfer that never came back -- either a
 * descriptor still waiting on the bus or one whose packet the host still holds.
 * Both need the same cure, and neither shows up in descriptor memory: a refused
 * descriptor is left exactly as it was written. */
static int ctrl_list_held(void)
{
    return (OHCI_REG(OHCI_REG_CMDSTATUS) & OHCI_CMD_CLF) != 0u;
}

/* Wait for the host to stop looking at the control list.
 *
 * A drained ring is not that news.  The list-filled flag clears on the pass
 * *after* the last descriptor retires -- and a driver cannot clear it itself,
 * the register only ever ORs the bit in -- so between those two frames the
 * controller is still reading the descriptors we are about to rewrite.  Every
 * queueing driver's contract is the same: publish a ring only where the host
 * has said it is finished with the memory.
 *
 * Returns the frames it took, or -1 when the flag outlived the budget. */
static int ctrl_list_quiet(uint32_t frames)
{
    uint32_t f;

    for (f = 1; f <= frames; f++) {
        if (!ctrl_list_held()) return (int)f;
        ohci_wait_us(1050u);
    }
    return -1;
}

/* Restart the frame engine after a transfer that saw no frames at all.
 *
 * A DMA error inside this controller stops it taking frame boundaries entirely:
 * the error handler clears the end-of-frame timer, and the only thing that puts
 * it back is a write to HcControl that *transitions* the functional state into
 * operational -- rewriting the value already there is a no-op.  So the state a
 * driver can reach is a controller that is switched on, answers register reads,
 * and never looks at a descriptor again, with nothing in any register saying
 * so.  Cycling SUSPEND -> OPERATIONAL recovers it.
 *
 * SUSPEND and not RESET: the functional state RESET resets the root hub, which
 * clears HcRhPortStatus including the connect bit, and the connect bit is one
 * no driver write can set back. */
static void ohci_rearm_engine(void)
{
    OHCI_REG(OHCI_REG_INTSTATUS) = 0xFFFFFFFFu;       /* the fatal flag among them */
    OHCI_REG(OHCI_REG_CONTROL) = OHCI_CTRL_CLE | OHCI_CTRL_PLE |
                                 OHCI_CTRL_HCFS_SUSPEND;
    ohci_wait_us(2000u);
    OHCI_REG(OHCI_REG_CONTROL) = OHCI_CTRL_CLE | OHCI_CTRL_PLE |
                                 OHCI_CTRL_HCFS_OPER;
    ohci_wait_us(2000u);
}

/* Clear the halt an error leaves on an endpoint, so the next transfer starts
 * from a ring the controller will look at again. */
static void ed_unhalt(volatile ps2_ohci_ed_t *ed)
{
    if (ed->head & ED_H) {
        ed->head &= ~ED_H;
        OHCI_SYNC();
    }
}

/* Which stage of a control ring the controller never came back from: 1 is the
 * SETUP, 2 .. n-1 the data stage, n the status stage, 0 a ring it wrote all the
 * way through.  Read off the descriptors rather than the endpoint head, which
 * this controller writes back from a copy made before the transfer and so loses
 * whenever one pass finishes inside another. */
static uint32_t td_stuck_index(uint32_t ntd, uint32_t tail)
{
    for (uint32_t i = 0; i < ntd; i++) {
        uint32_t prog = (i + 1u < ntd) ? td_iop(CTRL_TD_BASE + i + 1u) : tail;
        if (!td_retired(CTRL_TD_BASE + i, prog)) return i + 1u;
    }
    return 0;
}

/* Run one control transfer on endpoint zero and return how many bytes came
 * back, or a negative code: -1 controller dead, -2 out of frames, -3 the ring
 * was too small.  Every transfer leaves its measurements on the device row --
 * the TD it stopped on, what the drain wait returned, and how many frames the
 * controller advanced while we waited -- because on this host the only way to
 * tell a device that never answers from a controller that never polls is to
 * have looked at both.  `tag` says which step of enumeration this was. */
static int ctrl_transfer(ps2_usb_dev_t *d, uint32_t func_addr, uint32_t mps0,
                         uint8_t req_type, uint8_t request, uint32_t value,
                         uint32_t index, uint32_t len, volatile uint8_t *data,
                         uint32_t tag)
{
    volatile ps2_ohci_ed_t *ed = ed_at(ED_CTRL);
    uint32_t ntd, chunk, i, tail, fm0, worst = 0, svc = 0;
    uint32_t dir_data = (req_type & USB_DIR_IN) ? TD_DIR_IN : TD_DIR_OUT;
    uint32_t dir_status = (req_type & USB_DIR_IN) ? TD_DIR_OUT : TD_DIR_IN;
    volatile uint8_t *setup;
    int r, quiet;

    /* SETUP, then one TD per max-packet of the data stage -- a TD may not
     * straddle a packet boundary -- then the zero-length status stage in the
     * other direction. */
    ntd = 1u + (len ? (len + mps0 - 1u) / mps0 : 0u) + 1u;
    if (ntd > CTRL_TD_SLOTS) return -3;

    /* Cancel the previous transfer's unfinished business before queueing this
     * one: while the host holds a packet, it turns every descriptor on every
     * list away at the door -- no delivery, no writeback, no error recorded. */
    quiet = ctrl_list_quiet(CTRL_QUIET_FRAMES);
    if (quiet < 0) {
        ohci_release_async();
        quiet = ctrl_list_quiet(CTRL_QUIET_FRAMES);
    }

    setup = blk_bytes(PAY_CTRL(0));
    setup[0] = req_type;
    setup[1] = request;
    setup[2] = value & 0xFFu;
    setup[3] = (value >> 8) & 0xFFu;
    setup[4] = index & 0xFFu;
    setup[5] = (index >> 8) & 0xFFu;
    setup[6] = len & 0xFFu;
    setup[7] = (len >> 8) & 0xFFu;

    /* The list terminator is one past the whole descriptor pool rather than one
     * past this queue, because a retiring descriptor comes back holding the head
     * of the done queue -- a descriptor address, or zero.  A terminator that is
     * itself some descriptor's address can therefore be written into a retired
     * descriptor by the host and read back as the pointer we left there, which
     * is a transfer believed finished that never started. */
    tail = td_after(CTRL_TD_BASE, TD_SLOTS);
    for (i = 0; i < ntd; i++) {
        volatile ps2_ohci_td_t *td = td_at(CTRL_TD_BASE + i);
        uint32_t dir = TD_DIR_SETUP, pay = 0, blen = 0;

        if (i == 0) {
            dir = TD_DIR_SETUP;
            pay = PAY_CTRL(0);
            blen = 8u;
        } else if (i < ntd - 1u) {
            uint32_t off = (i - 1u) * mps0;

            dir = dir_data;
            pay = PAY_CTRL(i);
            chunk = len - off;
            blen = chunk < mps0 ? chunk : mps0;
            /* A read's stage starts at zero, because the only way to read "the
             * host copied these bytes in" is to have had nothing there for it to
             * copy; a write's stage carries the payload for the host to copy out. */
            for (uint32_t j = 0; j < blen; j++)
                blk_bytes(pay)[j] = (dir_data == TD_DIR_IN) ? 0u : data[off + j];
        } else {
            /* The status stage is an empty packet the other way round from the
             * data stage: in for a write, out for a read.  Getting that backways
             * is not cosmetic -- a SETUP followed by an empty OUT is retired by
             * this controller with a clean condition code and never handed to the
             * device at all, which is how every device on this bus went on
             * answering address zero while the driver queued writes that way. */
            dir = dir_status;
        }

        td->flags = (dir << TD_DP_SHIFT) | (dir == TD_DIR_IN ? TD_R : 0u);
        td->cbp   = blen ? s_block + pay : 0u;
        td->be    = blen ? s_block + pay + blen - 1u : 0u;
        td->next  = (i + 1u < ntd) ? td_iop(CTRL_TD_BASE + i + 1u) : tail;
    }

    ed->flags = ((func_addr & 0x7Fu) << ED_FA_SHIFT) |
                (d->low_speed ? ED_S : 0u) |
                ((mps0 & 0x7Fu) << ED_MPS_SHIFT);
    ed->head  = td_iop(CTRL_TD_BASE);
    ed->tail  = tail;
    ed->next  = ed_iop(ED_CTRL);     /* the async list is cyclic: this one is a ring of one */
    OHCI_SYNC();

    OHCI_REG(OHCI_REG_INTSTATUS) = OHCI_INTR_UE;      /* drop a stale fatal flag */
    OHCI_REG(OHCI_REG_CTRL_HEAD) = ed_iop(ED_CTRL);
    OHCI_REG(OHCI_REG_CMDSTATUS) = OHCI_CMD_CLF;

    fm0 = ps2_usb_frame_number();
    r = td_wait_written(CTRL_TD_BASE + ntd - 1u, tail, CTRL_WAIT_FRAMES);
    for (i = 0; i < ntd; i++) {
        uint32_t cc = td_cc(CTRL_TD_BASE + i);
        if (cc && !worst) worst = cc;
        /* The buffer pointer is the one word of a descriptor that says whether
         * the host moved the bytes: it zeroes it and only on a full transfer,
         * and we are the only other party that ever writes it.  Unlike the next
         * pointer, no bookkeeping of ours can leave it in this state by
         * accident -- and unlike the condition code, which a host leaves at
         * zero for a descriptor nobody touched, it cannot read as success by
         * default. */
        if (td_completed(CTRL_TD_BASE + i)) svc |= 1u << i;
    }
    d->frames   = ps2_usb_frame_number() - fm0;
    d->wait_ret = r;
    d->stuck_td = td_stuck_index(ntd, tail);
    if (worst) d->cc = worst;
    {
        ps2_usb_tx_t tx;
        /* head is the host's own writeback of where its walk stopped, and it
         * reaches one past this transfer's last descriptor when the ring
         * drained, so the pair says whether a transfer was never started or
         * started and then refused. */
        tx.port   = d->port;
        tx.tag    = tag;
        tx.req    = ((uint32_t)req_type << 8) | request;
        tx.ntd    = ntd;
        tx.result = r;
        tx.head   = ed->head;
        tx.done   = OHCI_REG(OHCI_REG_DONE_HEAD);
        tx.stuck  = d->stuck_td;
        tx.cc     = worst;
        tx.svc    = svc;
        tx.data0  = (ntd > 2u && (req_type & USB_DIR_IN))
                        ? le32_of(blk_bytes(PAY_CTRL(1))) : 0u;
        tx.quiet  = quiet;
        ps2_usb_log_tx(&tx);
    }
    ed_unhalt(ed);

    if (r < 0) {
        /* A wait that saw no frames is a controller that stopped looking at
         * memory, not a device that stopped talking: put the frame engine back
         * so the next attempt measures the device rather than the freeze. */
        if (d->frames == 0u || r == -1) {
            s_engine_rearms++;
            ohci_rearm_engine();
        }
        return r;
    }
    if (worst) return (int)(worst + 2u);

    if ((req_type & USB_DIR_IN) && data) {
        /* The data stage is TDs 1 .. ntd-2; the TD after them is the status
         * stage and has no buffer behind it. */
        for (i = 0; i + 2u < ntd; i++) {
            volatile uint8_t *src = blk_bytes(PAY_CTRL(i + 1u));
            uint32_t off = i * mps0;
            chunk = len - off;
            if (chunk > mps0) chunk = mps0;
            for (uint32_t j = 0; j < chunk; j++) data[off + j] = src[j];
        }
    }
    return (int)len;
}

/* bLength/bDescriptorType checked, then walk the configuration descriptor for
 * the first interrupt IN endpoint and the interface protocol that says whether
 * this is the keyboard. */
static int cfg_parse(const uint8_t *c, uint32_t n, ps2_usb_dev_t *d)
{
    uint32_t i;
    int ep_found = 0;

    if (n < 9u || c[0] != 9u || c[1] != USB_DT_CONFIG) return 0;
    for (i = 0; i + 2u <= n; i += c[i]) {
        uint32_t bl = c[i];
        if (bl < 2u) break;
        if (c[i + 1] == USB_TYPE_INTERFACE && bl >= 9u) {
            /* HID boot protocol: subclass 1, protocol 1 keyboard / 2 mouse. */
            d->is_keyboard = (c[i + 5] == 3u && c[i + 6] == 1u && c[i + 7] == 1u);
        }
        if (c[i + 1] == USB_TYPE_ENDPOINT && bl >= 7u && (c[i + 2] & 0x80u) &&
            (c[i + 3] & 3u) == 3u) {
            d->ep       = c[i + 2] & 0x0Fu;
            d->mps      = c[i + 4] | ((uint32_t)c[i + 5] << 8);
            d->interval = c[i + 6];
            ep_found = 1;
            break;
        }
    }
    return ep_found;
}

/* One descriptor of a device's interrupt ring, by its position in the circle. */
static uint32_t int_td_iop(uint32_t dev, uint32_t slot)
{
    return td_iop(INT_TD_BASE + dev * INT_TD_PER_DEV + slot);
}

/* Fill the next free descriptor of a device's interrupt ring, one report deep.
 *
 * Only the tail is ours to write, and only into the slot the controller is not
 * holding, so this can run while a transfer is in flight: the descriptor it
 * publishes is the one the host walks to after the last one it was already
 * given. */
static void intr_arm(uint32_t dev)
{
    intr_state_t *q = &s_intr[dev];
    uint32_t nxt = int_td_iop(dev, (q->arm + 1u) % INT_TD_PER_DEV);
    volatile ps2_ohci_td_t *td = td_at(INT_TD_BASE + dev * INT_TD_PER_DEV + q->arm);
    ps2_usb_dev_t *d = &s_devs[dev];

    td->flags = (TD_DIR_IN << TD_DP_SHIFT) | TD_R;
    td->cbp   = s_block + PAY_INT(dev, q->arm);
    td->be    = td->cbp + (d->mps ? d->mps - 1u : 7u);
    td->next  = nxt;
    ed_at(ED_INTR(dev))->tail = nxt;
    OHCI_SYNC();
    q->arm = (uint8_t)((q->arm + 1u) % INT_TD_PER_DEV);
    q->queued++;
}

/* Keep the ring as full as it is allowed to be.  Each armed descriptor is one
 * more frame of polling the controller can do on its own, which is the only way
 * a report can be waiting for us rather than having to be asked for: an
 * interrupt endpoint is visited once per frame, so the depth of this ring is how
 * many milliseconds of hand jitter the port absorbs before it starts losing
 * motion. */
static void intr_top_up(uint32_t dev)
{
    intr_state_t *q = &s_intr[dev];

    while (q->queued < INT_TD_PER_DEV - 1u) intr_arm(dev);
}

/* Build the endpoint descriptor for a device's interrupt IN endpoint.  The
 * controller cannot see it until the periodic table names it. */
static void intr_ed_init(uint32_t dev)
{
    volatile ps2_ohci_ed_t *ed = ed_at(ED_INTR(dev));
    ps2_usb_dev_t *d = &s_devs[dev];

    ed->flags = ((d->addr & 0x7Fu) << ED_FA_SHIFT) |
                ((d->ep & 0xFu) << ED_EN_SHIFT) |
                (TD_DIR_IN << ED_D_SHIFT) |
                (d->low_speed ? ED_S : 0u) |
                (((d->mps & 0x7FFu)) << ED_MPS_SHIFT);
    ed->head = int_td_iop(dev, 0);
    ed->tail = ed->head;
    ed->next = 0;                    /* the periodic list is terminated, not cyclic */
    OHCI_SYNC();

    s_intr[dev].get = 0;
    s_intr[dev].arm = 0;
    s_intr[dev].queued = 0;
    d->step = PS2_USB_STEP_POLLING;
    d->live = 1;
}

/* Publish every armed device once per frame.  Both devices ask to be polled
 * every 10 ms, and the controller costs nothing for a poll it answers "not
 * ready", so the rings go in every slot and a report is seen within a
 * millisecond of the device having it. */
static void periodic_install(void)
{
    volatile uint32_t *h = (volatile uint32_t *)blk_ptr(BLK_HCCA);
    uint32_t head_ed = 0;
    uint32_t prev_dev = 0xFFFFFFFFu;

    for (uint32_t i = 0; i < DEV_SLOTS; i++) {
        if (!s_devs[i].live) continue;
        if (!head_ed) head_ed = ed_iop(ED_INTR(i));
        if (prev_dev != 0xFFFFFFFFu) ed_at(ED_INTR(prev_dev))->next = ed_iop(ED_INTR(i));
        prev_dev = i;
    }
    /* Terminate whatever the loop last touched: an unarmed ED's next is already
     * zero, and a chain that ran off the end of it would be walked as garbage. */
    if (prev_dev != 0xFFFFFFFFu) ed_at(ED_INTR(prev_dev))->next = 0;

    for (uint32_t n = 0; n < HCCA_INTR_WORDS; n++) h[n] = head_ed;
    OHCI_SYNC();
}

/* Bring one root hub port up: reset it, read whatever descriptor answers, give
 * the device a function address, and arm its interrupt endpoint.
 *
 * The reset belongs here rather than in a pass of its own because the host
 * resolves a function address across whichever ports are enabled and takes the
 * first one it reaches.  Enabling every port before asking any of them a
 * question therefore leaves the later ports answering for the device that was
 * attached first -- which is what one port at a time avoids. */
static void enumerate_port(uint32_t dev, uint32_t port)
{
    uint8_t desc[18];
    uint8_t cfg[80];
    ps2_usb_dev_t *d = &s_devs[dev];
    uint32_t mps0, total, n;
    int r;

    OHCI_REG(OHCI_REG_RHPORT1 + (port - 1u) * 4u) = OHCI_PORT_PRS;
    ohci_wait_us(20000u);                        /* spec: 10 ms reset, then recovery */
    OHCI_REG(OHCI_REG_RHPORT1 + (port - 1u) * 4u) = OHCI_PORT_PRSC;
    d->port_status = OHCI_REG(OHCI_REG_RHPORT1 + (port - 1u) * 4u);
    d->low_speed = (d->port_status & OHCI_PORT_LSDA) != 0;
    if (!(d->port_status & OHCI_PORT_PES)) return;
    d->step = PS2_USB_STEP_RESET;

    /* Zeroed because d0 below is one of the numbers that reads an enumeration
     * failure apart, and stack left over from the transfer before is the
     * alternative to a device that answered nothing. */
    for (n = 0; n < sizeof(desc); n++) desc[n] = 0u;
    for (n = 0; n < sizeof(cfg); n++) cfg[n] = 0u;

    /* The first eight bytes of the device descriptor are the whole packet a
     * freshly attached device is guaranteed to answer, and the eighth is the
     * endpoint-zero size every later transfer has to be chunked by. */
    r = ctrl_transfer(d, 0u, MPS0_FALLBACK, USB_DIR_IN,
                      USB_REQ_GET_DESCRIPTOR, (uint32_t)USB_DT_DEVICE << 8,
                      0u, 8u, desc, PS2_TX_PLAIN);
    /* Its first eight bytes are on the [TX] row of this transfer either way, so
     * nothing here records them again; this return is only about whether the
     * data stage landed. */
    if (r < 8) return;
    mps0 = desc[7] ? desc[7] : MPS0_FALLBACK;
    d->step = PS2_USB_STEP_DESC0;

    /* Whether the address took is asked of the device, not of the controller.
     * The same descriptor is read twice, the payload zeroed before each, and the
     * read that brings bytes back names the address the device answers.  Nothing
     * in a descriptor header can give that news: a host that retires a
     * descriptor without ever handing it to the device leaves it looking exactly
     * as finished as one that transferred, which is how a SET_ADDRESS that went
     * nowhere read as success.
     *
     * Both reads ask for the whole 18 bytes rather than the eight the first read
     * needed, because the vendor and product words are the only thing that says
     * *which* device answered.  Every HID device opens a device descriptor the
     * same way, so the eight bytes above cannot tell a keyboard from a pointer. */
    if (ctrl_transfer(d, 0u, mps0, 0u, USB_REQ_SET_ADDRESS,
                      d->addr, 0u, 0u, 0, PS2_TX_SETADDR) < 0) return;
    for (n = 0; n < sizeof(desc); n++) desc[n] = 0u;
    r = ctrl_transfer(d, d->addr, mps0, USB_DIR_IN, USB_REQ_GET_DESCRIPTOR,
                      (uint32_t)USB_DT_DEVICE << 8, 0u, sizeof(desc), desc,
                      PS2_TX_READ_ADDR);
    if (r >= 8) {
        d->step = PS2_USB_STEP_ADDRESS;
        d->desc_id = (r >= 12) ? le32_of(desc + 8) : 0u;
    } else {
        /* Nothing answers at the address we just asked for.  Ask at zero: bytes
         * from there mean a device that is alive and simply never took the
         * address, and enumeration has to continue at zero saying so. */
        for (n = 0; n < sizeof(desc); n++) desc[n] = 0u;
        r = ctrl_transfer(d, 0u, mps0, USB_DIR_IN, USB_REQ_GET_DESCRIPTOR,
                          (uint32_t)USB_DT_DEVICE << 8, 0u, sizeof(desc), desc,
                          PS2_TX_READ_AT0);
        if (r < 8) return;
        d->desc_id = (r >= 12) ? le32_of(desc + 8) : 0u;
        d->addr = 0u;
    }

    /* Configuration: the header first, because it carries wTotalLength, then
     * exactly that much.  Asking for the precise length is what keeps every
     * data-stage packet whole -- a short packet mid-transfer would strand the
     * TDs queued behind it. */
    if (ctrl_transfer(d, d->addr, mps0, USB_DIR_IN,
                      USB_REQ_GET_DESCRIPTOR, (uint32_t)USB_DT_CONFIG << 8,
                      0u, 8u, cfg, PS2_TX_PLAIN) < 8) return;
    total = cfg[2] | ((uint32_t)cfg[3] << 8);
    if (!total || total > sizeof(cfg)) { d->cc = CC_DATAOVERRUN; return; }
    n = (uint32_t)ctrl_transfer(d, d->addr, mps0, USB_DIR_IN,
                                USB_REQ_GET_DESCRIPTOR, (uint32_t)USB_DT_CONFIG << 8,
                                0u, total, cfg, PS2_TX_PLAIN);
    if (n != total || !cfg_parse(cfg, n, d)) return;
    d->step = PS2_USB_STEP_CONFIG;

    if (ctrl_transfer(d, d->addr, mps0, 0u, USB_REQ_SET_CONFIGURATION,
                      1u, 0u, 0u, 0, PS2_TX_PLAIN) < 0) return;
    /* Boot protocol: the 8-byte keyboard and 4-byte mouse reports the decoders
     * below already speak.  Class request, recipient is the interface. */
    if (ctrl_transfer(d, d->addr, mps0, 0x21u, USB_REQ_SET_PROTOCOL,
                      0u, 0u, 0u, 0, PS2_TX_PLAIN) < 0) return;
    d->step = PS2_USB_STEP_CONFIGURED;

    /* One polling ring per function address.  This host never applies
     * SET_ADDRESS, so every device ends up here at address zero, and its own
     * lookup takes the first enabled port holding the asked-for address: a
     * second ring at the same address would be answered by the first device,
     * which looks exactly like a live second port while telling you nothing
     * about the device actually on it. */
    if (d->addr == s_polled_addr) { d->step = PS2_USB_STEP_DUPADDR; return; }
    s_polled_addr = d->addr;

    /* The last control transfer can leave the host holding a packet it has
     * already serviced, and while it holds one no descriptor on any list gets
     * looked at -- including the polling ring about to be queued. */
    if (ctrl_list_quiet(CTRL_QUIET_FRAMES) < 0) {
        ohci_release_async();
        ctrl_list_quiet(CTRL_QUIET_FRAMES);
    }

    intr_ed_init(dev);
    intr_top_up(dev);
}

int ps2_usb_host_start(void)
{
    volatile uint32_t *h;
    uint32_t ndp;

    if (s_verdict != PS2_OHCI_IOP_DMA) return 0;
    s_block = ps2_iopram_shadow_off();
    if (!s_block) return 0;

    for (unsigned i = 0; i < sizeof(s_devs); i++) ((uint8_t *)s_devs)[i] = 0;
    for (unsigned i = 0; i < sizeof(s_intr); i++) ((uint8_t *)s_intr)[i] = 0;
    s_live_devs = 0;
    s_engine_rearms = 0;
    s_async_releases = 0;
    s_polled_addr = ADDR_UNPOLLED;

    /* The frame table must read NULL before the periodic list is let loose: a
     * half-built ED there would be walked as a pointer. */
    h = (volatile uint32_t *)blk_ptr(BLK_HCCA);
    for (uint32_t i = 0; i < HCCA_INTR_WORDS; i++) h[i] = 0u;
    h[HCCA_FRAME_WORD] = 0u;
    h[HCCA_DONE_WORD]  = 0u;
    /* The control endpoint starts with a head that names no descriptor at all,
     * so the first transfer's stranded-packet check cannot read whatever the
     * visibility probe left in the block. */
    ed_at(ED_CTRL)->head = 0u;
    ed_at(ED_CTRL)->next = ed_iop(ED_CTRL);
    OHCI_SYNC();

    OHCI_REG(OHCI_REG_HCCA) = s_block + BLK_HCCA;
    OHCI_REG(OHCI_REG_CONTROL) = OHCI_CTRL_CLE | OHCI_CTRL_PLE | OHCI_CTRL_HCFS_OPER;
    ohci_wait_us(2000u);
    s_host_up = 1;

    uint32_t t0 = ps2_count_read();
    ndp = s_probe.rh_a & 0x1Fu;
    if (ndp > DEV_SLOTS) ndp = DEV_SLOTS;
    for (uint32_t port = 1; port <= ndp; port++) {
        uint32_t dev = port - 1u;
        if (dev >= DEV_SLOTS) break;

        /* Every port gets tried, not only the ones reporting a connection: the
         * connect bit is the emulator's to set, and a controller reset drops it
         * until the attach callback runs again. */
        s_devs[dev].port = port;
        s_devs[dev].addr = dev + 1u;
        s_devs[dev].port_status = OHCI_REG(OHCI_REG_RHPORT1 + (port - 1u) * 4u);
        enumerate_port(dev, port);
        if (s_devs[dev].live) s_live_devs++;
    }

    periodic_install();

    /* Enumeration is a sequence of waits for a controller that only moves at a
     * frame boundary, so its cost is worth naming on screen: a device that does
     * not answer is only found out after the full frame budget of every
     * transfer it was asked about. */
    {
        uint32_t dt = (uint32_t)(ps2_count_read() - t0);
        s_host_us = count_running() ? dt / EE_COUNTS_PER_US : 0u;
    }
    return (int)s_live_devs;
}

int ps2_usb_host_up(void) { return s_host_up; }

const ps2_usb_dev_t *ps2_usb_dev(int index)
{
    if (index < 0 || index >= (int)DEV_SLOTS) return 0;
    if (!s_devs[index].port) return 0;
    return &s_devs[index];
}

uint32_t ps2_usb_host_us(void) { return s_host_us; }
uint32_t ps2_usb_engine_rearms(void) { return s_engine_rearms; }
uint32_t ps2_usb_async_releases(void) { return s_async_releases; }

/* One device's periodic queue, word by word, for the row that has to tell apart
 * the two ways a poll loop goes quiet without an error: the controller stopped
 * walking the list, which shows in the endpoint's own head and tail, or it
 * walked the descriptor and was refused, which shows in the descriptor's
 * condition code.  Both are read out of the memory the controller shares rather
 * than out of our bookkeeping, which is the only part of this that can lie. */
uint32_t ps2_usb_intr_word(int index, int which)
{
    uint32_t slot, td_i;

    if (index < 0 || index >= (int)DEV_SLOTS) return 0u;
    slot  = s_intr[index].get;          /* the head of our side: the next report due */
    td_i  = INT_TD_BASE + (uint32_t)index * INT_TD_PER_DEV + slot;

    switch (which) {
    case INTR_W_ED_HEAD:  return ed_at(ED_INTR(index))->head;
    case INTR_W_ED_TAIL:  return ed_at(ED_INTR(index))->tail;
    case INTR_W_TD_IOP:   return td_iop(td_i);
    case INTR_W_TD_NEXT:  return td_at(td_i)->next;
    case INTR_W_TD_CC:    return td_cc(td_i);
    case INTR_W_TD_CBP:   return td_at(td_i)->cbp;
    case INTR_W_PENDING:  return (uint32_t)s_intr[index].queued;
    case INTR_W_SLOT:     return slot;
    case INTR_W_ARM:      return (uint32_t)s_intr[index].arm;
    case INTR_W_BURST:    return s_intr[index].burst;
    default:              return 0u;
    }
}

/* Collect one report if the controller has retired the descriptor at the head of
 * the ring, and say whether it did.  A descriptor the device answered "not
 * ready" to is still queued and still head, so nothing happens until it
 * completes -- that is the poll, and it costs this loop a comparison. */
static int intr_collect(uint32_t dev)
{
    volatile ps2_ohci_ed_t *ed = ed_at(ED_INTR(dev));
    ps2_usb_dev_t *d = &s_devs[dev];
    intr_state_t *q = &s_intr[dev];
    uint32_t cc, cur;
    uint8_t report[8];

    if (!q->queued) return 0;
    cur = INT_TD_BASE + dev * INT_TD_PER_DEV + q->get;

    /* A descriptor the device answered "not ready" to is still queued with the
     * words we left in it, so this comparison is the whole poll.  Waiting for the
     * endpoint head to reach the tail instead would read a report that never
     * arrived: the head this controller writes back can be the one from before
     * the transfer. */
    if (!td_retired(cur, int_td_iop(dev, (q->get + 1u) % INT_TD_PER_DEV)))
        return 0;

    cc = td_cc(cur);
    d->polls++;
    if (cc) {
        d->errors++;
        d->cc = cc;
        if (d->errors > 8u) d->live = 0;      /* give the endpoint up on */
    } else {
        volatile uint8_t *src = blk_bytes(PAY_INT(dev, q->get));
        for (uint32_t i = 0; i < 8; i++) report[i] = src[i];
    }

    /* The ring goes back out before the report goes anywhere.  That order is the
     * whole difference between a poll and a stop: the consumer of a keystroke is
     * the shell, a shell command runs the desktop session, and the session's own
     * event loop pumps this function.  Left unarmed across that call, the
     * controller has nothing queued -- head equals tail, no condition code, no
     * error anywhere -- and the nested loop can only ask it again, so every key
     * pressed inside the GUI is a token nobody sent.  The report is already in a
     * local by then, and the descriptors re-armed here are ones the controller
     * has not walked, so nothing the consumer does can look at bytes the host is
     * writing. */
    q->get = (uint8_t)((q->get + 1u) % INT_TD_PER_DEV);
    q->queued--;
    ed_unhalt(ed);
    if (d->live) intr_top_up(dev);
    if (cc) return 1;

    if (d->is_keyboard || d->mps >= 8u) ps2_usb_process_keyboard_report(report);
    else ps2_usb_process_mouse_report(report);
    return 1;
}

/* Called from the shell's input pump.  The controller has already moved the
 * reports into the block at its own frame boundaries; what happens here is
 * collecting them, which is why this has to run between keystrokes rather than
 * only at boot. */
void ps2_usb_poll(void)
{
    if (!s_usb_available) return;

    /* The shadow block is IOP RAM we borrowed from an idle patch of it; if the
     * IOP ever allocates it back, the descriptors we are walking stop being
     * ours and the only honest response is to stop touching them. */
    if (!ps2_iopram_shadow_intact()) {
        s_shadow_lost = 1;
        s_usb_available = 0;
        s_host_up = 0;
        return;
    }
    if (!s_host_up) return;

    for (uint32_t i = 0; i < DEV_SLOTS; i++) {
        uint32_t got = 0;

        if (!s_devs[i].live) continue;

        /* Drain the ring instead of taking one report from it.  The controller
         * retires at most one descriptor per endpoint per frame, so everything
         * that piled up while the desktop was painting is motion the device has
         * already forgotten by the time we get to it: an HID pointer keeps a few
         * reports of its own and drops the older ones when they run out, and a
         * dropped relative report is movement that never happens.  One report per
         * pass therefore makes this pump, not the bus, the rate the pointer
         * moves at. */
        while (intr_collect(i)) {
            got++;
            if (got > s_intr[i].burst) s_intr[i].burst = got;
        }
        if (got) ps2_usb_log_ring(i, got, s_intr[i].burst, ps2_usb_frame_number());
    }

    /* Announced once, and only once, because it is the one row that can prove
     * the periodic list moves without anyone being able to type: the boot log's
     * report counters are printed before a frame has passed for a device to
     * answer in, so on a machine with no keyboard bound they read zero no matter
     * what the polling ring does afterwards. */
    if (!s_report_seen && (s_kbd_reports || s_mouse_reports)) {
        s_report_seen = 1;
        ps2_usb_report_landed(s_kbd_reports, s_mouse_reports);
    }
}

uint32_t ps2_usb_hid_to_btron_key(uint8_t mod, uint8_t code)
{
    int shift = (mod & 0x22) != 0; /* Left Shift (0x02) or Right Shift (0x20) */

    /* Letters: a - z / A - Z */
    if (code >= 0x04 && code <= 0x1D) {
        int is_upper = shift ^ s_caps_lock;
        char base = is_upper ? 'A' : 'a';
        return (uint32_t)(base + (code - 0x04));
    }

    /* Top Row Numbers: 1 - 9 */
    if (code >= 0x1E && code <= 0x26) {
        const char *num = "123456789";
        const char *sym = "!@#$%^&*(";
        return (uint32_t)(shift ? sym[code - 0x1E] : num[code - 0x1E]);
    }

    /* Top Row 0 */
    if (code == 0x27) return shift ? ')' : '0';

    /* Whitespace and basic editing */
    if (code == 0x28) return 0x0A;  /* Return / Enter */
    if (code == 0x29) return 0x1B;  /* Escape */
    if (code == 0x2A) return 0x08;  /* Backspace */
    if (code == 0x2B) return 0x09;  /* Tab */
    if (code == 0x2C) return ' ';   /* Space */

    /* Punctuation */
    if (code == 0x2D) return shift ? '_' : '-';
    if (code == 0x2E) return shift ? '+' : '=';
    if (code == 0x2F) return shift ? '{' : '[';
    if (code == 0x30) return shift ? '}' : ']';
    if (code == 0x31) return shift ? '|' : '\\';
    if (code == 0x33) return shift ? ':' : ';';
    if (code == 0x34) return shift ? '"' : '\'';
    if (code == 0x35) return shift ? '~' : '`';
    if (code == 0x36) return shift ? '<' : ',';
    if (code == 0x37) return shift ? '>' : '.';
    if (code == 0x38) return shift ? '?' : '/';

    /* Caps Lock Toggle */
    if (code == 0x39) {
        s_caps_lock = !s_caps_lock;
        return 0;
    }

    /* Function Keys F1 - F12 */
    if (code >= 0x3A && code <= 0x45) {
        return BTRON_KEY_F1 + (code - 0x3A);
    }

    /* Navigation & Editing */
    if (code == 0x49) return BTRON_KEY_INSERT;
    if (code == 0x4A) return BTRON_KEY_HOME;
    if (code == 0x4B) return BTRON_KEY_PGUP;
    if (code == 0x4C) return BTRON_KEY_DELETE;
    if (code == 0x4D) return BTRON_KEY_END;
    if (code == 0x4E) return BTRON_KEY_PGDN;

    /* Cursor Arrows */
    if (code == 0x4F) return BTRON_KEY_RIGHT;
    if (code == 0x50) return BTRON_KEY_LEFT;
    if (code == 0x51) return BTRON_KEY_DOWN;
    if (code == 0x52) return BTRON_KEY_UP;

    /* Keypad Operators */
    if (code == 0x54) return '/';
    if (code == 0x55) return '*';
    if (code == 0x56) return '-';
    if (code == 0x57) return '+';
    if (code == 0x58) return 0x0A; /* Keypad Enter */

    /* Keypad Numbers */
    if (code >= 0x59 && code <= 0x61) {
        return (uint32_t)('1' + (code - 0x59));
    }
    if (code == 0x62) return '0';
    if (code == 0x63) return '.';

    /* Japanese Keyboard Specific Keys */
    if (code == 0x88) return BTRON_KEY_HK_TOGGLE; /* Hiragana / Katakana Toggle */
    if (code == 0x8A) return BTRON_KEY_HENKAN;    /* Henkan (Convert) */
    if (code == 0x8B) return BTRON_KEY_MUHENKAN;  /* Muhenkan (Cancel) */

    return 0;
}

/* Process standard 8-byte USB HID keyboard report */
void ps2_usb_process_keyboard_report(const uint8_t report[8])
{
    uint8_t mod = report[0];
    s_kbd_reports++;

    /* 1. Detect newly pressed keys */
    for (int i = 2; i < 8; i++) {
        uint8_t code = report[i];
        if (code == 0) continue;

        int was_down = 0;
        for (int j = 0; j < 6; j++) {
            if (s_prev_keys[j] == code) {
                was_down = 1;
                break;
            }
        }

        if (!was_down) {
            uint32_t key = ps2_usb_hid_to_btron_key(mod, code);
            /* Named as it is decoded, before anything is made of it.  Above this
             * row sits the emulator, which decides whether a host keypress ever
             * reaches the device; below it sits the shell, which decides whether a
             * decoded key reaches a prompt.  Without this row a run that shows
             * nothing on screen cannot say which of the two lost it. */
            ps2_usb_log_kbd(mod, code, key, s_kbd_reports);
            if (key != 0) {
                ps2_usb_on_key(key, 1);
            }
        }
    }

    /* 2. Detect released keys */
    for (int j = 0; j < 6; j++) {
        uint8_t prev = s_prev_keys[j];
        if (prev == 0) continue;

        int is_down = 0;
        for (int i = 2; i < 8; i++) {
            if (report[i] == prev) {
                is_down = 1;
                break;
            }
        }

        if (!is_down) {
            uint32_t key = ps2_usb_hid_to_btron_key(mod, prev);
            if (key != 0) {
                ps2_usb_on_key(key, 0);
            }
        }
    }

    /* Save current state for next report */
    for (int k = 0; k < 6; k++) {
        s_prev_keys[k] = report[2 + k];
    }
}

/* Process standard 3/4-byte USB HID mouse report */
void ps2_usb_process_mouse_report(const uint8_t report[4])
{
    uint8_t buttons = report[0];
    int8_t dx = (int8_t)report[1];
    int8_t dy = (int8_t)report[2];
    s_mouse_reports++;
    ps2_usb_log_mouse_raw(s_mouse_reports,
                          (uint32_t)report[0] | ((uint32_t)report[1] << 8) |
                          ((uint32_t)report[2] << 16) | ((uint32_t)report[3] << 24));
    ps2_usb_on_mouse((int)dx, (int)dy, buttons);
}

void ps2_usb_inject_keyboard(uint8_t mod, uint8_t keycode)
{
    uint8_t report[8] = { mod, 0, keycode, 0, 0, 0, 0, 0 };
    ps2_usb_process_keyboard_report(report);

    /* Immediately release to simulate key click */
    uint8_t release_report[8] = { 0 };
    ps2_usb_process_keyboard_report(release_report);
}

void ps2_usb_inject_mouse(uint8_t buttons, int8_t dx, int8_t dy)
{
    ps2_usb_on_mouse((int)dx, (int)dy, buttons);
}
