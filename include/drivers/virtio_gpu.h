/*
 * virtio_gpu.h — VirtIO-GPU PCI Device Driver for B-System (BTRON 3.20)
 * Designed for QEMU q35 / PC Workstation Targets (virtio-gpu-pci & virtio-vga)
 */

#ifndef BTRON_DRIVERS_VIRTIO_GPU_H
#define BTRON_DRIVERS_VIRTIO_GPU_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* VirtIO PCI Identifiers */
#define VIRTIO_PCI_VENDOR_ID            0x1AF4
#define VIRTIO_PCI_DEVICE_GPU           0x1050
#define VIRTIO_PCI_DEVICE_LEGACY_GPU    0x1010
#define VIRTIO_SUBSYS_GPU               0x0010

/* VirtIO PCI Capability Types */
#define VIRTIO_PCI_CAP_COMMON_CFG       1
#define VIRTIO_PCI_CAP_NOTIFY_CFG       2
#define VIRTIO_PCI_CAP_ISR_CFG          3
#define VIRTIO_PCI_CAP_DEVICE_CFG       4
#define VIRTIO_PCI_CAP_PCI_CFG          5

/* VirtIO Device Status Bits */
#define VIRTIO_STAT_ACKNOWLEDGE         1
#define VIRTIO_STAT_DRIVER              2
#define VIRTIO_STAT_DRIVER_OK           4
#define VIRTIO_STAT_FEATURES_OK         8
#define VIRTIO_STAT_FAILED              128

/* VirtIO 1.0 Feature Bits */
#define VIRTIO_F_VERSION_1              (1ULL << 32)

/* VirtIO-GPU 2D Command Codes */
#define VIRTIO_GPU_CMD_GET_DISPLAY_INFO         0x0100
#define VIRTIO_GPU_CMD_RESOURCE_CREATE_2D       0x0101
#define VIRTIO_GPU_CMD_RESOURCE_UNREF           0x0102
#define VIRTIO_GPU_CMD_SET_SCANOUT              0x0103
#define VIRTIO_GPU_CMD_RESOURCE_FLUSH           0x0104
#define VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D      0x0105
#define VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING  0x0106
#define VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING  0x0107

#define VIRTIO_GPU_RESP_OK_NODATA               0x1100
#define VIRTIO_GPU_RESP_OK_DISPLAY_INFO         0x1101
#define VIRTIO_GPU_RESP_ERR_UNSPEC              0x1200

/* Pixel Formats */
#define VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM        1
#define VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM        2
#define VIRTIO_GPU_FORMAT_A8R8G8B8_UNORM        3
#define VIRTIO_GPU_FORMAT_X8R8G8B8_UNORM        4
#define VIRTIO_GPU_FORMAT_R8G8B8A8_UNORM        67
#define VIRTIO_GPU_FORMAT_R8G8B8X8_UNORM        68

/* VirtIO PCI Capability Structure (in PCI Configuration Space) */
struct virtio_pci_cap {
    uint8_t  cap_vndr;    /* 0x09 = PCI_CAP_ID_VNDR */
    uint8_t  cap_next;    /* Next capability pointer */
    uint8_t  cap_len;     /* Total length */
    uint8_t  cfg_type;    /* VIRTIO_PCI_CAP_* */
    uint8_t  bar;         /* BAR 0-5 */
    uint8_t  id;
    uint8_t  padding[2];
    uint32_t offset;      /* Offset within BAR */
    uint32_t length;      /* Size within BAR */
} __attribute__((packed));

/* VirtIO PCI Common Configuration Structure (MMIO) */
struct virtio_pci_common_cfg {
    uint32_t device_feature_select;
    uint32_t device_feature;
    uint32_t driver_feature_select;
    uint32_t driver_feature;
    uint16_t config_msix_vector;
    uint16_t num_queues;
    uint8_t  device_status;
    uint8_t  config_generation;
    uint16_t queue_select;
    uint16_t queue_size;
    uint16_t queue_msix_vector;
    uint16_t queue_enable;
    uint16_t queue_notify_off;
    uint64_t queue_desc;
    uint64_t queue_driver;
    uint64_t queue_device;
    uint16_t queue_notif_config_data;
    uint16_t queue_reset;
} __attribute__((packed));

