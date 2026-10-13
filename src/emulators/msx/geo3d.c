/*
 * geo3d.c — MSX Geo3D Geometry Coprocessor Implementation.
 *
 * Alexander Moncks / HRA! Geo3D hardware geometry pipeline emulator.
 * Synthesized for MSX FPGA configurations (e.g. V9968 Cartridge on Tang Nano 20K).
 */

#include "geo3d.h"
#include <string.h>

#define INSIDE 0
#define LEFT   1
#define RIGHT  2
#define BOTTOM 4
#define TOP    8

/* (a * b) / c with a 64-bit intermediate, truncating toward zero.  The PS2 image
 * links no libgcc, so a plain int64 '/' would call __divdi3; this uses only
 * shifts, compares and subtracts, which the compiler emits inline. */
static int32_t muldiv(int32_t a, int32_t b, int32_t c)
{
    uint64_t n, q = 0, r = 0;
    uint32_t d;
    int neg = 0, i;

    if (c == 0) return 0;
    if (a < 0) { a = -a; neg ^= 1; }
    if (b < 0) { b = -b; neg ^= 1; }
    if (c < 0) { c = -c; neg ^= 1; }
    n = (uint64_t)(uint32_t)a * (uint32_t)b;
    d = (uint32_t)c;
    for (i = 63; i >= 0; i--) {
        r = (r << 1) | ((n >> i) & 1u);
        if (r >= d) { r -= d; q |= (uint64_t)1 << i; }
    }
    return neg ? -(int32_t)(uint32_t)q : (int32_t)(uint32_t)q;
}

static int compute_outcode(int x, int y, int xmin, int ymin, int xmax, int ymax)
{
    int code = INSIDE;
    if (x < xmin)      code |= LEFT;
    else if (x > xmax) code |= RIGHT;
    if (y < ymin)      code |= BOTTOM;
    else if (y > ymax) code |= TOP;
    return code;
}

static int clip_line(int *x0, int *y0, int *x1, int *y1, int xmin, int ymin, int xmax, int ymax)
{
    int c0 = compute_outcode(*x0, *y0, xmin, ymin, xmax, ymax);
    int c1 = compute_outcode(*x1, *y1, xmin, ymin, xmax, ymax);

    while (1) {
        if (!(c0 | c1)) {
            return 1; /* Trivially accepted */
        }
        if (c0 & c1) {
            return 0; /* Trivially rejected */
        }
        int c_out = c0 ? c0 : c1;
        int x = 0, y = 0;

        if (c_out & TOP) {
            x = *x0 + (int)muldiv(*x1 - *x0, ymax - *y0, *y1 - *y0);
            y = ymax;
        } else if (c_out & BOTTOM) {
            x = *x0 + (int)muldiv(*x1 - *x0, ymin - *y0, *y1 - *y0);
            y = ymin;
        } else if (c_out & RIGHT) {
            y = *y0 + (int)muldiv(*y1 - *y0, xmax - *x0, *x1 - *x0);
            x = xmax;
        } else if (c_out & LEFT) {
            y = *y0 + (int)muldiv(*y1 - *y0, xmin - *x0, *x1 - *x0);
            x = xmin;
        }

        if (c_out == c0) {
            *x0 = x; *y0 = y;
            c0 = compute_outcode(*x0, *y0, xmin, ymin, xmax, ymax);
        } else {
            *x1 = x; *y1 = y;
            c1 = compute_outcode(*x1, *y1, xmin, ymin, xmax, ymax);
        }
    }
}

