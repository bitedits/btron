/*
 * B-TRON Retro OS — Cleanroom xHCI USB 3.0 / 2.0 Host Controller Interface
 * Supports VIA VL805 on Raspberry Pi 400 (AArch64 / BCM2711)
 */

#ifndef BTRON_ARCH_BCM283X_XHCI_H
#define BTRON_ARCH_BCM283X_XHCI_H

#include <stdint.h>
#include <stdbool.h>
#include <dwc2.h>  /* for usb_kbd_report_t and usb_mouse_report_t definitions */

/* xHCI Operational Register offsets */
#define XHCI_OP_USBCMD          0x00
#define XHCI_OP_USBSTS          0x04
#define XHCI_OP_PAGESIZE        0x08
#define XHCI_OP_DNCTRL          0x14
#define XHCI_OP_CRCR            0x18
#define XHCI_OP_DCBAAP          0x30
#define XHCI_OP_CONFIG          0x38
#define XHCI_OP_PORTSC_BASE     0x400

/* USBCMD bits */
#define XHCI_CMD_RS             (1u << 0)  /* Run/Stop */
#define XHCI_CMD_HCRST          (1u << 1)  /* Host Controller Reset */
#define XHCI_CMD_INTE           (1u << 2)  /* Interrupter Enable */

/* USBSTS bits */
#define XHCI_STS_HCH            (1u << 0)  /* HC Halted */
#define XHCI_STS_HSE            (1u << 2)  /* Host System Error */
#define XHCI_STS_EINT           (1u << 3)  /* Event Interrupt */
#define XHCI_STS_PCD            (1u << 4)  /* Port Change Detect */
#define XHCI_STS_CNR            (1u << 11) /* Controller Not Ready */

/* PORTSC bits */
#define XHCI_PORT_CCS           (1u << 0)  /* Current Connect Status */
#define XHCI_PORT_PED           (1u << 1)  /* Port Enabled/Disabled */
#define XHCI_PORT_PR            (1u << 4)  /* Port Reset */
#define XHCI_PORT_PLS_MASK      (0xFu << 5)/* Port Link State */
#define XHCI_PORT_PP            (1u << 9)  /* Port Power */
#define XHCI_PORT_CSC           (1u << 17) /* Connect Status Change */
#define XHCI_PORT_PRC           (1u << 21) /* Port Reset Change */

/* Interrupter 0 offsets (relative to Runtime Base + 0x20) */
#define XHCI_IR_IMAN            0x00
#define XHCI_IR_IMOD            0x04
#define XHCI_IR_ERSTSZ          0x08
#define XHCI_IR_ERSTBA          0x10
#define XHCI_IR_ERDP            0x18

/* TRB types */
#define XHCI_TRB_NORMAL         1
#define XHCI_TRB_SETUP_STAGE    2
#define XHCI_TRB_DATA_STAGE     3
#define XHCI_TRB_STATUS_STAGE   4
#define XHCI_TRB_LINK           6
#define XHCI_TRB_ENABLE_SLOT    9
#define XHCI_TRB_DISABLE_SLOT   10
#define XHCI_TRB_ADDRESS_DEV    11
#define XHCI_TRB_CONFIG_EP      12
#define XHCI_TRB_EVAL_CTX       13
#define XHCI_TRB_NOOP_CMD       23
#define XHCI_TRB_EVT_TRANSFER   32
#define XHCI_TRB_EVT_CMD_COMPL  33
#define XHCI_TRB_EVT_PORT_CHG   34

/* 16-byte Transfer Request Block */
typedef struct {
    uint64_t param;
    uint32_t status;
    uint32_t control;
} __attribute__((packed, aligned(16))) xhci_trb_t;

/* Event Ring Segment Table Entry */
typedef struct {
    uint64_t seg_base;
    uint32_t seg_size;
    uint32_t rsvd;
} __attribute__((packed, aligned(16))) xhci_erst_entry_t;

/* xHCI Slot Context (32 bytes) */
typedef struct {
    uint32_t info1;
    uint32_t info2;
    uint32_t tt_info;
    uint32_t state;
    uint32_t rsvd[4];
} __attribute__((packed, aligned(32))) xhci_slot_ctx_t;

/* xHCI Endpoint Context (32 bytes) */
typedef struct {
    uint32_t ep_info1;
    uint32_t ep_info2;
    uint64_t tr_dequeue_ptr;
    uint32_t tx_info;
    uint32_t rsvd[3];
} __attribute__((packed, aligned(32))) xhci_ep_ctx_t;

/* Driver Functions */
int  xhci_init(uintptr_t mmio_base);
int  xhci_poll_keyboard(usb_kbd_report_t *rep);
int  xhci_poll_mouse(usb_mouse_report_t *rep);
bool xhci_has_devices(void);

#endif /* BTRON_ARCH_BCM283X_XHCI_H */
