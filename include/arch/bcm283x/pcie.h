/*
 * B-TRON Retro OS — BCM2711 PCIe Root Complex & VL805 Interface
 * Cleanroom implementation for Raspberry Pi 400 (Cortex-A72 / AArch64)
 */

#ifndef BTRON_ARCH_BCM283X_PCIE_H
#define BTRON_ARCH_BCM283X_PCIE_H

#include <stdint.h>
#include <stdbool.h>

/* BCM2711 PCIe Root Complex Base Address in ARM physical address space */
#define BCM2711_PCIE_REG_BASE           0xFD500000UL

/* 64-bit Outbound Window: CPU ARM physical address -> PCI address */
#define BCM2711_PCIE_CPU_MEM_BASE       0x600000000ULL
#define BCM2711_PCIE_BUS_MEM_BASE       0xC0000000UL
#define BCM2711_PCIE_MEM_SIZE           0x40000000UL     /* 1 GB */

/* VIA VL805 xHCI Controller Specifics on Pi 4 / Pi 400 */
#define VL805_PCI_BUS                   1
#define VL805_PCI_DEV                   0
#define VL805_PCI_FUNC                  0
#define VL805_PCI_ADDR                  0x00100000UL     /* Bus 1, Dev 0, Func 0 */
#define VL805_VENDOR_ID                 0x1106           /* VIA Technologies */
#define VL805_DEVICE_ID                 0x3483           /* VL805 USB 3.0 Host */

/* Broadcom STB PCIe RC Register Offsets */
#define PCIE_RC_CFG_VENDOR_SPECIFIC_REG1 0x0188
#define PCIE_RC_CFG_PRIV1_ID_VAL3        0x043C
#define PCIE_RC_DL_MDIO_ADDR             0x1100
#define PCIE_RC_DL_MDIO_WR_DATA          0x1104
#define PCIE_RC_DL_MDIO_RD_DATA          0x1108
#define PCIE_MISC_MISC_CTRL              0x4008
#define PCIE_MISC_CPU_2_PCIE_MEM_WIN0_LO 0x400C
#define PCIE_MISC_CPU_2_PCIE_MEM_WIN0_HI 0x4010
#define PCIE_MISC_RC_BAR1_CONFIG_LO      0x402C
#define PCIE_MISC_RC_BAR2_CONFIG_LO      0x4034
#define PCIE_MISC_RC_BAR2_CONFIG_HI      0x4038
#define PCIE_MISC_RC_BAR3_CONFIG_LO      0x403C
#define PCIE_MISC_PCIE_STATUS            0x4068
#define PCIE_MISC_REVISION               0x406C
#define PCIE_MISC_CPU_2_PCIE_MEM_WIN0_BASE_LIMIT 0x4070
#define PCIE_MISC_CPU_2_PCIE_MEM_WIN0_BASE_HI    0x4074
#define PCIE_MISC_CPU_2_PCIE_MEM_WIN0_LIMIT_HI   0x4078
#define PCIE_MISC_HARD_PCIE_HARD_DEBUG   0x4204
#define PCIE_EXT_CFG_INDEX               0x9000
#define PCIE_EXT_CFG_DATA                0x9004

/* VideoCore Mailbox Property Tag for VL805 Firmware Bootstrap */
#define RPI_FIRMWARE_NOTIFY_XHCI_RESET   0x00030058UL

/* PCI Configuration Registers */
#define PCI_VENDOR_ID                    0x00
#define PCI_DEVICE_ID                    0x02
#define PCI_COMMAND                      0x04
#define PCI_STATUS                       0x06
#define PCI_CLASS_REVISION               0x08
#define PCI_BAR0                         0x10

/* PCI Command bits */
#define PCI_COMMAND_IO                   (1u << 0)
#define PCI_COMMAND_MEMORY               (1u << 1)
#define PCI_COMMAND_MASTER               (1u << 2)

/* Driver API */
int bcm2711_pcie_init(void);
uint32_t pci_read_config32(uint32_t bus, uint32_t dev, uint32_t func, uint32_t offset);
void pci_write_config32(uint32_t bus, uint32_t dev, uint32_t func, uint32_t offset, uint32_t val);
uintptr_t bcm2711_pcie_get_vl805_mmio(void);

#endif /* BTRON_ARCH_BCM283X_PCIE_H */
