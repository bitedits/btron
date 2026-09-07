USB
===

Let's add support for USB mouse and keyboard for UEFI build!
In QEMU, the emulated 8042 PS/2 controller handles input seamlessly.
However, for modern UEFI baremetal hardware without legacy PS/2 emulation (CSM) or QEMU with -device qemu-xhci, -device usb-kbd, -device usb-mouse:

* We need an xHCI (Extensible Host Controller Interface, USB 3.0) PCI driver at Class 0x0C, Subclass 0x03, ProgIF 0x30.
* USB HID Boot Protocol parsers (8-byte keyboard report, 3-byte mouse report).

We can implement an xHCI PCI discovery and USB HID boot protocol driver for native USB keyboard and mouse on modern UEFI machines.
You can consult Haiku USB stack in ./thrird_party/ folder.

# Credits

Namdak Tonpa
