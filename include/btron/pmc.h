/*
 * B-TRON Retro OS — pmc.h
 * Cleanroom Cho-Kanji / PMC Window Frame & Control Specification.
 * Adheres to NASA JPL / DO-178C Level A Safety-Critical Guidelines:
 *  - Zero dynamic heap allocation in render paths.
 *  - Fixed bounded loops, strictly checked coordinate ranges.
 *  - Pure integer arithmetic, zero uninitialized state.
 */

#ifndef _BTRON_PMC_H_
#define _BTRON_PMC_H_

#include <btron/types.h>
#include <btron/dp.h>
#include <btron/wnd.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Window Manager Frame Appearance Modes */
typedef enum {
    WM_STYLE_BEOS = 0,     /* BeOS / CWM Sliding Gold Tab, Minimalist Grips */
    WM_STYLE_CHOKANJI = 1  /* Authentic PMC Cho-Kanji 3D Bevel, Full Titleband, Tombo */
} WmStyleMode;

extern WmStyleMode g_wm_style;

/* PMC Cho-Kanji Palette Tokens (Authentic B-right/V 4.0 & BTRON3) */
#define PMC_COL_OUTLINE       0x00000000U  /* Black borders */
#define PMC_COL_LIGHT         0x00FFFFFFU  /* 3D Bevel highlight */
#define PMC_COL_SHADOW        0x00202020U  /* 3D Bevel shadow */
#define PMC_COL_DARK_SHADOW   0x00000000U  /* Deep recessed shadow */
#define PMC_COL_GROUND        0x00204050U  /* Desktop backdrop (Teal / Slate) */
#define PMC_COL_ACT_TITLE     0x00808080U  /* Active titleband (Classic Mid-Grey) */
#define PMC_COL_INACT_TITLE   0x00D8D8D8U  /* Inactive titleband (Pale Grey) */
#define PMC_COL_ACT_TEXT      0x00FFFFFFU  /* Active title text */
#define PMC_COL_INACT_TEXT    0x00606060U  /* Inactive title text */
#define PMC_COL_BODY          0x00E0E0E0U  /* Window work area background */
#define PMC_COL_SBAR_KNOB     0x00E0E0E0U  /* Scrollbar thumb */
#define PMC_COL_SBAR_TRACK    0x00808080U  /* Scrollbar track */
#define PMC_COL_SBAR_TOMBO    0x00E0E000U  /* Scrollbar registration marks (Yellow) */
#define PMC_COL_SWITCH_SUNKEN 0x00C8C8C8U  /* Sunken switch background */
#define PMC_COL_SWITCH_RAISED 0x00E0E0E0U  /* Raised switch background */

/* Core Rendering & Style Management API */
void pmc_set_style(WmStyleMode style);
WmStyleMode pmc_get_style(void);

/* Cleanroom NASA-grade frame & widget renderers */
void pmc_draw_window_frame(GDEV *dev, WND *wnd);
void pmc_draw_switch(GDEV *dev, const RECT *r, const char *label, BOOL pressed, BOOL enabled);
void pmc_draw_scrollbar(GDEV *dev, const RECT *r, BOOL is_vert, H pos, H total, H view_len);
void pmc_draw_folder_tab(GDEV *dev, const RECT *r, const char *title, BOOL active);

/* Control hit tests for PMC Cho-Kanji layout */
BOOL pmc_hit_test_close(const WND *wnd, H x, H y);
BOOL pmc_hit_test_title(const WND *wnd, H x, H y);

#ifdef __cplusplus
}
#endif

#endif /* _BTRON_PMC_H_ */
