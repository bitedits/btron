/*
 * ps2_usb.h — Cleanroom Sony PlayStation 2 USB Host Controller (OHCI) Driver
 *
 * Direct register layout, controller presence probe, and USB HID Boot Protocol
 * keyboard/mouse decoder.  Specification-derived only: OHCI 1.0/1.1 register
 * layout, USB 1.1 HID boot protocol.  Zero proprietary Sony SDK dependencies.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#ifndef PS2_USB_H
#define PS2_USB_H

#include <stdint.h>
#include <stddef.h>

/* PS2 OHCI MMIO base.  The controller sits on the IOP-side internal bus and
 * keeps its own numbering when the Emotion Engine reaches it over the remote
 * bus, so EE physical 0x1F801600 is the target; the 0xBF800000 prefix is the
 * uncached KSEG1 alias of that same address. */
#define OHCI_BASE_ADDR          0xBF801600UL

/* OHCI Register Offsets */
#define OHCI_REG_REVISION       0x00
#define OHCI_REG_CONTROL        0x04
#define OHCI_REG_CMDSTATUS      0x08
#define OHCI_REG_INTSTATUS      0x0C
#define OHCI_REG_INTENABLE      0x10
#define OHCI_REG_INTDISABLE     0x14
#define OHCI_REG_HCCA           0x18
#define OHCI_REG_CMDSTATE       0x1C
#define OHCI_REG_CTRL_HEAD      0x20
#define OHCI_REG_CTRL_CUR       0x24
#define OHCI_REG_BULK_HEAD      0x28
#define OHCI_REG_DONE_HEAD      0x30
#define OHCI_REG_FMINTERVAL     0x34
#define OHCI_REG_FMREMAINING    0x38
#define OHCI_REG_FMNUMBER       0x3C
#define OHCI_REG_RHDESCRIPTORA  0x48
#define OHCI_REG_RHSTATUS       0x50
#define OHCI_REG_RHPORT1        0x54
#define OHCI_REG_RHPORT2        0x58

/* Sony's OHCI has an enable block past the standard register window, which the
 * Linux ps2 glue writes before the controller starts clocking frames */
#define OHCI_REG_PS2_ENABLE     0x80
#define OHCI_PS2_ENABLE_VALUE   0x11u

/* OHCI Control Register Bits (OHCI 1.0, HcControl at offset 0x04) */
#define OHCI_CTRL_CBSR          (3u << 0)   /* Control/Bulk service request */
#define OHCI_CTRL_PLE           (1u << 2)   /* Periodic list enable */
#define OHCI_CTRL_IE            (1u << 3)   /* SOF interrupt enable */
#define OHCI_CTRL_CLE           (1u << 4)   /* Control list enable */
#define OHCI_CTRL_BLE           (1u << 5)   /* Bulk list enable */
#define OHCI_CTRL_HCFS_RESET    (0x00u << 6)
#define OHCI_CTRL_HCFS_RESUME   (0x01u << 6)
#define OHCI_CTRL_HCFS_OPER     (0x02u << 6)
#define OHCI_CTRL_HCFS_SUSPEND  (0x03u << 6)
#define OHCI_CTRL_HCFS_MASK     (0x03u << 6)
#define OHCI_CTRL_IR            (1u << 8)   /* Interrupt routing */

/* HcInterruptStatus / HcInterruptEnable Bits */
#define OHCI_INTR_SO            (1u << 0)   /* Scheduling overrun */
#define OHCI_INTR_WD            (1u << 1)   /* DoneHead writeback */
#define OHCI_INTR_SF            (1u << 2)   /* Start of frame */
#define OHCI_INTR_RD            (1u << 3)   /* Resume detected */
#define OHCI_INTR_UE            (1u << 4)   /* Unrecoverable error */
#define OHCI_INTR_RHSC          (1u << 6)   /* Root hub status change */
#define OHCI_INTR_MIE           (1u << 31)  /* Master interrupt enable */

/* HcCommandStatus Bits (OHCI 1.0, offset 0x08).  Bits are set by writing a 1
 * and a 0 leaves the bit alone, so this register is also how a driver tells the
 * controller that it has filled in the control list: until CLF is written the
 * controller never looks at HcControlHeadED, and a list it has drained clears
 * CLF again by itself. */
#define OHCI_CMD_HCRST          (1u << 0)   /* Host Controller Reset */
#define OHCI_CMD_CLF            (1u << 1)   /* Control List Filled */
#define OHCI_CMD_BLF            (1u << 2)   /* Bulk List Filled */
#define OHCI_CMD_OCR            (1u << 3)   /* Ownership Change Request */

