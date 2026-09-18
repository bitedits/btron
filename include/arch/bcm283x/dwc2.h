/*
 * B-TRON Retro OS — BCM283x DWC2 USB 2.0 Host Controller & HID Driver
 * Cleanroom implementation for Raspberry Pi 2B (BCM2836) and QEMU.
 */

#ifndef BTRON_DRIVERS_BCM283X_DWC2_H
#define BTRON_DRIVERS_BCM283X_DWC2_H

#include <stdint.h>
#include <stdbool.h>

/*
 * Peripheral base address:
 *   QEMU raspi2b emulates a BCM2835 peripheral map → 0x20000000
 *   Real Raspberry Pi 2B hardware (BCM2836)         → 0x3F000000
 *
 * Set -DQEMU_RASPI2B=1 in ARM_CFLAGS (Makefile) for QEMU builds.
 * Leave unset (or set -DREAL_RPI2B=1) for physical hardware.
 */
#if defined(QEMU_RASPI2B)
#define BCM283X_PERIPH_BASE  0x20000000u  /* QEMU raspi2b: BCM2835 map */
#elif defined(__aarch64__)
extern uintptr_t g_mmio_base;
#define BCM283X_PERIPH_BASE  g_mmio_base  /* Dynamic Pi 3B (0x3F000000) or Pi 4B (0xFE000000) */
#elif defined(_RPI_BCM283x_)
#define BCM283X_PERIPH_BASE  0x3F000000u  /* Real Pi 2B BCM2836 hardware */
#else
#define BCM283X_PERIPH_BASE  0x20000000u  /* Default / unknown → BCM2835 */
#endif

/* Legacy alias */
#define BCM2836_PERIPH_BASE  BCM283X_PERIPH_BASE

#define DWC2_BASE_ADDR        (BCM283X_PERIPH_BASE + 0x00980000u)

/* Core Global Registers */
#define DWC2_GOTGCTL          0x000
#define DWC2_GOTGINT          0x004
#define DWC2_GAHBCFG          0x008
#define DWC2_GUSBCFG          0x00C
#define DWC2_GRSTCTL          0x010
#define DWC2_GINTSTS          0x014
#define DWC2_GINTMSK          0x018
#define DWC2_GRXSTSR          0x01C
#define DWC2_GRXSTSP          0x020
#define DWC2_GRXFSIZ          0x024
#define DWC2_GNPTXFSIZ        0x028
#define DWC2_GNPTXSTS         0x02C
#define DWC2_HPTXFSIZ         0x100

/* Host Mode Registers */
#define DWC2_HCFG             0x400
#define DWC2_HFIR             0x404
#define DWC2_HFNUM            0x408
#define DWC2_HPTXSTS          0x410
#define DWC2_HAINT            0x414
#define DWC2_HAINTMSK         0x418
#define DWC2_HPRT0            0x440

/* Host Channel Registers (Channel n = 0..15) */
#define DWC2_HCCHAR(n)        (0x500 + 0x20 * (n))
#define DWC2_HCSPLT(n)        (0x504 + 0x20 * (n))
#define DWC2_HCINT(n)         (0x508 + 0x20 * (n))
#define DWC2_HCINTMSK(n)      (0x50C + 0x20 * (n))
#define DWC2_HCTSIZ(n)        (0x510 + 0x20 * (n))
#define DWC2_HCDMA(n)         (0x514 + 0x20 * (n))

/* Standard USB Request Types */
#define USB_REQ_GET_STATUS        0x00
#define USB_REQ_CLEAR_FEATURE     0x01
#define USB_REQ_SET_FEATURE       0x03
#define USB_REQ_SET_ADDRESS       0x05
#define USB_REQ_GET_DESCRIPTOR    0x06
#define USB_REQ_SET_DESCRIPTOR    0x07
#define USB_REQ_GET_CONFIGURATION 0x08
#define USB_REQ_SET_CONFIGURATION 0x09
#define USB_REQ_GET_INTERFACE     0x0A
#define USB_REQ_SET_INTERFACE     0x0B
#define USB_REQ_SET_IDLE          0x0A
#define USB_REQ_SET_PROTOCOL      0x0B

/* Descriptor Types */
#define USB_DESC_TYPE_DEVICE      0x01
#define USB_DESC_TYPE_CONFIG      0x02
#define USB_DESC_TYPE_STRING      0x03
#define USB_DESC_TYPE_INTERFACE   0x04
#define USB_DESC_TYPE_ENDPOINT    0x05
#define USB_DESC_TYPE_HID         0x21
#define USB_DESC_TYPE_REPORT      0x22

#pragma pack(push, 1)
typedef struct {
    uint8_t  bmRequestType;
    uint8_t  bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} usb_setup_packet_t;

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
} usb_device_descriptor_t;

typedef struct {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t wTotalLength;
    uint8_t  bNumInterfaces;
    uint8_t  bConfigurationValue;
    uint8_t  iConfiguration;
    uint8_t  bmAttributes;
    uint8_t  bMaxPower;
} usb_config_descriptor_t;

typedef struct {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bInterfaceNumber;
    uint8_t  bAlternateSetting;
    uint8_t  bNumEndpoints;
    uint8_t  bInterfaceClass;
    uint8_t  bInterfaceSubClass;
    uint8_t  bInterfaceProtocol;
    uint8_t  iInterface;
} usb_interface_descriptor_t;

typedef struct {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bEndpointAddress;
    uint8_t  bmAttributes;
    uint16_t wMaxPacketSize;
    uint8_t  bInterval;
} usb_endpoint_descriptor_t;

typedef struct {
    uint8_t modifiers;
    uint8_t reserved;
    uint8_t keys[6];
} usb_kbd_report_t;

typedef struct {
    uint8_t buttons;
    int16_t dx;
    int16_t dy;
    int16_t wheel;
} usb_mouse_report_t;
#pragma pack(pop)

/* Driver Functions */
int  dwc2_init(void);
bool dwc2_has_devices(void);
int  dwc2_poll_keyboard(usb_kbd_report_t *report);
int  dwc2_poll_mouse(usb_mouse_report_t *report);
uint32_t dwc2_usb_to_btron_key(uint8_t scancode, uint8_t modifiers);

#endif /* BTRON_DRIVERS_BCM283X_DWC2_H */