static inline int16_t get_w(const uint8_t *p)
{
    return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static inline uint16_t get_u(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

void geo3d_init(geo3d_t *g)
{
    if (!g) return;
    memset(g, 0, sizeof(*g));
    geo3d_reset(g);
}

void geo3d_reset(geo3d_t *g)
{
    if (!g) return;
    g->reg_idx = 0;
    memset(g->regs, 0, sizeof(g->regs));
    g->v_buf_len = 0;
    g->e_buf_len = 0;
    g->f_buf_len = 0;
    g->cnt_skipped = 0;
    g->cnt_drawn = 0;
    g->cnt_culled = 0;
}

void geo3d_attach_vdp(geo3d_t *g, v9938_t *v)
{
    if (g) g->vdp = v;
}

static void transform_vertex(geo3d_t *g, int16_t vx, int16_t vy, int16_t vz, geo3d_proj_t *out)
{
    int16_t m00 = get_w(&g->regs[0x00]);
    int16_t m01 = get_w(&g->regs[0x02]);
    int16_t m02 = get_w(&g->regs[0x04]);
    int16_t m10 = get_w(&g->regs[0x06]);
    int16_t m11 = get_w(&g->regs[0x08]);
    int16_t m12 = get_w(&g->regs[0x0A]);
    int16_t m20 = get_w(&g->regs[0x0C]);
    int16_t m21 = get_w(&g->regs[0x0E]);
    int16_t m22 = get_w(&g->regs[0x10]);

    int16_t tx = get_w(&g->regs[0x12]);
    int16_t ty = get_w(&g->regs[0x14]);
    int16_t tz = get_w(&g->regs[0x16]);

    uint16_t f     = get_u(&g->regs[0x18]);
    uint16_t cx    = get_u(&g->regs[0x1A]);
    uint16_t cy    = get_u(&g->regs[0x1C]);
    uint16_t znear = get_u(&g->regs[0x1E]);
    uint16_t w     = get_u(&g->regs[0x20]);
    uint16_t h     = get_u(&g->regs[0x22]);

    int32_t x = ((int32_t)m00 * vx + (int32_t)m01 * vy + (int32_t)m02 * vz) >> 14;
    int32_t y = ((int32_t)m10 * vx + (int32_t)m11 * vy + (int32_t)m12 * vz) >> 14;
    int32_t z = ((int32_t)m20 * vx + (int32_t)m21 * vy + (int32_t)m22 * vz) >> 14;

    x += tx;
    y += ty;
    z += tz;

    out->flags = 0;
    out->z = (int16_t)z;

    if (z <= (int32_t)znear) {
        out->flags |= 0x01u; /* NEAR plane rejection */
        out->valid = 0;
        out->sx = 0;
        out->sy = 0;
        return;
    }

    out->valid = 1;
    int32_t sx = (int32_t)cx + (x * (int32_t)f) / z;
    int32_t sy = (int32_t)cy - (y * (int32_t)f) / z;

    if (sx < -32768 || sx > 32767) out->flags |= 0x02u; /* OVFX */
    if (sy < -32768 || sy > 32767) out->flags |= 0x04u; /* OVFY */
    if (sx < 0)                    out->flags |= 0x10u; /* SX < 0 */
    if (sx >= (int32_t)w)          out->flags |= 0x20u; /* SX >= W */
    if (sy < 0)                    out->flags |= 0x40u; /* SY < 0 */
    if (sy >= (int32_t)h)          out->flags |= 0x80u; /* SY >= H */

    out->sx = (int16_t)sx;
    out->sy = (int16_t)sy;
}

static void geo3d_run(geo3d_t *g)
{
    uint8_t nvert = g->regs[0x42];
    uint8_t nedge = g->regs[0x43];
    uint8_t nface = g->regs[0x59];
    uint8_t color = g->regs[0x44];
    uint8_t lop   = (uint8_t)(g->regs[0x45] & 0x0Fu);
    uint16_t ypage = get_u(&g->regs[0x46]);
    uint8_t ctrl  = g->regs[0x48];
    uint16_t w    = get_u(&g->regs[0x20]);
    uint16_t h    = get_u(&g->regs[0x22]);

    g->cnt_skipped = 0;
    g->cnt_drawn = 0;
    g->cnt_culled = 0;

    if (!w) w = 256;
    if (!h) h = 212;

    /* 1. Transform Phase */
    for (uint32_t i = 0; i < nvert; i++) {
        transform_vertex(g, g->vram[i].x, g->vram[i].y, g->vram[i].z, &g->proj[i]);
    }

    if (!(ctrl & 0x02u)) {
        /* Wireframe Edge Mode */
        if (!g->vdp) return;
        for (uint32_t e = 0; e < nedge; e++) {
            uint8_t v0 = g->edges[e].v0;
            uint8_t v1 = g->edges[e].v1;
            if (v0 >= nvert || v1 >= nvert) {
                g->cnt_skipped++;
                continue;
            }
            geo3d_proj_t *p0 = &g->proj[v0];
            geo3d_proj_t *p1 = &g->proj[v1];

            if (!p0->valid || !p1->valid) {
                g->cnt_skipped++;
                continue;
            }

            /* Cohen-Sutherland outcode check: trivial reject */
            if (p0->flags & p1->flags & 0xF0u) {
                g->cnt_culled++;
                continue;
            }

            int x0 = p0->sx, y0 = p0->sy;
            int x1 = p1->sx, y1 = p1->sy;
            if (clip_line(&x0, &y0, &x1, &y1, 0, 0, (int)w - 1, (int)h - 1)) {
                v9938_draw_line(g->vdp, x0, y0 + (int)ypage, x1, y1 + (int)ypage, color, lop);
                g->cnt_drawn++;
            } else {
                g->cnt_culled++;
            }
        }
    } else {
        /* Filled Faces Mode */
        if (!g->vdp) return;
        int16_t m00 = get_w(&g->regs[0x00]);
        int16_t m01 = get_w(&g->regs[0x02]);
        int16_t m02 = get_w(&g->regs[0x04]);
        int16_t m10 = get_w(&g->regs[0x06]);
        int16_t m11 = get_w(&g->regs[0x08]);
        int16_t m12 = get_w(&g->regs[0x0A]);
        int16_t m20 = get_w(&g->regs[0x0C]);
        int16_t m21 = get_w(&g->regs[0x0E]);
        int16_t m22 = get_w(&g->regs[0x10]);

        int16_t lx = get_w(&g->regs[0x5A]);
        int16_t ly = get_w(&g->regs[0x5C]);
        int16_t lz = get_w(&g->regs[0x5E]);

        /* Light vector moved to model space: LM = transpose(R) * L */
        int32_t lmx = ((int32_t)m00 * lx + (int32_t)m10 * ly + (int32_t)m20 * lz) >> 14;
        int32_t lmy = ((int32_t)m01 * lx + (int32_t)m11 * ly + (int32_t)m21 * lz) >> 14;
        int32_t lmz = ((int32_t)m02 * lx + (int32_t)m12 * ly + (int32_t)m22 * lz) >> 14;

        for (uint32_t f = 0; f < nface; f++) {
            geo3d_face_t *fc = &g->faces[f];
            uint8_t i0 = fc->i0, i1 = fc->i1, i2 = fc->i2, i3 = fc->i3;
            if (i0 >= nvert || i1 >= nvert || i2 >= nvert) {
                g->cnt_skipped++;
                continue;
            }
            geo3d_proj_t *p0 = &g->proj[i0];
            geo3d_proj_t *p1 = &g->proj[i1];
            geo3d_proj_t *p2 = &g->proj[i2];
            geo3d_proj_t *p3 = (i3 < nvert) ? &g->proj[i3] : p2;

            if (!p0->valid || !p1->valid || !p2->valid || !p3->valid) {
                g->cnt_skipped++;
                continue;
            }

            /* Signed area backface culling */
            int64_t area = (int64_t)(p1->sx - p0->sx) * (p2->sy - p0->sy) -
                           (int64_t)(p1->sy - p0->sy) * (p2->sx - p0->sx);
            if (area <= 0) {
                g->cnt_culled++;
                continue;
            }

            /* Flat shading */
            int32_t dot = ((int32_t)lmx * fc->nx + (int32_t)lmy * fc->ny + (int32_t)lmz * fc->nz) >> 14;
            if (dot < 0) dot = 0;
            int32_t level = (7 * dot) >> 14;
            if (level > 6) level = 6;
            uint8_t col = (uint8_t)(fc->base_col + (uint8_t)level);

            /* Rasterize convex polygon */
            int pts[4][2] = {
                { p0->sx, p0->sy },
                { p1->sx, p1->sy },
                { p2->sx, p2->sy },
                { p3->sx, p3->sy }
            };
            int num_pts = (i3 == i2) ? 3 : 4;

            int min_y = pts[0][1], max_y = pts[0][1];
            for (int k = 1; k < num_pts; k++) {
                if (pts[k][1] < min_y) min_y = pts[k][1];
                if (pts[k][1] > max_y) max_y = pts[k][1];
            }
            if (min_y < 0) min_y = 0;
            if (max_y >= (int)h) max_y = (int)h - 1;

            for (int y = min_y; y <= max_y; y++) {
                int min_x = (int)w + 1000, max_x = -1000;
                for (int k = 0; k < num_pts; k++) {
                    int next_k = (k + 1) % num_pts;
                    int y_a = pts[k][1], y_b = pts[next_k][1];
                    int x_a = pts[k][0], x_b = pts[next_k][0];
                    if ((y_a <= y && y_b > y) || (y_b <= y && y_a > y)) {
                        int x_int = x_a + (int)muldiv(y - y_a, x_b - x_a, y_b - y_a);
                        if (x_int < min_x) min_x = x_int;
                        if (x_int > max_x) max_x = x_int;
                    }
                }
                if (min_x <= max_x) {
                    if (min_x < 0) min_x = 0;
                    if (max_x >= (int)w) max_x = (int)w - 1;
                    if (min_x <= max_x) {
                        v9938_draw_hline(g->vdp, min_x, max_x, y + (int)ypage, col, lop);
                    }
                }
            }
            g->cnt_drawn++;
        }
    }
}

uint8_t geo3d_read(geo3d_t *g, uint8_t port)
{
    if (!g) return 0xFFu;
    if ((port & 2u) == 0u) {
        /* Port 8Dh / 9Dh: Status / Flags */
        return g->regs[0x36]; /* Returns FLAGS */
    }

    /* Port 8Fh / 9Fh: Data */
    uint8_t idx = g->reg_idx;
    uint8_t val = 0;

    if (idx >= 0x30u && idx <= 0x36u) {
        val = g->regs[idx];
        g->reg_idx = (uint8_t)((idx + 1u) & 0x7Fu);
        return val;
    }

    if (idx == 0x4Au) val = (uint8_t)(g->cnt_skipped & 0xFFu);
    else if (idx == 0x4Bu) val = (uint8_t)((g->cnt_skipped >> 8) & 0xFFu);
    else if (idx == 0x4Cu) val = (uint8_t)(g->cnt_drawn & 0xFFu);
    else if (idx == 0x4Du) val = (uint8_t)((g->cnt_drawn >> 8) & 0xFFu);
    else if (idx == 0x4Eu) val = (uint8_t)(g->cnt_culled & 0xFFu);
    else if (idx == 0x4Fu) val = (uint8_t)((g->cnt_culled >> 8) & 0xFFu);
    else val = g->regs[idx];

    if (idx >= 0x4Au && idx <= 0x4Fu) {
        g->reg_idx = (uint8_t)((idx + 1u) & 0x7Fu);
    }
    return val;
}

void geo3d_write(geo3d_t *g, uint8_t port, uint8_t val)
{
    if (!g) return;
    if ((port & 2u) == 0u) {
        /* Port 8Dh / 9Dh: Register Index */
        g->reg_idx = (uint8_t)(val & 0x7Fu);
        g->v_buf_len = 0;
        g->e_buf_len = 0;
        g->f_buf_len = 0;
        return;
    }

    /* Port 8Fh / 9Fh: Register Data */
    uint8_t idx = g->reg_idx;

    if (idx == 0x50u) {
        /* Vertex Stream (6 bytes per vertex) */
        g->v_buf[g->v_buf_len++] = val;
        if (g->v_buf_len == 6u) {
            uint8_t ptr = g->regs[0x40]; /* VADDR */
            g->vram[ptr].x = get_w(&g->v_buf[0]);
            g->vram[ptr].y = get_w(&g->v_buf[2]);
            g->vram[ptr].z = get_w(&g->v_buf[4]);
            g->regs[0x40] = (uint8_t)((ptr + 1u) & 0xFFu);
            g->v_buf_len = 0;
        }
        return;
    }

    if (idx == 0x51u) {
        /* Edge Stream (2 bytes per edge) */
        g->e_buf[g->e_buf_len++] = val;
        if (g->e_buf_len == 2u) {
            uint8_t ptr = g->regs[0x41]; /* EADDR */
            g->edges[ptr].v0 = g->e_buf[0];
            g->edges[ptr].v1 = g->e_buf[1];
            g->regs[0x41] = (uint8_t)((ptr + 1u) & 0xFFu);
            g->e_buf_len = 0;
        }
        return;
    }

    if (idx == 0x52u) {
        /* Face Stream (11 bytes per face) */
        g->f_buf[g->f_buf_len++] = val;
        if (g->f_buf_len == 11u) {
            uint8_t ptr = g->regs[0x58]; /* FADDR */
            g->faces[ptr].i0 = g->f_buf[0];
            g->faces[ptr].i1 = g->f_buf[1];
            g->faces[ptr].i2 = g->f_buf[2];
            g->faces[ptr].i3 = g->f_buf[3];
            g->faces[ptr].nx = get_w(&g->f_buf[4]);
            g->faces[ptr].ny = get_w(&g->f_buf[6]);
            g->faces[ptr].nz = get_w(&g->f_buf[8]);
            g->faces[ptr].base_col = g->f_buf[10];
            g->regs[0x58] = (uint8_t)((ptr + 1u) & 0xFFu);
            g->f_buf_len = 0;
        }
        return;
    }

    /* Standard Register Write */
    g->regs[idx] = val;

    if (idx == 0x29u) {
        /* Writing high byte of VZ triggers immediate vertex calculation */
        geo3d_proj_t res;
        transform_vertex(g, get_w(&g->regs[0x24]), get_w(&g->regs[0x26]), get_w(&g->regs[0x28]), &res);
        g->regs[0x30] = (uint8_t)(res.sx & 0xFFu);
        g->regs[0x31] = (uint8_t)((res.sx >> 8) & 0xFFu);
        g->regs[0x32] = (uint8_t)(res.sy & 0xFFu);
        g->regs[0x33] = (uint8_t)((res.sy >> 8) & 0xFFu);
        g->regs[0x34] = (uint8_t)(res.z & 0xFFu);
        g->regs[0x35] = (uint8_t)((res.z >> 8) & 0xFFu);
        g->regs[0x36] = res.flags;
    } else if (idx == 0x48u) {
        /* Writing CTRL register with bit 0 = 1 triggers autonomous RUN */
        if (val & 0x01u) {
            geo3d_run(g);
        }
    }

    /* Auto-increment for sequential register setup */
    if (idx < 0x2Au || (idx >= 0x40u && idx <= 0x48u) || (idx >= 0x58u && idx <= 0x5Fu)) {
        g->reg_idx = (uint8_t)((idx + 1u) & 0x7Fu);
    }
}