/* HcRhPortStatus Bits (OHCI 1.0, offsets 0x54 + 4 per port).
 *
 * The four change bits at 16..20 are write-one-to-clear; the control bits below
 * them are write-one-to-request.  PRS in particular is bit 4: writing bit 3
 * instead asks for an over-current indication, which does nothing at all on a
 * root hub, so a reset request that lands there leaves the port disabled and
 * no device on it will ever answer a token. */
#define OHCI_PORT_CCS           (1u << 0)   /* Current Connect Status */
#define OHCI_PORT_PES           (1u << 1)   /* Port Enable Status */
#define OHCI_PORT_PSS           (1u << 2)   /* Port Suspend Status */
#define OHCI_PORT_POCI          (1u << 3)   /* Over-current indication */
#define OHCI_PORT_PRS           (1u << 4)   /* Port Reset Status (request) */
#define OHCI_PORT_PPS           (1u << 8)   /* Port Power Status */
#define OHCI_PORT_LSDA          (1u << 9)   /* Low-Speed Device Attached */
#define OHCI_PORT_CSC           (1u << 16)  /* Connect Status Change */
#define OHCI_PORT_PESC          (1u << 17)  /* Enable Status Change */
#define OHCI_PORT_PSSC          (1u << 18)  /* Suspend Status Change */
#define OHCI_PORT_OCIC          (1u << 19)  /* Over-current Indication Change */
#define OHCI_PORT_PRSC          (1u << 20)  /* Reset Status Change */
/* Everything the driver acknowledges rather than requests. */
#define OHCI_PORT_CHANGE_BITS   (OHCI_PORT_CSC | OHCI_PORT_PESC | \
                                 OHCI_PORT_PSSC | OHCI_PORT_OCIC | \
                                 OHCI_PORT_PRSC)

/* HcRhStatus Bits */
#define OHCI_RH_STATUS_PWR      (1u << 0)   /* Power Supply Status (read-only) */
#define OHCI_RH_STATUS_NPS      (1u << 16)  /* No Power Supply */
#define OHCI_RH_STATUS_PI       (1u << 17)  /* Power control Implemented */

#include <btron/event.h>

#ifndef BTRON_KEY_INSERT
#define BTRON_KEY_INSERT        0x40000049
#endif
#ifndef BTRON_KEY_PGUP
#define BTRON_KEY_PGUP          BTRON_KEY_PAGE_UP
#endif
#ifndef BTRON_KEY_PGDN
#define BTRON_KEY_PGDN          BTRON_KEY_PAGE_DOWN
#endif
#ifndef BTRON_KEY_HENKAN
#define BTRON_KEY_HENKAN        0x4000008A
#endif
#ifndef BTRON_KEY_MUHENKAN
#define BTRON_KEY_MUHENKAN      0x4000008B
#endif
#ifndef BTRON_KEY_HIRAGANA
#define BTRON_KEY_HIRAGANA      BTRON_KEY_F6
#endif
#ifndef BTRON_KEY_KATAKANA
#define BTRON_KEY_KATAKANA      BTRON_KEY_F7
#endif
#ifndef BTRON_KEY_HK_TOGGLE
#define BTRON_KEY_HK_TOGGLE     0x40000088
#endif

/* What one pass of the presence probe measured.  Everything here is a raw
 * register value or a count, because the phase-1 question is not "does the
 * driver work" but "can this controller master memory the Emotion Engine can
 * read back" -- and the only memory it can master is IOP RAM. */
typedef struct {
    uint32_t rev;            /* HcRevision before any write */
    uint32_t ctrl;           /* HcControl */
    uint32_t cmd;            /* HcCommandStatus */
    uint32_t intstatus;      /* HcInterruptStatus */
    uint32_t intenable;      /* HcInterruptEnable, read back after disabling all */
    uint32_t fminterval;     /* HcFmInterval: a real OHCI resets to 0x80002ED3 */
    uint32_t fm_before;      /* HcFmNumber */
    uint32_t fm_after;       /* HcFmNumber after a measured wait */
    uint32_t rh_a;           /* HcRhDescriptorA: low byte is the port count */
    uint32_t rh_status;      /* HcRhStatus */
    uint32_t port1;          /* HcRhPortStatus 1 as found */
    uint32_t port2;          /* HcRhPortStatus 2 as found */
    uint32_t port1_after;    /* ...and after the probe ran, which must match: a */
    uint32_t port2_after;    /* controller reset clears CCS and nothing writes it */
    uint32_t rev_enabled;    /* HcRevision after the PS2 enable write */
    uint32_t hcca_iop;       /* HcHCCA as written: a byte offset inside IOP RAM */
    uint32_t hcca_frame;     /* HCCA FrameNumber, read back through the window */
    uint32_t hcca_done;      /* HCCA DoneHead, read back through the window */
    uint32_t int_after;      /* HcInterruptStatus after the measured run */
    int      regs_alive;     /* window decoded as something other than 0 or ~0 */
    int      frames_run;     /* HcFmNumber advanced across the wait */
    int      iop_dma_ok;     /* controller's frame writeback landed where we saw it */
} ps2_ohci_probe_t;

