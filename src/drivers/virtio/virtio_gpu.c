/*
 * virtio_gpu.c — VirtIO-GPU PCI Device Driver for B-System (BTRON 3.20)
 * Cleanroom implementation for QEMU q35 & PC Baremetal Workstations.
 * Supports both virtio-gpu-pci and virtio-vga modes with 2D hardware commands.
 */

#include <drivers/virtio_gpu.h>
#include <drivers/vesa.h>
#include <libstr.h>

extern void uart_puts_raw(const char *str);

/* ── Tiny formatters for serial logging ──────────────────────────────── */

static void vgpu_uart_hex32(uint32_t v) {
    static const char h[] = "0123456789ABCDEF";
    char buf[11];
    buf[0]='0'; buf[1]='x';
    for (int i = 9; i >= 2; i--) { buf[i] = h[v & 0xF]; v >>= 4; }
    buf[10] = '\0';
    uart_puts_raw(buf);
}

static void vgpu_uart_dec(uint32_t v) {
    char buf[12]; int i = 10; buf[11] = '\0';
    if (v == 0) { uart_puts_raw("0"); return; }
    while (v > 0 && i >= 0) { buf[i--] = (char)('0' + v % 10); v /= 10; }
    uart_puts_raw(buf + i + 1);
}

/* ── x86 PCI Configuration Port I/O ──────────────────────────────────── */

static inline void outl_pci(uint16_t port, uint32_t val) {
    __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint32_t inl_pci(uint16_t port) {
    uint32_t ret;
    __asm__ volatile("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static uint32_t pci_read_config32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t addr = (1U << 31) | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) | ((uint32_t)func << 8) | (offset & 0xFC);
    outl_pci(0x0CF8, addr);
    return inl_pci(0x0CFC);
}

static void pci_write_config32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val) {
    uint32_t addr = (1U << 31) | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) | ((uint32_t)func << 8) | (offset & 0xFC);
    outl_pci(0x0CF8, addr);
    outl_pci(0x0CFC, val);
}

static uint8_t pci_read_config8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t val = pci_read_config32(bus, slot, func, offset);
    return (uint8_t)((val >> ((offset & 3) * 8)) & 0xFF);
}

static uint16_t pci_read_config16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t val = pci_read_config32(bus, slot, func, offset);
    return (uint16_t)((val >> ((offset & 2) * 8)) & 0xFFFF);
}

static uintptr_t pci_get_bar_base(uint8_t bus, uint8_t slot, uint8_t func, uint8_t bar_idx) {
    uint8_t offset = (uint8_t)(0x10 + bar_idx * 4);
    uint32_t bar_low = pci_read_config32(bus, slot, func, offset);
    if (bar_low & 1) {
        return (uintptr_t)(bar_low & ~0x03U);
    }
    if ((bar_low & 0x06) == 0x04) {
        uint32_t bar_hi = pci_read_config32(bus, slot, func, offset + 4);
        return (uintptr_t)((bar_low & ~0x0FU) | ((uint64_t)bar_hi << 32));
    }
    return (uintptr_t)(bar_low & ~0x0FU);
}

/* ── Global VirtIO-GPU State ─────────────────────────────────────────── */

virtio_gpu_pci_t g_virtio_gpu = {0};

/* Ring Buffer Memory (Queue 0 = controlq, statically sized for bare-metal safety) */
#define VIRTIO_GPU_QUEUE_SIZE 16

static struct vring_desc  s_desc_ring[VIRTIO_GPU_QUEUE_SIZE] __attribute__((aligned(16)));
static struct {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[VIRTIO_GPU_QUEUE_SIZE];
} s_avail_ring __attribute__((aligned(2)));

static struct {
    uint16_t flags;
    uint16_t idx;
    struct vring_used_elem ring[VIRTIO_GPU_QUEUE_SIZE];
} s_used_ring __attribute__((aligned(4)));

/* Command & Response Transfer Buffers */
static union {
    struct virtio_gpu_resource_create_2d       create_2d;
    struct virtio_gpu_resource_attach_backing  attach;
    struct virtio_gpu_set_scanout              scanout;
    struct virtio_gpu_transfer_to_host_2d      transfer;
    struct virtio_gpu_resource_flush           flush;
} s_cmd_buf __attribute__((aligned(16)));

