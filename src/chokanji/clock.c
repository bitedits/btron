/*
 * B-TRON Retro OS — src/chokanji/clock.c
 * Authentic BTRON3 / Cho-Kanji Desktop Clock Accessory (時計).
 * Single C99 file adhering to NASA JPL Power of 10 Guidelines.
 *
 * Coordinates: Pure client-local space (0,0) to (w,h) on wnd->dev.
 */

#include <btron/types.h>
#include <btron/wnd.h>
#include <btron/dp.h>
#include <btron/pmc.h>
#include <btron/chokanji.h>
#include <btron/troncode.h>
#include <btron/clk.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <assert.h>

#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak))
#endif
ER get_tod(DATE_TIM *dt, TIMEZONE *tz) {
    if (!dt) return E_PAR;
    (void)tz;
#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    if (!t) return E_SYS;
    dt->year  = (H)(t->tm_year + 1900);
    dt->month = (H)(t->tm_mon + 1);
    dt->day   = (H)(t->tm_mday);
    dt->hour  = (H)(t->tm_hour);
    dt->min   = (H)(t->tm_min);
    dt->sec   = (H)(t->tm_sec);
    dt->msec  = 0;
#else
    dt->year = 2026; dt->month = 10; dt->day = 1;
    dt->hour = 12;   dt->min   = 0;  dt->sec  = 0; dt->msec = 0;
#endif
    return E_OK;
}

/* Precomputed sin/cos lookup table for 60 seconds (scaled * 1000) */
static const int s_sin60[60] = {
      0,  105,  208,  309,  407,  500,  588,  669,  743,  809,
    866,  914,  951,  978,  995, 1000,  995,  978,  951,  914,
    866,  809,  743,  669,  588,  500,  407,  309,  208,  105,
      0, -105, -208, -309, -407, -500, -588, -669, -743, -809,
   -866, -914, -951, -978, -995,-1000, -995, -978, -951, -914,
   -866, -809, -743, -669, -588, -500, -407, -309, -208, -105
};
static const int s_cos60[60] = {
   1000,  995,  978,  951,  914,  866,  809,  743,  669,  588,
    500,  407,  309,  208,  105,    0, -105, -208, -309, -407,
   -500, -588, -669, -743, -809, -866, -914, -951, -978, -995,
  -1000, -995, -978, -951, -914, -866, -809, -743, -669, -588,
   -500, -407, -309, -208, -105,    0,  105,  208,  309,  407,
    500,  588,  669,  743,  809,  866,  914,  951,  978,  995
};

#define CLK_FACE_RADIUS     56
#define CLK_WIN_W           160
#define CLK_WIN_H           180
#define CLK_LOOP_BOUND      60

typedef struct {
    WND *wnd;
    bool show_digital;
    bool show_seconds;
    int  last_sec;
} ClkState;

static ClkState g_clk;

static void clk_draw_hand(GDEV *dev, H cx, H cy, int step60, int radius, COLOR col) {
    if (!dev) return;
    if (step60 < 0) step60 = 0;
    if (step60 >= CLK_LOOP_BOUND) step60 %= CLK_LOOP_BOUND;
    const H ex = cx + (H)((s_sin60[step60] * radius) / 1000);
    const H ey = cy - (H)((s_cos60[step60] * radius) / 1000);
    drw_lin(dev, cx, cy, ex, ey);
    drw_lin(dev, cx + 1, cy, ex + 1, ey);
}