/* Probe verdicts */
#define PS2_OHCI_NONE         0   /* window reads as 0 or all-ones: not reachable from the EE */
#define PS2_OHCI_ALIVE        1   /* registers decode, but no descriptor writeback yet */
#define PS2_OHCI_IOP_DMA      2   /* controller masters IOP RAM we can read: host engine viable */

/* ── Host-controller descriptors ─────────────────────────────────
 *
 * Endpoint and transfer descriptors are the OHCI 1.0 ring format: four
 * little-endian words each, addressed by an offset inside IOP RAM because that
 * is the only memory this controller can master.  The controller owns the ED's
 * `head` word and the TD's `flags` condition code; everything else belongs to
 * the driver, and a TD stays in the queue untouched until the controller has a
 * reason to retire it -- a device that answers "not ready" leaves it exactly as
 * it was, which is what makes one queued TD per endpoint a poll. */
typedef struct {
    uint32_t flags;   /* function address, endpoint, direction, speed, max packet */
    uint32_t tail;    /* one past the last TD queued: the driver writes this */
    uint32_t head;    /* the TD being served: the controller writes this back */
    uint32_t next;
} ps2_ohci_ed_t;

typedef struct {
    uint32_t flags;   /* condition code, delay, direction, rounding */
    uint32_t cbp;     /* current buffer pointer */
    uint32_t next;
    uint32_t be;      /* buffer end pointer, inclusive */
} ps2_ohci_td_t;

/* Endpoint descriptor flags word */
#define ED_FA_SHIFT             0           /* function (device) address, 7 bits */
#define ED_EN_SHIFT             7           /* endpoint number, 4 bits */
#define ED_D_SHIFT              11          /* direction, 2 bits */
#define ED_S                    (1u << 13)   /* 1 = low speed, 0 = full speed */
#define ED_K                    (1u << 14)   /* skip this endpoint */
#define ED_F                    (1u << 15)   /* 1 = isochronous format */
#define ED_MPS_SHIFT            16          /* max packet size, 11 bits */
#define ED_MPS_MASK             (0x7FFu << ED_MPS_SHIFT)
/* Flags in the head word */
#define ED_H                    (1u << 0)   /* halted */
#define ED_C                    (1u << 1)   /* data toggle, control/iso only */
#define ED_PTR_MASK             0xFFFFFFF0u

/* Transfer descriptor flags word */
#define TD_R                    (1u << 18)  /* buffer rounding: a short packet is fine */
#define TD_DP_SHIFT             19          /* direction */
#define TD_DI_SHIFT             21          /* interrupt delay, in frames */
#define TD_CC_SHIFT             28          /* condition code, written by the controller */
#define TD_CC_MASK              (0xFu << TD_CC_SHIFT)

#define TD_DIR_SETUP            0u
#define TD_DIR_OUT              1u
#define TD_DIR_IN               2u

/* Condition codes (OHCI 1.0 table 4-2).  Only the ones a root-hub poll can
 * actually produce are named; 0 means the transfer completed as asked. */
#define CC_NOERROR              0x0u
#define CC_CRC                  0x1u
#define CC_BITSTUFFING          0x2u
#define CC_TOGGLEMISMATCH       0x3u
#define CC_STALL                0x4u
#define CC_NODEV                0x5u
#define CC_PIDCHECK             0x6u
#define CC_UNEXPECTEDPID        0x7u
#define CC_DATAOVERRUN          0x8u
#define CC_DATAUNDERRUN         0x9u
#define CC_BUFFEROVERRUN        0xCu
#define CC_BUFFERUNDERRUN       0xDu

/* Public API */
/* What one pass of the host engine measured per port.  `step` records how far
 * enumeration got, so a device that answers nothing looks different from a
 * device whose SET_ADDRESS never took effect. */
