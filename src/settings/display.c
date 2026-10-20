/*
 * B-System (BTRON 3.20) Settings Applet: display (Display / 画面表示)
 * Conforming to ./b-system/settings/Display.html
 */

#include <btron/settings.h>
#include <btron/dp.h>
#include <btron/troncode.h>
#include <btron/wnd.h>
#include <btron/settings_icon.h>

#include <btron/libc_shim.h>

#if defined(BTRON_PS2_TARGET)
/* The two choices the third section offers, and the one remembered answer. */
#include "../gl/gl_backend_choice.h"
#endif

typedef struct {
    WND *wnd;
    BOOL checks[10];
    BOOL is_dirty;
} AppletState_display;

static AppletState_display g_state_display;

#if defined(BTRON_PS2_TARGET)
/*
 * The row's whole job is to change one remembered value and say so.
 *
 * The choice is read when a GL surface is created (`src/gl/egl_surface.c` asks
 * `gl_backend_selected()` there), so it takes effect at the next window that opens rather
 * than under the one that is on screen -- re-installing a rasteriser while an app holds a
 * context to the old one is not a thing this applet is in a position to do safely.
 *
 * The console row is why the setting is testable headlessly: it names the choice made and
 * the pipeline that answers, and on this image the EE/GS answer is TinyGL with a line
 * saying the GS backend is not in the image yet (`src/gl/gl_dispatch.c`).
 *
 * `snprintf` + `uart_puts_raw` rather than `printf`: this file is compiled
 * `-ffreestanding -nostdlib` for the PS2 image, where `<btron/libc_shim.h>` declares
 * `snprintf` and no `printf` at all, and the EE's console is the SIO row every other
 * measurement in this port is read from.
 */
extern void uart_puts_raw(const char *s);

static void display_select_gl_backend(GL_BACKEND backend)
{
    if (gl_backend_selected() == backend) return;

    gl_backend_select(backend);
    g_state_display.is_dirty = TRUE;

    char msg[96];
    snprintf(msg, sizeof(msg),
             "[DISPLAY] OpenGL PS2 choice: %s (applies to the next GL window)\n",
             backend == GL_BACKEND_EEGS ? "EE/GS" : "TinyGL");
    uart_puts_raw(msg);

    redraw_all_windows();
}
#endif

#if defined(BTRON_PS2_TARGET)
static void on_gl_tinygl(WND *wnd, SETTINGS_CONTROL *ctl) {
    (void)wnd; (void)ctl;
    display_select_gl_backend(GL_BACKEND_TINYGL);
}

static void on_gl_eegs(WND *wnd, SETTINGS_CONTROL *ctl) {
    (void)wnd; (void)ctl;
    display_select_gl_backend(GL_BACKEND_EEGS);
}

static SETTINGS_CONTROL s_display_ps2_controls[] = {
    { 6, SETTINGS_CTL_CUSTOM, 0, 0, { 18, 276, 480, 294 }, NULL, NULL, NULL, NULL, on_gl_tinygl },
    { 7, SETTINGS_CTL_CUSTOM, 0, 0, { 18, 298, 480, 316 }, NULL, NULL, NULL, NULL, on_gl_eegs }
};
#endif

static void on_display_default(WND *wnd) {
    (void)wnd;
    g_state_display.checks[0] = TRUE;
    g_state_display.checks[1] = FALSE;
    g_state_display.checks[2] = FALSE;
    g_state_display.checks[3] = TRUE;
    g_state_display.checks[4] = TRUE;
    g_state_display.checks[5] = TRUE;
#if defined(BTRON_PS2_TARGET)
    display_select_gl_backend(gl_backend_default());
#endif
    g_state_display.is_dirty = TRUE;
}

static void on_display_apply(WND *wnd) {
    (void)wnd;
    g_state_display.is_dirty = FALSE;
}