static struct virtio_gpu_ctrl_hdr s_resp_buf __attribute__((aligned(16)));

/* ── Synchronous Command Submission to Controlq ──────────────────────── */

static int virtio_gpu_send_command(const void *cmd, size_t cmd_len, void *resp, size_t resp_len) {
    if (!g_virtio_gpu.desc || !g_virtio_gpu.avail || !g_virtio_gpu.used || !g_virtio_gpu.notify_addr) {
        return -1;
    }

    /* Desc 0: Outgoing Command (Host Read-Only) */
    s_desc_ring[0].addr  = (uint64_t)(uintptr_t)cmd;
    s_desc_ring[0].len   = (uint32_t)cmd_len;
    s_desc_ring[0].flags = VRING_DESC_F_NEXT;
    s_desc_ring[0].next  = 1;

    /* Desc 1: Incoming Response (Host Write-Only) */
    s_desc_ring[1].addr  = (uint64_t)(uintptr_t)resp;
    s_desc_ring[1].len   = (uint32_t)resp_len;
    s_desc_ring[1].flags = VRING_DESC_F_WRITE;
    s_desc_ring[1].next  = 0;

    /* Publish head descriptor 0 to available ring */
    uint16_t avail_idx = s_avail_ring.idx;
    s_avail_ring.ring[avail_idx % VIRTIO_GPU_QUEUE_SIZE] = 0;
    __asm__ volatile("" ::: "memory");
    s_avail_ring.idx = avail_idx + 1;
    __asm__ volatile("" ::: "memory");

    /* Ring Doorbell */
    *g_virtio_gpu.notify_addr = 0;

    /* Spin-wait for completion with pause */
    uint16_t expected_used = g_virtio_gpu.last_used_idx + 1;
    for (uint32_t spin = 0; spin < 5000000; spin++) {
        __asm__ volatile("" ::: "memory");
        if (s_used_ring.idx == expected_used) {
            g_virtio_gpu.last_used_idx = s_used_ring.idx;
            return 0;
        }
        __asm__ volatile("pause");
    }

    uart_puts_raw("[VIRTIO-GPU] Command timeout waiting for host response!\r\n");
    return -2;
}

/* ── PCI Bus Scanning & Capability Discovery ─────────────────────────── */