void clk_paint(WND *wnd, GDEV *dev) {
    (void)wnd;
    if (!g_clk.wnd || !dev) return;

    const H w = dev->width;
    const H h = dev->height;
    const H cx = w / 2;
    const H cy = 68;

    /* 1. Background fill in client space (0, 0, w, h) */
    RECT bg = { 0, 0, w, h };
    fill_rec(dev, &bg, PMC_COL_BODY);

    /* 2. Outer clock face plate with 3D sunken bevel */
    RECT face_r = { cx - CLK_FACE_RADIUS - 4, cy - CLK_FACE_RADIUS - 4,
                    cx + CLK_FACE_RADIUS + 5, cy + CLK_FACE_RADIUS + 5 };
    fill_rec(dev, &face_r, 0x00F8F8F8U);
    drw_rec(dev, &face_r);
    drw_lin(dev, face_r.left + 1, face_r.top + 1, face_r.right - 2, face_r.top + 1);
    drw_lin(dev, face_r.left + 1, face_r.top + 1, face_r.left + 1, face_r.bottom - 2);

    /* 3. Hour ticks */
    for (int hmark = 0; hmark < 12; hmark++) {
        const int tick_step = (hmark * 5) % CLK_LOOP_BOUND;
        const H ox = cx + (H)((s_sin60[tick_step] * (CLK_FACE_RADIUS - 4)) / 1000);
        const H oy = cy - (H)((s_cos60[tick_step] * (CLK_FACE_RADIUS - 4)) / 1000);
        const H ix = cx + (H)((s_sin60[tick_step] * (CLK_FACE_RADIUS - 9)) / 1000);
        const H iy = cy - (H)((s_cos60[tick_step] * (CLK_FACE_RADIUS - 9)) / 1000);
        drw_lin(dev, ox, oy, ix, iy);
    }

    /* 4. Current time */
    DATE_TIM dt;
    get_tod(&dt, NULL);
    const int sec  = dt.sec  % 60;
    const int min  = dt.min  % 60;
    const int hour = dt.hour % 12;
    g_clk.last_sec = sec;

    /* 5. Hour hand (dark grey/navy) */
    const int hour_step = (hour * 5 + min / 12) % CLK_LOOP_BOUND;
    clk_draw_hand(dev, cx, cy, hour_step, CLK_FACE_RADIUS * 55 / 100, PMC_COL_OUTLINE);

    /* 6. Minute hand */
    clk_draw_hand(dev, cx, cy, min, CLK_FACE_RADIUS * 80 / 100, PMC_COL_OUTLINE);

    /* 7. Second hand (vibrant red) */
    if (g_clk.show_seconds) {
        clk_draw_hand(dev, cx, cy, sec, CLK_FACE_RADIUS * 88 / 100, 0x00CC1100U);
    }

    /* 8. Center pivot dot */
    RECT cdot = { cx - 2, cy - 2, cx + 3, cy + 3 };
    fill_rec(dev, &cdot, PMC_COL_OUTLINE);

    /* 9. Digital readout plate */
    if (g_clk.show_digital) {
        char tbuf[32];
        snprintf(tbuf, sizeof(tbuf), "%02d:%02d:%02d", dt.hour, dt.min, dt.sec);
        RECT dig_r = { cx - 44, cy + CLK_FACE_RADIUS + 12, cx + 44, cy + CLK_FACE_RADIUS + 34 };
        fill_rec(dev, &dig_r, 0x00E8E8E8U);
        drw_rec(dev, &dig_r);
        drw_tc_string(dev, dig_r.left + 8, dig_r.top + 4, tbuf, PMC_COL_OUTLINE, 0x00000000);
    }

    /* 10. Status toggle prompt */
    drw_tc_string(dev, 12, h - 18, "[クリックで表示切替]", COLOR_GRAY, 0x00000000);
}

static void clk_destroy(WND *wnd) {
    (void)wnd;
    g_clk.wnd = NULL;
}

static void clk_event_handler(WND *wnd, const EVT *evt) {
    if (!wnd || !evt) return;
    if (evt->type == EV_BUT_DOWN) {
        /* Toggle modes on click */
        if (g_clk.show_digital && g_clk.show_seconds) {
            g_clk.show_seconds = false;
        } else if (g_clk.show_digital && !g_clk.show_seconds) {
            g_clk.show_digital = false;
            g_clk.show_seconds = true;
        } else {
            g_clk.show_digital = true;
            g_clk.show_seconds = true;
        }
        inval_wnd(wnd);
    }
}

void clk_app_init(void) {
    if (g_clk.wnd) {
        top_wnd(g_clk.wnd);
        return;
    }
    memset(&g_clk, 0, sizeof(g_clk));
    g_clk.show_digital = true;
    g_clk.show_seconds = true;

    g_clk.wnd = opn_wnd("時計 (Clock)", 20, 60, CLK_WIN_W, CLK_WIN_H,
                         WND_ATTR_TITLE | WND_ATTR_CLOSE | WND_ATTR_BORDER);
    if (g_clk.wnd) {
        g_clk.wnd->paint = clk_paint;
        g_clk.wnd->event_handler = clk_event_handler;
        g_clk.wnd->destroy = clk_destroy;
        inval_wnd(g_clk.wnd);
    }
}
