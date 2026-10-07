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
 * is how many rows the list under the cursor holds, xmb_bands how many bands
 * the bar has. */
int  xmb_band(void);
int  xmb_depth(void);
int  xmb_rows(void);
int  xmb_bands(void);

/* The row the cursor rests on within that list, and the label of any row of it
 * ("" when the row is out of range).  A walk through folders is proved by which
 * row the bar ends up on, not by how many rows it drew. */
int  xmb_row(void);
const char *xmb_label(int row);

/* The glyph a row is drawn with, as its cell in the icon atlas's grid: a mounted
 * volume, a drawer and a body each carry their own, so a check can name the icon
 * it looked at instead of guessing it from the picture.  -1 for no such row. */
int  xmb_row_icon(int row);

/* The folder the Discs band is showing, with the VFS's "#<fid>" Real Body anchors
 * taken back out, and how many levels below the mounted-volume list that folder
 * sits - xmb_levels() is xmb_depth() - 1 for that band, where a Settings menu
 * would be one level too. */
const char *xmb_path(void);
int  xmb_levels(void);

/* How far the category bar has been pushed sideways by the depth of the stack, in
 * pixels: the animation's target is one icon width and a tenth per level below the
 * band, with no ceiling.  0 at the band level. */
int  xmb_bar_x(void);

/* The rows of the Settings band that hold a value of the B-System itself, rather
 * than a value of the bar: their values are read from and written to the system's
 * own state - the appearance settings' icon size, the PMC's window frame style,
 * the kernel's keyboard and mouse constants, the terminal settings, the input
 * method's.  These ids name those values, so that a test can ask the bar what such
 * a row shows right now - xmb_bound() reads it out of the system, exactly as the
 * row does - without copying the menu's tables or keeping a second value. */
enum {
    XMB_BIND_NONE = 0,
    XMB_BIND_ICON_SIZE,      /* appearance_get/set_icon_size()          */
    XMB_BIND_WM_STYLE,       /* pmc_get/set_style()                     */
    XMB_BIND_KBD_REPEAT,     /* g_kbd_repeat_enabled                    */
    XMB_BIND_KBD_DELAY,      /* g_kbd_repeat_delay_us                   */
    XMB_BIND_KBD_RATE,       /* g_kbd_repeat_interval_us                */
    XMB_BIND_MOUSE_STEP,     /* g_mouse_step_mult                       */
    XMB_BIND_MOUSE_PROFILE,  /* g_mouse_accel_profile                   */
    XMB_BIND_MOUSE_BUTTONS,  /* g_mouse_swap_select_adjust              */
    XMB_BIND_TERM_THEME,     /* terminal_get/set_settings(): theme      */
    XMB_BIND_TERM_FONT,      /*                               font_size */
    XMB_BIND_TERM_CURSOR,    /*                               cursor    */
    XMB_BIND_TERM_TRANSP,    /*                               transparency */
    XMB_BIND_TIP_MODE,       /* tip_get/set_mode()                      */
    XMB_BIND_TIP_KANA,       /* TIP_KEY_SETTINGS.jp_space_is_convert    */
    XMB_BIND_TIP_TAB,        /* TIP_KEY_SETTINGS.jp_tab_is_popup        */
    XMB_BIND_TIP_ARROW,      /* TIP_KEY_SETTINGS.arrow_nav_enabled      */
    XMB_BIND_TIP_NUMBER,     /* TIP_KEY_SETTINGS.num_select_enabled     */
    XMB_BIND_COUNT
};
int  xmb_bound(int bind);

/* The waving sheet's own brightness field, as computed for the last frame: the
 * extremes and mean over the grid, and the number of samples.  Pass NULL for
 * any value not wanted. */
void xmb_ribbon_calibrate(float *min_b, float *max_b, float *mean_b, int *samples);

#endif /* BTRON_XMB_H */