int virtio_gpu_pci_probe(void) {
    if (g_virtio_gpu.is_detected) return 0;

    for (uint32_t bus = 0; bus < 4; bus++) {
        for (uint32_t slot = 0; slot < 32; slot++) {
            for (uint32_t func = 0; func < 8; func++) {
                uint16_t vendor = pci_read_config16(bus, slot, func, 0x00);
                if (vendor != VIRTIO_PCI_VENDOR_ID) continue;

                uint16_t device = pci_read_config16(bus, slot, func, 0x02);
                uint16_t subsys = pci_read_config16(bus, slot, func, 0x2E);

                bool is_gpu = (device == VIRTIO_PCI_DEVICE_GPU) ||
                              (subsys == VIRTIO_SUBSYS_GPU) ||
                              (device == VIRTIO_PCI_DEVICE_LEGACY_GPU && subsys == VIRTIO_SUBSYS_GPU);

                if (!is_gpu) continue;

                /* VirtIO GPU device identified */
                g_virtio_gpu.pci_bus   = (uint8_t)bus;
                g_virtio_gpu.pci_slot  = (uint8_t)slot;
                g_virtio_gpu.pci_func  = (uint8_t)func;
                g_virtio_gpu.device_id = device;
                g_virtio_gpu.is_detected = true;

                /* Enable Bus Master, Memory Space, and SERR */
                uint32_t cmd = pci_read_config32(bus, slot, func, 0x04);
                cmd |= (1U << 1) | (1U << 2) | (1U << 8);
                pci_write_config32(bus, slot, func, 0x04, cmd);

                uart_puts_raw("[VIRTIO-GPU] Found VirtIO GPU on PCI ");
                vgpu_uart_dec(bus);  uart_puts_raw(":");
                vgpu_uart_dec(slot); uart_puts_raw(".");
                vgpu_uart_dec(func);
                uart_puts_raw(" (ID="); vgpu_uart_hex32(device); uart_puts_raw(")\r\n");

                /* Walk PCI capabilities to locate Modern VirtIO structures */
                uint8_t cap_ptr = pci_read_config8(bus, slot, func, 0x34) & ~0x03U;
                int max_caps = 48;

                while (cap_ptr >= 0x40 && max_caps-- > 0) {
                    uint8_t cap_id = pci_read_config8(bus, slot, func, cap_ptr);
                    if (cap_id == 0x09) { /* PCI_CAP_ID_VNDR */
                        uint8_t cfg_type = pci_read_config8(bus, slot, func, cap_ptr + 3);
                        uint8_t bar      = pci_read_config8(bus, slot, func, cap_ptr + 4);
                        uint32_t offset  = pci_read_config32(bus, slot, func, cap_ptr + 8);

                        uintptr_t bar_base = pci_get_bar_base(bus, slot, func, bar);

                        if (cfg_type == VIRTIO_PCI_CAP_COMMON_CFG) {
                            g_virtio_gpu.common_cfg = (volatile struct virtio_pci_common_cfg *)(bar_base + offset);
                            uart_puts_raw("[VIRTIO-GPU] Common Config MMIO @ ");
                            vgpu_uart_hex32((uint32_t)(bar_base + offset));
                            uart_puts_raw(" (BAR"); vgpu_uart_dec(bar); uart_puts_raw(")\r\n");
                        } else if (cfg_type == VIRTIO_PCI_CAP_NOTIFY_CFG) {
                            uint32_t notify_mult = pci_read_config32(bus, slot, func, cap_ptr + 16);
                            g_virtio_gpu.notify_bar_base = bar_base;
                            g_virtio_gpu.notify_offset   = offset;
                            g_virtio_gpu.notify_off_mult = notify_mult;
                            uart_puts_raw("[VIRTIO-GPU] Notify Doorbell MMIO @ ");
                            vgpu_uart_hex32((uint32_t)(bar_base + offset));
                            uart_puts_raw(" (Multiplier="); vgpu_uart_dec(notify_mult); uart_puts_raw(")\r\n");
                        }
                    }
                    cap_ptr = pci_read_config8(bus, slot, func, cap_ptr + 1) & ~0x03U;
                }

                return 0;
            }
        }
    }

    return -1;
}

/* ── Driver Initialization & 2D Surface Setup ────────────────────────── */

