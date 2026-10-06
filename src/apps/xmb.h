/*
 * src/apps/xmb.h — PS3 XMB (XrossMediaBar) desktop application for B-System
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#ifndef BTRON_XMB_H
#define BTRON_XMB_H

#include <btron/wnd.h>

/* Opens the OpenGL XrossMediaBar on the B-System desktop */
WND* open_xmb_window(void);

/* Settings the bar draws with, read back for verification
 * (verify/tests/test_xmb_render.c).  Values are the rows' own: toggles 0/1,
 * Screen Brightness 0..100.  -1 for an unknown id. */
#define XMB_SETTING_THEME       0   /* Theme -> Colour      */
#define XMB_SETTING_WAVE        1   /* Theme -> Wave Background */
#define XMB_SETTING_PARTICLES   2   /* Theme -> Wave Particles  */
#define XMB_SETTING_BRIGHTNESS  3   /* Screen -> Screen Brightness */
#define XMB_SETTING_FADE        4   /* Screen -> Edge Fade      */
#define XMB_SETTING_SHADOWS     5   /* Screen -> Icon Shadows   */
int  xmb_setting(int which);

/* Where the cursor is: the band index, 0 the leftmost, and the depth of the
 * stacked lists, 1 at the band level and 2 once a row's menu is open.  xmb_rows
 * is how many rows the list under the cursor holds. */
int  xmb_band(void);
int  xmb_depth(void);
int  xmb_rows(void);

/* The waving sheet's own brightness field, as computed for the last frame: the
 * extremes and mean over the grid, and the number of samples.  Pass NULL for
 * any value not wanted. */
void xmb_ribbon_calibrate(float *min_b, float *max_b, float *mean_b, int *samples);

#endif /* BTRON_XMB_H */
