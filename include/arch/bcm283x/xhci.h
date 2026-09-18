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
#define XHCI_TRB_RESET_EP       14
#define XHCI_TRB_SET_TR_DQ      16
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

/* USB Standard Request Codes */
#define USB_REQ_GET_STATUS          0x00
#define USB_REQ_CLEAR_FEATURE       0x01
#define USB_REQ_SET_FEATURE         0x03
#define USB_REQ_SET_ADDRESS         0x05
#define USB_REQ_GET_DESCRIPTOR      0x06
#define USB_REQ_SET_DESCRIPTOR      0x07
#define USB_REQ_GET_CONFIGURATION   0x08
#define USB_REQ_SET_CONFIGURATION   0x09
#define USB_REQ_SET_INTERFACE       0x0B
#define USB_REQ_SET_IDLE            0x0A
#define USB_REQ_SET_PROTOCOL        0x0B

/* USB Request Types */
#define USB_REQ_TYPE_STANDARD       (0x00 << 5)
#define USB_REQ_TYPE_CLASS          (0x01 << 5)
#define USB_REQ_TYPE_VENDOR         (0x02 << 5)

#define USB_REQ_RCPT_DEVICE         0x00
#define USB_REQ_RCPT_INTERFACE      0x01
#define USB_REQ_RCPT_ENDPOINT       0x02
#define USB_REQ_RCPT_OTHER          0x03

#define USB_DIR_OUT                 0x00
#define USB_DIR_IN                  0x80

/* USB Descriptor Types */
#define USB_DT_DEVICE               0x01
#define USB_DT_CONFIGURATION        0x02
#define USB_DT_STRING               0x03
#define USB_DT_INTERFACE            0x04
#define USB_DT_ENDPOINT             0x05
#define USB_DT_HID                  0x21
#define USB_DT_REPORT               0x22
#define USB_DT_HUB                  0x29

/* Hub Class Port Features */
#define HUB_FEAT_PORT_CONNECTION    0
#define HUB_FEAT_PORT_ENABLE        1
#define HUB_FEAT_PORT_SUSPEND       2
#define HUB_FEAT_PORT_OVER_CURRENT  3
#define HUB_FEAT_PORT_RESET         4
#define HUB_FEAT_PORT_POWER         8
#define HUB_FEAT_PORT_LOW_SPEED     9
#define HUB_FEAT_C_PORT_CONNECTION  16
#define HUB_FEAT_C_PORT_ENABLE      17
#define HUB_FEAT_C_PORT_SUSPEND     18
#define HUB_FEAT_C_PORT_OVER_CURRENT 19
#define HUB_FEAT_C_PORT_RESET       20

/* Hub Port Status Bits */
#define HUB_PORT_STAT_CONNECTION    (1u << 0)
#define HUB_PORT_STAT_ENABLE        (1u << 1)
#define HUB_PORT_STAT_SUSPEND       (1u << 2)
#define HUB_PORT_STAT_OVER_CURRENT  (1u << 3)
#define HUB_PORT_STAT_RESET         (1u << 4)
#define HUB_PORT_STAT_POWER         (1u << 8)
#define HUB_PORT_STAT_LOW_SPEED     (1u << 9)
#define HUB_PORT_STAT_HIGH_SPEED    (1u << 10)

/* USB Setup Packet */
typedef struct {
    uint8_t  bmRequestType;
    uint8_t  bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} __attribute__((packed)) usb_setup_pkt_t;

/* USB Standard Device Descriptor (18 bytes) */
typedef struct {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t bcdUSB;
    uint8_t  bDeviceClass;
    uint8_t  bDeviceSubClass;
    uint8_t  bDeviceProtocol;
    uint8_t  bMaxPacketSize0;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t  iManufacturer;
    uint8_t  iProduct;
    uint8_t  iSerialNumber;
    uint8_t  bNumConfigurations;
} __attribute__((packed)) usb_device_desc_t;

/* USB Hub Descriptor */
typedef struct {
    uint8_t  bDescLength;
    uint8_t  bDescriptorType;
    uint8_t  bNbrPorts;
    uint16_t wHubCharacteristics;
    uint8_t  bPwrOn2PwrGood;
    uint8_t  bHubContrCurrent;
    uint8_t  DeviceRemovable;
    uint8_t  PortPwrCtrlMask;
} __attribute__((packed)) usb_hub_desc_t;

/* Hub Port Status Response (4 bytes) */
typedef struct {
    uint16_t wPortStatus;
    uint16_t wPortChange;
} __attribute__((packed)) usb_port_status_t;

/* Driver Functions */
int  xhci_init(uintptr_t mmio_base);
void xhci_process(void);        /* Drain event ring — call ONCE per poll cycle */
int  xhci_poll_keyboard(usb_kbd_report_t *rep);
int  xhci_poll_mouse(usb_mouse_report_t *rep);
bool xhci_has_devices(void);

#endif /* BTRON_ARCH_BCM283X_XHCI_H */
