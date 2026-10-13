/*
 * v9938.h — Yamaha V9938 / V9958 MSX2/MSX2+/Turbo R Video Display Processor.
 * Hardware emulation source for B-MSX.
 */

#ifndef _BTRON_EMULATOR_MSX_V9938_H_
#define _BTRON_EMULATOR_MSX_V9938_H_

#include <stdint.h>

#define V9938_VRAM_SIZE     131072u   /* 128 KiB */
#define V9938_REG_COUNT     64u
#define V9938_STAT_COUNT    16u

typedef struct {
    uint8_t  vram[V9938_VRAM_SIZE];
    uint8_t  regs[V9938_REG_COUNT];
    uint8_t  stat[V9938_STAT_COUNT];
    uint16_t pal[16];                /* RGB565 */

    uint16_t vram_ptr;               /* low 14 bits */
    uint8_t  latch;                  /* first byte of 2-byte sequence */
    uint8_t  pal_latch;              /* first byte of 2-byte palette sequence */
    uint8_t  ctrl_pending;
    uint8_t  pal_pending;
    uint8_t  write_mode;
    uint8_t  read_ahead;
    uint8_t  irq_pending;
    uint8_t  is_v9958;

    /* Command Engine registers (R32..R46) */
    uint16_t sx, sy;
    uint16_t dx, dy;
    uint16_t nx, ny;
    uint8_t  col;
    uint8_t  arg;
    uint8_t  cmd;

    /* Line interrupt and raster state */
    uint32_t t_line;
    uint16_t line;

    /* Transfer state for HMMC / LMMC */
    uint8_t  tr_active;
    uint8_t  tr_code;
    uint8_t  tr_op;
    int      tr_dix;
    int      tr_diy;
    uint16_t tr_x;
    uint16_t tr_y;
    uint16_t tr_bytes_per_line;
} v9938_t;

void    v9938_init(v9938_t *v, int is_v9958);
void    v9938_reset(v9938_t *v);
uint8_t v9938_read(v9938_t *v, uint8_t port);
void    v9938_write(v9938_t *v, uint8_t port, uint8_t val);
void    v9938_advance(v9938_t *v, uint32_t t_states);
int     v9938_vblank_taken(v9938_t *v);
void    v9938_render(v9938_t *v, uint16_t *fb);
uint8_t v9938_mode(const v9938_t *v);
void    v9938_draw_line(v9938_t *v, int x0, int y0, int x1, int y1, uint8_t color, uint8_t op);
void    v9938_draw_hline(v9938_t *v, int x0, int x1, int y, uint8_t color, uint8_t op);

#endif /* _BTRON_EMULATOR_MSX_V9938_H_ */