/* Virtqueue Split Ring Descriptors */
#define VRING_DESC_F_NEXT       1
#define VRING_DESC_F_WRITE      2
#define VRING_DESC_F_INDIRECT   4

struct vring_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed));

struct vring_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[];
} __attribute__((packed));

struct vring_used_elem {
    uint32_t id;
    uint32_t len;
} __attribute__((packed));

struct vring_used {
    uint16_t flags;
    uint16_t idx;
    struct vring_used_elem ring[];
} __attribute__((packed));

/* VirtIO-GPU Command Structures */
struct virtio_gpu_ctrl_hdr {
    uint32_t type;
    uint32_t flags;
    uint64_t fence_id;
    uint32_t ctx_id;
    uint32_t padding;
} __attribute__((packed));

struct virtio_gpu_rect {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
} __attribute__((packed));

struct virtio_gpu_resource_create_2d {
    struct virtio_gpu_ctrl_hdr hdr;
    uint32_t resource_id;
    uint32_t format;
    uint32_t width;
    uint32_t height;
} __attribute__((packed));

struct virtio_gpu_mem_entry {
    uint64_t addr;
    uint32_t length;
    uint32_t padding;
} __attribute__((packed));

struct virtio_gpu_resource_attach_backing {
    struct virtio_gpu_ctrl_hdr hdr;
    uint32_t resource_id;
    uint32_t nr_entries;
    struct virtio_gpu_mem_entry entries[1];
} __attribute__((packed));

struct virtio_gpu_set_scanout {
    struct virtio_gpu_ctrl_hdr hdr;
    struct virtio_gpu_rect r;
    uint32_t scanout_id;
    uint32_t resource_id;
} __attribute__((packed));

struct virtio_gpu_transfer_to_host_2d {
    struct virtio_gpu_ctrl_hdr hdr;
    struct virtio_gpu_rect r;
    uint64_t offset;
    uint32_t resource_id;
    uint32_t padding;
} __attribute__((packed));

struct virtio_gpu_resource_flush {
    struct virtio_gpu_ctrl_hdr hdr;
    struct virtio_gpu_rect r;
    uint32_t resource_id;
    uint32_t padding;
} __attribute__((packed));

/* VirtIO-GPU Device State */
typedef struct {
    uint8_t   pci_bus;
    uint8_t   pci_slot;
    uint8_t   pci_func;
    uint16_t  device_id;
    bool      is_detected;
    bool      is_active;

    /* MMIO Register Mappings */
    volatile struct virtio_pci_common_cfg *common_cfg;
    volatile uint16_t *notify_addr;
    uint32_t  notify_off_mult;
    uintptr_t notify_bar_base;
    uint32_t  notify_offset;

    /* Virtqueue State (Queue 0 = controlq) */
    uint16_t  queue_size;
    struct vring_desc  *desc;
    struct vring_avail *avail;
    struct vring_used  *used;
    uint16_t  last_used_idx;

    /* Display Surface State */
    uint16_t  width;
    uint16_t  height;
    uint32_t  resource_id;
    uint32_t *framebuffer;
    uint64_t  framebuffer_phys;
    size_t    framebuffer_size;
} virtio_gpu_pci_t;

extern virtio_gpu_pci_t g_virtio_gpu;

/* Driver API */
int  virtio_gpu_pci_probe(void);
int  virtio_gpu_pci_init(uint16_t width, uint16_t height, void *fb_mem);
void virtio_gpu_pci_flush(uint32_t x, uint32_t y, uint32_t w, uint32_t h);
void virtio_gpu_pci_flush_all(void);

#ifdef __cplusplus
}
#endif

#endif /* BTRON_DRIVERS_VIRTIO_GPU_H */