typedef enum {
    PS2_USB_STEP_NONE = 0,
    PS2_USB_STEP_RESET,        /* port reset requested and the port came up enabled */
    PS2_USB_STEP_DESC0,        /* first 8 bytes of the device descriptor read at addr 0 */
    PS2_USB_STEP_ADDRESS,      /* SET_ADDRESS accepted and the descriptor re-read at it */
    PS2_USB_STEP_CONFIG,       /* whole configuration descriptor read */
    PS2_USB_STEP_CONFIGURED,   /* SET_CONFIGURATION accepted */
    PS2_USB_STEP_POLLING,      /* interrupt IN endpoint armed and queued */
    PS2_USB_STEP_DUPADDR,      /* enumerated, but another device already holds its
                                * function address, so no ring was armed for it */
} ps2_usb_step_t;

typedef struct {
    uint32_t port;             /* 1-based root hub port, 0 when unused */
    uint32_t addr;             /* assigned function address */
    uint32_t step;             /* ps2_usb_step_t */
    uint32_t cc;               /* last condition code that was not CC_NOERROR */
    uint32_t mps;              /* interrupt IN max packet size */
    uint32_t interval;         /* bInterval, in frames */
    uint32_t ep;               /* interrupt IN endpoint number */
    uint32_t low_speed;        /* ED speed bit as written */
    uint32_t is_keyboard;      /* interface came up in boot-keyboard protocol */
    uint32_t polls;            /* TDs retired on the interrupt endpoint */
    uint32_t errors;           /* TDs retired with a nonzero condition code */
    uint32_t port_status;      /* HcRhPortStatus after the reset */
    uint32_t desc_id;          /* idVendor | idProduct << 16, which device this is */
    uint32_t stuck_td;         /* 1-based control TD left at the head, 0 = ring drained */
    uint32_t frames;           /* frames the controller advanced during that wait */
    int      wait_ret;         /* td_wait_written() return for the failed transfer */
    int      live;             /* reports are being decoded from this device */
} ps2_usb_dev_t;

/* Bring the host engine up: build the descriptors in the borrowed IOP-RAM
 * block, reset the root hub ports, enumerate what answers and arm its interrupt
 * endpoint.  Returns the number of devices left polling.  Call after
 * ps2_usb_probe() has said PS2_OHCI_IOP_DMA. */
int  ps2_usb_host_start(void);
const ps2_usb_dev_t *ps2_usb_dev(int index);   /* 0 or 1, NULL when unused */
int  ps2_usb_host_up(void);
uint32_t ps2_usb_host_us(void);              /* wall time enumeration took */
uint32_t ps2_usb_frame_number(void);           /* HcFmNumber, for the log */
uint32_t ps2_usb_reg(uint32_t off);            /* one raw Hc* register, for the log */
uint32_t ps2_usb_engine_rearms(void);          /* transfers that had to restart the frame engine */
uint32_t ps2_usb_async_releases(void);         /* transfers that had to cancel a stranded TD */

/* Which word of a device's interrupt queue ps2_usb_intr_word() is asked for.
 * The first five are the controller's own memory rather than our bookkeeping:
 * HeadP and TailP say what the periodic list holds, and a descriptor's next
 * pointer, condition code and buffer pointer say whether it was walked. */
enum {
    INTR_W_ED_HEAD,
    INTR_W_ED_TAIL,
    INTR_W_TD_IOP,     /* this descriptor's address, to read the two above against */
    INTR_W_TD_NEXT,
    INTR_W_TD_CC,
    INTR_W_TD_CBP,
    INTR_W_PENDING,    /* how many descriptors are armed and waiting to be collected */
    INTR_W_SLOT,       /* the descriptor at the head of the ring, the next report due */
    INTR_W_ARM,        /* the next free descriptor, the one a top-up would fill */
    INTR_W_BURST       /* most reports one pump pass retired; see ps2_usb_poll() */
};
uint32_t ps2_usb_intr_word(int index, int which);

/* One row per control transfer, printed as the transfer finishes -- which is
 * what puts it in the same order as the host's own device-side log, so the two
 * can be read against each other.  It travels as one pointer because this
 * driver builds -march=mips3 while the console that prints it builds
 * -march=mips2, and a call of more than four arguments leaves the fifth onward
 * on the stack, which those two lay out differently; a format string across
 * that boundary is worse still.  Implemented by whichever core owns the
 * console. */