int virtio_gpu_pci_init(uint16_t width, uint16_t height, void *fb_mem) {
    if (!g_virtio_gpu.is_detected) {
        if (virtio_gpu_pci_probe() != 0) {
            return -1;
        }
    }

    volatile struct virtio_pci_common_cfg *cfg = g_virtio_gpu.common_cfg;
    if (!cfg) {
        uart_puts_raw("[VIRTIO-GPU] Missing VirtIO common config MMIO!\r\n");
        return -2;
    }

    /* 1. Device Reset */
    cfg->device_status = 0;
    for (volatile int d = 0; d < 1000; d++) __asm__ volatile("pause");

    /* 2. Acknowledge & Driver Status */
    cfg->device_status = VIRTIO_STAT_ACKNOWLEDGE | VIRTIO_STAT_DRIVER;

    /* 3. Negotiate Features (Accept VirtIO 1.0 VERSION_1) */
    cfg->driver_feature_select = 1;
    cfg->driver_feature = (uint32_t)(VIRTIO_F_VERSION_1 >> 32);
    cfg->device_status |= VIRTIO_STAT_FEATURES_OK;

    if (!(cfg->device_status & VIRTIO_STAT_FEATURES_OK)) {
        uart_puts_raw("[VIRTIO-GPU] Features negotiation failed!\r\n");
        return -3;
    }

    /* 4. Configure Virtqueue 0 (controlq) */
    cfg->queue_select = 0;
    uint16_t q_size = cfg->queue_size;
    if (q_size > VIRTIO_GPU_QUEUE_SIZE) q_size = VIRTIO_GPU_QUEUE_SIZE;
    cfg->queue_size = q_size;
    g_virtio_gpu.queue_size = q_size;

    /* Program split ring physical addresses */
    cfg->queue_desc   = (uint64_t)(uintptr_t)s_desc_ring;
    cfg->queue_driver = (uint64_t)(uintptr_t)&s_avail_ring;
    cfg->queue_device = (uint64_t)(uintptr_t)&s_used_ring;
    cfg->queue_enable = 1;

    /* Compute doorbell address */
    uint16_t notify_off = cfg->queue_notify_off;
    g_virtio_gpu.notify_addr = (volatile uint16_t *)(g_virtio_gpu.notify_bar_base +
                                                     g_virtio_gpu.notify_offset +
                                                     notify_off * g_virtio_gpu.notify_off_mult);

    /* 5. Driver OK */
    cfg->device_status |= VIRTIO_STAT_DRIVER_OK;
    g_virtio_gpu.desc  = s_desc_ring;
    g_virtio_gpu.avail = (struct vring_avail *)&s_avail_ring;
    g_virtio_gpu.used  = (struct vring_used *)&s_used_ring;
    g_virtio_gpu.last_used_idx = 0;

    uart_puts_raw("[VIRTIO-GPU] Control Virtqueue 0 active (Ring size=");
    vgpu_uart_dec(q_size); uart_puts_raw(")\r\n");

    /* 6. Create 2D Resource */
    g_virtio_gpu.width            = width;
    g_virtio_gpu.height           = height;
    g_virtio_gpu.resource_id      = 1;
    g_virtio_gpu.framebuffer      = (uint32_t *)fb_mem;
    g_virtio_gpu.framebuffer_phys = (uint64_t)(uintptr_t)fb_mem;
    g_virtio_gpu.framebuffer_size = (size_t)width * height * 4;

    s_cmd_buf.create_2d.hdr.type   = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D;
    s_cmd_buf.create_2d.hdr.flags  = 0;
    s_cmd_buf.create_2d.hdr.fence_id = 0;
    s_cmd_buf.create_2d.hdr.ctx_id   = 0;
    s_cmd_buf.create_2d.hdr.padding  = 0;
    s_cmd_buf.create_2d.resource_id  = g_virtio_gpu.resource_id;
    s_cmd_buf.create_2d.format       = VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM;
    s_cmd_buf.create_2d.width        = width;
    s_cmd_buf.create_2d.height       = height;

    if (virtio_gpu_send_command(&s_cmd_buf.create_2d, sizeof(s_cmd_buf.create_2d),
                                &s_resp_buf, sizeof(s_resp_buf)) != 0 ||
        s_resp_buf.type != VIRTIO_GPU_RESP_OK_NODATA) {
        uart_puts_raw("[VIRTIO-GPU] RESOURCE_CREATE_2D failed!\r\n");
        return -4;
    }

    /* 7. Attach Backing Memory */
    s_cmd_buf.attach.hdr.type       = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING;
    s_cmd_buf.attach.hdr.flags      = 0;
    s_cmd_buf.attach.hdr.fence_id   = 0;
    s_cmd_buf.attach.hdr.ctx_id     = 0;
    s_cmd_buf.attach.hdr.padding    = 0;
    s_cmd_buf.attach.resource_id    = g_virtio_gpu.resource_id;
    s_cmd_buf.attach.nr_entries     = 1;
    s_cmd_buf.attach.entries[0].addr   = g_virtio_gpu.framebuffer_phys;
    s_cmd_buf.attach.entries[0].length = (uint32_t)g_virtio_gpu.framebuffer_size;
    s_cmd_buf.attach.entries[0].padding = 0;

    if (virtio_gpu_send_command(&s_cmd_buf.attach, sizeof(s_cmd_buf.attach),
                                &s_resp_buf, sizeof(s_resp_buf)) != 0 ||
        s_resp_buf.type != VIRTIO_GPU_RESP_OK_NODATA) {
        uart_puts_raw("[VIRTIO-GPU] RESOURCE_ATTACH_BACKING failed!\r\n");
        return -5;
    }

    /* 8. Set Scanout to Primary Display 0 */
    s_cmd_buf.scanout.hdr.type      = VIRTIO_GPU_CMD_SET_SCANOUT;
    s_cmd_buf.scanout.hdr.flags     = 0;
    s_cmd_buf.scanout.hdr.fence_id  = 0;
    s_cmd_buf.scanout.hdr.ctx_id    = 0;
    s_cmd_buf.scanout.hdr.padding   = 0;
    s_cmd_buf.scanout.scanout_id    = 0;
    s_cmd_buf.scanout.resource_id   = g_virtio_gpu.resource_id;
    s_cmd_buf.scanout.r.x           = 0;
    s_cmd_buf.scanout.r.y           = 0;
    s_cmd_buf.scanout.r.width       = width;
    s_cmd_buf.scanout.r.height      = height;

    if (virtio_gpu_send_command(&s_cmd_buf.scanout, sizeof(s_cmd_buf.scanout),
                                &s_resp_buf, sizeof(s_resp_buf)) != 0 ||
        s_resp_buf.type != VIRTIO_GPU_RESP_OK_NODATA) {
        uart_puts_raw("[VIRTIO-GPU] SET_SCANOUT failed!\r\n");
        return -6;
    }

    g_virtio_gpu.is_active = true;

    uart_puts_raw("[VIRTIO-GPU] 2D Scanout Active: ");
    vgpu_uart_dec(width); uart_puts_raw("x");
    vgpu_uart_dec(height);
    uart_puts_raw(" ARGB8888 Backing @ ");
    vgpu_uart_hex32((uint32_t)g_virtio_gpu.framebuffer_phys);
    uart_puts_raw(" [OK]\r\n");

    /* Initial display flush */
    virtio_gpu_pci_flush_all();

    return 0;
}