static SETTINGS_CONTROL s_display_controls[] = {
    { 0, SETTINGS_CTL_RADIO, 1, 0, { 18, 70, 480, 88 }, "1024 x 768 @ 60Hz (24-bit TrueColor) - Recommended", &g_state_display.checks[0], NULL, &g_state_display.is_dirty, NULL },
    { 1, SETTINGS_CTL_RADIO, 1, 0, { 18, 92, 480, 110 }, "800 x 600 @ 60Hz (Legacy PC-98 / VESA Mode)", &g_state_display.checks[1], NULL, &g_state_display.is_dirty, NULL },
    { 2, SETTINGS_CTL_RADIO, 1, 0, { 18, 114, 480, 132 }, "1920 x 1080 Full HD (VirtIO-GPU / Modern DRM)", &g_state_display.checks[2], NULL, &g_state_display.is_dirty, NULL },
    { 3, SETTINGS_CTL_CHECKBOX, 0, 0, { 18, 178, 480, 196 }, "Enable 1:1 Pixel Mapping (100% Standard Scale)", &g_state_display.checks[3], NULL, &g_state_display.is_dirty, NULL },
    { 4, SETTINGS_CTL_CHECKBOX, 0, 0, { 18, 200, 480, 218 }, "Apply sRGB TRON Standard Color Profile (Gamma 2.2)", &g_state_display.checks[4], NULL, &g_state_display.is_dirty, NULL },
    { 5, SETTINGS_CTL_CHECKBOX, 0, 0, { 18, 222, 480, 240 }, "Enable VESA Double Buffering Surface Compositor", &g_state_display.checks[5], NULL, &g_state_display.is_dirty, NULL }
};

static void paint_display_settings(WND *wnd, GDEV *dev) {
    if (!wnd || !dev) return;

    /* Background */
    RECT r = { 0, 0, dev->width, dev->height };
    fill_rec(dev, &r, COLOR_WHITE);
    drw_rec(dev, &r);

    /* Header Bar */
    settings_draw_header(dev, "display", "Display", "画面表示", "Resolution & DPI scaling");

    /* Section 1: Resolution & Video Mode */
    RECT s1 = { 10, 56, dev->width - 10, 150 };
    settings_draw_section(dev, &s1, " [1. Resolution & Video Mode] ");

    /* Section 2: DPI Scaling & Color Profiles */
    RECT s2 = { 10, 164, dev->width - 10, 258 };
    settings_draw_section(dev, &s2, " [2. DPI Scaling & Color Profiles] ");

    settings_draw_controls(dev, s_display_controls, 6);

    /* Section 3: OpenGL PS2 Rasterizer */
#if defined(BTRON_PS2_TARGET)
    RECT s3 = { 10, 262, dev->width - 10, 340 };
    settings_draw_section(dev, &s3, " [3. OpenGL PS2 Rasterizer] ");
    settings_draw_ui_radio(dev, 18, 276, "OpenGL PS2: TinyGL software rasterizer (EE CPU)",
                           gl_backend_selected() == GL_BACKEND_TINYGL, FALSE);
    settings_draw_ui_radio(dev, 18, 298, "OpenGL PS2: EE/GS hardware rasterizer (GS, in progress)",
                           gl_backend_selected() == GL_BACKEND_EEGS, FALSE);
#endif

    /* Action Buttons */
    settings_draw_action_buttons(dev, g_state_display.is_dirty);
}

static void handle_display_event(WND *wnd, const EVT *evt) {
    if (!wnd || !evt) return;
    if (settings_dispatch_control_event(wnd, s_display_controls, 6, evt)) return;
#if defined(BTRON_PS2_TARGET)
    if (settings_dispatch_control_event(wnd, s_display_ps2_controls, 2, evt)) return;
#endif
    settings_dispatch_action_buttons(wnd, evt, on_display_default, on_display_apply);
}

WND* open_display_settings_window(void) {
    memset(&g_state_display, 0, sizeof(AppletState_display));
    g_state_display.checks[0] = TRUE;
    g_state_display.checks[1] = FALSE;
    g_state_display.checks[2] = FALSE;
    g_state_display.checks[3] = TRUE;
    g_state_display.checks[4] = TRUE;
    g_state_display.checks[5] = TRUE;

    WND *wnd = opn_wnd("画面表示設定 (Screen Display/Monitor Settings)",
                       80, 45, 640, 430,
                       WND_ATTR_TITLE | WND_ATTR_CLOSE | WND_ATTR_BORDER);
    if (!wnd) return NULL;
    g_state_display.wnd = wnd;
    wnd->user_data = (VW)(uintptr_t)&g_state_display;
    wnd->paint = paint_display_settings;
    wnd->event_handler = handle_display_event;
    return wnd;
}
