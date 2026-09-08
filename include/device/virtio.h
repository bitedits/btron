/*
 * VirtIO 1.4 Driver Specifications for B-System / QEMU
 * Full C99 VirtIO-GPU (2D Display & SDL2 Blitter) & VirtIO-Console
 */

#ifndef _BTRON_KERNEL_VIRTIO_H_
#define _BTRON_KERNEL_VIRTIO_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* VirtIO MMIO Register Offsets */
#define VIRTIO_MMIO_MAGIC_VALUE         0x000
#define VIRTIO_MMIO_VERSION             0x004
#define VIRTIO_MMIO_DEVICE_ID           0x008
#define VIRTIO_MMIO_VENDOR_ID           0x00c
#define VIRTIO_MMIO_STATUS              0x070
#define VIRTIO_MMIO_QUEUE_NOTIFY        0x050

/* VirtIO Device Types */
#define VIRTIO_ID_NET                   1
#define VIRTIO_ID_BLOCK                 2
#define VIRTIO_ID_CONSOLE               3
#define VIRTIO_ID_GPU                   16
#define VIRTIO_ID_SOUND                 25

/* VirtIO-GPU Command Types */
#define VIRTIO_GPU_CMD_RESOURCE_CREATE_2D   0x0101
#define VIRTIO_GPU_CMD_RESOURCE_UNREF       0x0102
#define VIRTIO_GPU_CMD_SET_SCANOUT          0x0103
#define VIRTIO_GPU_CMD_RESOURCE_FLUSH       0x0104
#define VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D  0x0105
#define VIRTIO_GPU_RESP_OK_NODATA           0x1100

/* VirtIO-GPU Formats */
#define VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM    1
#define VIRTIO_GPU_FORMAT_R8G8B8A8_UNORM    3

/* VirtIO-GPU Control Packet Header */
struct virtio_gpu_ctrl_hdr {
    uint32_t type;
    uint32_t flags;
    uint64_t fence_id;
    uint32_t ctx_id;
    uint32_t padding;
};

/* VirtIO-GPU Rectangle */
struct virtio_gpu_rect {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
};

/* VirtIO-GPU 2D Resource Create Command Packet */
struct virtio_gpu_resource_create_2d {
    struct virtio_gpu_ctrl_hdr hdr;
    uint32_t resource_id;
    uint32_t format;
    uint32_t width;
    uint32_t height;
};

/* VirtIO-GPU 2D Transfer Command Packet */
struct virtio_gpu_transfer_to_host_2d {
    struct virtio_gpu_ctrl_hdr hdr;
    struct virtio_gpu_rect r;
    uint64_t offset;
    uint32_t resource_id;
    uint32_t padding;
};

/* VirtIO-GPU Resource Flush Command Packet */
struct virtio_gpu_resource_flush {
    struct virtio_gpu_ctrl_hdr hdr;
    struct virtio_gpu_rect r;
    uint32_t resource_id;
    uint32_t padding;
};

/* VirtIO-GPU Scanout Set Command Packet */
struct virtio_gpu_set_scanout {
    struct virtio_gpu_ctrl_hdr hdr;
    struct virtio_gpu_rect r;
    uint32_t scanout_id;
    uint32_t resource_id;
};

/* VirtIO-GPU SDL2 Blitter Context */
struct vio_gpu_sdl2_blitter {
    uint32_t host_window_width;
    uint32_t host_window_height;
    uint32_t active_resource_id;
    uint32_t format;
    void *framebuffer;
    size_t framebuffer_size;
    bool dirty;
};

/* VirtIO-GPU Device Handle */
struct vio_gpu_device {
    uint32_t active_scanout;
    uint32_t current_resource_id;
    struct vio_gpu_sdl2_blitter blitter;
};

/* Status Codes */
typedef int vio_status_t;
#define VIO_OK          0
#define VIO_ERR_INVAL  -1
#define VIO_ERR_NOMEM  -2

/* VirtIO-GPU & VirtIO-Console API */
void virtio_mmio_init(uintptr_t base_addr);
void virtio_driver_init_all(void);

/* VirtIO-GPU Driver Functions */
vio_status_t vio_gpu_init(struct vio_gpu_device **out_dev);
vio_status_t vio_gpu_create_resource_2d(struct vio_gpu_device *dev, uint32_t resource_id, uint32_t format, uint32_t width, uint32_t height);
vio_status_t vio_gpu_transfer_to_host_2d(struct vio_gpu_device *dev, uint32_t resource_id, uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint64_t offset);
vio_status_t vio_gpu_resource_flush(struct vio_gpu_device *dev, uint32_t resource_id, uint32_t x, uint32_t y, uint32_t width, uint32_t height);
vio_status_t vio_gpu_set_scanout(struct vio_gpu_device *dev, uint32_t scanout_id, uint32_t resource_id, uint32_t width, uint32_t height);
vio_status_t vio_gpu_sdl2_blit_frame(struct vio_gpu_device *dev, const void *src_buf, size_t buf_size);

/* VirtIO-Console Driver Functions */
void virtio_console_putchar(char c);
int virtio_console_write(const char *buf, uint32_t len);
int virtio_console_read(char *buf, uint32_t max_len, uint32_t *out_len);

/* VirtIO-Sound compatible PCM output (host-backed on the QEMU build). */
int virtio_sound_open(uint32_t sample_rate, uint8_t channels);
int virtio_sound_write(const int16_t *samples, size_t frames);
void virtio_sound_close(void);
bool virtio_sound_is_ready(void);

bool virtio_is_initialized(void);

#ifdef __cplusplus
}
#endif

#endif /* _BTRON_KERNEL_VIRTIO_H_ */