/* ── Transfer Rectangle & Flush Display ──────────────────────────────── */

void virtio_gpu_pci_flush(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    if (!g_virtio_gpu.is_active) return;

    /* Bound checking */
    if (x >= g_virtio_gpu.width || y >= g_virtio_gpu.height) return;
    if (x + w > g_virtio_gpu.width)  w = g_virtio_gpu.width - x;
    if (y + h > g_virtio_gpu.height) h = g_virtio_gpu.height - y;
    if (w == 0 || h == 0) return;

    /* 1. Transfer dirty rectangle to host */
    s_cmd_buf.transfer.hdr.type     = VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D;
    s_cmd_buf.transfer.hdr.flags    = 0;
    s_cmd_buf.transfer.hdr.fence_id = 0;
    s_cmd_buf.transfer.hdr.ctx_id   = 0;
    s_cmd_buf.transfer.hdr.padding  = 0;
    s_cmd_buf.transfer.resource_id  = g_virtio_gpu.resource_id;
    s_cmd_buf.transfer.offset       = (uint64_t)(y * g_virtio_gpu.width + x) * 4;
    s_cmd_buf.transfer.r.x          = x;
    s_cmd_buf.transfer.r.y          = y;
    s_cmd_buf.transfer.r.width      = w;
    s_cmd_buf.transfer.r.height     = h;
    s_cmd_buf.transfer.padding      = 0;

    virtio_gpu_send_command(&s_cmd_buf.transfer, sizeof(s_cmd_buf.transfer),
                            &s_resp_buf, sizeof(s_resp_buf));

    /* 2. Flush dirty rectangle to scanout */
    s_cmd_buf.flush.hdr.type     = VIRTIO_GPU_CMD_RESOURCE_FLUSH;
    s_cmd_buf.flush.hdr.flags    = 0;
    s_cmd_buf.flush.hdr.fence_id = 0;
    s_cmd_buf.flush.hdr.ctx_id   = 0;
    s_cmd_buf.flush.hdr.padding  = 0;
    s_cmd_buf.flush.resource_id  = g_virtio_gpu.resource_id;
    s_cmd_buf.flush.r.x          = x;
    s_cmd_buf.flush.r.y          = y;
    s_cmd_buf.flush.r.width      = w;
    s_cmd_buf.flush.r.height     = h;
    s_cmd_buf.flush.padding      = 0;

    virtio_gpu_send_command(&s_cmd_buf.flush, sizeof(s_cmd_buf.flush),
                            &s_resp_buf, sizeof(s_resp_buf));
}

void virtio_gpu_pci_flush_all(void) {
    if (!g_virtio_gpu.is_active) return;
    virtio_gpu_pci_flush(0, 0, g_virtio_gpu.width, g_virtio_gpu.height);
}