typedef struct {
    uint32_t port;
    uint32_t tag;            /* which step of enumeration this row is, see below */
    uint32_t req;            /* bmRequestType in the high byte, bRequest low */
    uint32_t ntd;            /* descriptors this transfer queued */
    int32_t  result;         /* the drain wait's return: frames, -1 dead, -2 out of frames */
    uint32_t head;           /* the endpoint head as the host last wrote it back */
    uint32_t done;           /* HcDoneHead: a descriptor inside the block is retired */
    uint32_t stuck;          /* 1-based descriptor the host never came back from */
    uint32_t cc;             /* last condition code that was not CC_NOERROR */
    uint32_t data0;          /* first four bytes of the data payload, zeroed before */
    uint32_t svc;            /* bit per descriptor: the host moved its buffer to the end */
    int32_t  quiet;          /* frames until the host released the list, negative = never */
} ps2_usb_tx_t;
void ps2_usb_log_tx(const ps2_usb_tx_t *t);

/* Announced once, from the first interrupt report the host decoded.  The boot
 * log's report counters are printed less than two seconds after the polling ring
 * goes out, which is before a device has had a frame to answer in -- so without
 * this row a run that never gets input and a run whose polling ring is dead
 * print the same zero.  Two scalars, so it crosses the mips2/mips3 boundary
 * safely. */
void ps2_usb_report_landed(uint32_t kbd, uint32_t mouse);

/* One row per key the keyboard decoder sees, printed with the modifiers and the
 * HID usage that produced it, before the key is posted to anyone.  Four scalars
 * for the same reason as above.  */
void ps2_usb_log_kbd(uint32_t mod, uint32_t code, uint32_t key, uint32_t reports);

/* One row per pointer report with the bytes as the endpoint handed them over,
 * before this file turns any of them into an axis.  A pointer that moves the
 * wrong way has two possible authors -- the device, which put the columns in the
 * slots we read as rows, and the parse, which read them wrongly -- and only the
 * unopened report tells them apart.  `b3b0` is the report in memory order, low
 * byte first; two scalars, so it crosses the mips2/mips3 boundary safely. */
void ps2_usb_log_mouse_raw(uint32_t reports, uint32_t b3b0);

/* One row per pump pass that retired anything, with how many reports that pass
 * drained and the deepest any pass has ever gone.  This is the row that says
 * whether the pointer's rate is set by the bus or by us: `got` sitting at the
 * ring's capacity means reports were waiting to be collected when the pass ended,
 * so the pump is the limit and the device has begun discarding motion; `got` at
 * one or two means the ring is keeping up and whatever the pointer does next is
 * the mapping's doing.  Four scalars, as above. */
void ps2_usb_log_ring(uint32_t dev, uint32_t got, uint32_t burst, uint32_t frame);

/* Which call of the enumeration a [TX] row came from.  The address stage needs
 * two probes because nothing about a SET_ADDRESS that failed is visible in the
 * descriptor it left behind: the read at tag 3 asks the same descriptor at the
 * new address and the one at tag 2 at address zero, and which of the two brings
 * back bytes is the only answer available for whether the device took the
 * address at all. */
enum {
    PS2_TX_PLAIN     = 0,
    PS2_TX_SETADDR   = 1,
    PS2_TX_READ_AT0  = 2,
    PS2_TX_READ_ADDR = 3,
};

void ps2_usb_init(void);
void ps2_usb_poll(void);
int  ps2_usb_probe(ps2_ohci_probe_t *out);   /* runs the measurement, returns verdict */
const ps2_ohci_probe_t *ps2_usb_last_probe(void);
int  ps2_usb_verdict(void);                  /* last probe verdict */
uint32_t ps2_usb_kbd_reports(void);          /* accepted HID keyboard reports */
uint32_t ps2_usb_mouse_reports(void);        /* accepted HID mouse reports */
const char *ps2_usb_verdict_name(int verdict);
void ps2_usb_reset_probe(void);              /* discard state and re-probe next poll */
int  ps2_usb_shadow_lost(void);              /* IOP reclaimed the shadow block */
uint32_t ps2_usb_hid_to_btron_key(uint8_t mod, uint8_t code);
void ps2_usb_process_keyboard_report(const uint8_t report[8]);
void ps2_usb_process_mouse_report(const uint8_t report[4]);
void ps2_usb_inject_keyboard(uint8_t mod, uint8_t keycode);
void ps2_usb_inject_mouse(uint8_t buttons, int8_t dx, int8_t dy);

#endif /* PS2_USB_H */
