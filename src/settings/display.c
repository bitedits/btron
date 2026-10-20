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
    g_state_display.checks[6] = TRUE;
    g_state_display.checks[7] = FALSE;
}

static void on_gl_eegs(WND *wnd, SETTINGS_CONTROL *ctl) {
    (void)wnd; (void)ctl;
    display_select_gl_backend(GL_BACKEND_EEGS);
    g_state_display.checks[6] = FALSE;
    g_state_display.checks[7] = TRUE;
}
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
    GL_BACKEND def = gl_backend_default();
    display_select_gl_backend(def);
    g_state_display.checks[6] = (def == GL_BACKEND_TINYGL);
    g_state_display.checks[7] = (def == GL_BACKEND_EEGS);
#endif
    g_state_display.is_dirty = TRUE;
}

static void on_display_apply(WND *wnd) {
    (void)wnd;
    g_state_display.is_dirty = FALSE;
}

static SETTINGS_CONTROL s_display_controls[] = {
    { 0, SETTINGS_CTL_RADIO, 1, 0, {0}, "1024 x 768 @ 60Hz (24-bit TrueColor) - Recommended", &g_state_display.checks[0], NULL, &g_state_display.is_dirty, NULL },
    { 1, SETTINGS_CTL_RADIO, 1, 0, {0}, "800 x 600 @ 60Hz (Legacy PC-98 / VESA Mode)", &g_state_display.checks[1], NULL, &g_state_display.is_dirty, NULL },
    { 2, SETTINGS_CTL_RADIO, 1, 0, {0}, "1920 x 1080 Full HD (VirtIO-GPU / Modern DRM)", &g_state_display.checks[2], NULL, &g_state_display.is_dirty, NULL },
    { 3, SETTINGS_CTL_CHECKBOX, 0, 0, {0}, "Enable 1:1 Pixel Mapping (100% Standard Scale)", &g_state_display.checks[3], NULL, &g_state_display.is_dirty, NULL },
    { 4, SETTINGS_CTL_CHECKBOX, 0, 0, {0}, "Apply sRGB TRON Standard Color Profile (Gamma 2.2)", &g_state_display.checks[4], NULL, &g_state_display.is_dirty, NULL },
    { 5, SETTINGS_CTL_CHECKBOX, 0, 0, {0}, "Enable VESA Double Buffering Surface Compositor", &g_state_display.checks[5], NULL, &g_state_display.is_dirty, NULL },
#if defined(BTRON_PS2_TARGET)
    { 6, SETTINGS_CTL_RADIO, 2, 0, {0}, "OpenGL PS2: TinyGL software rasterizer (EE CPU)", &g_state_display.checks[6], NULL, &g_state_display.is_dirty, on_gl_tinygl },
    { 7, SETTINGS_CTL_RADIO, 2, 0, {0}, "OpenGL PS2: EE/GS hardware rasterizer (GS, in progress)", &g_state_display.checks[7], NULL, &g_state_display.is_dirty, on_gl_eegs }
#endif
};

static SETTINGS_SECTION s_display_sections[] = {
    { " [1. Resolution & Video Mode] ", 1, 0, 3, 56, 94, {0} },
    { " [2. DPI Scaling & Color Profiles] ", 1, 3, 3, 164, 94, {0} },
#if defined(BTRON_PS2_TARGET)
    { " [3. OpenGL PS2 Rasterizer] ", 1, 6, 2, 262, 78, {0} },
#endif
};

static void paint_display_settings(WND *wnd, GDEV *dev) {
    if (!wnd || !dev) return;
    settings_draw_dialog(dev, "display", "Display", "画面表示", "Resolution & DPI scaling",
                         s_display_sections, (int)(sizeof(s_display_sections) / sizeof(s_display_sections[0])),
                         s_display_controls, (int)(sizeof(s_display_controls) / sizeof(s_display_controls[0])),
                         g_state_display.is_dirty);
}

static void handle_display_event(WND *wnd, const EVT *evt) {
    if (!wnd || !evt) return;
    settings_dispatch_dialog_event(wnd,
                                   s_display_sections, (int)(sizeof(s_display_sections) / sizeof(s_display_sections[0])),
                                   s_display_controls, (int)(sizeof(s_display_controls) / sizeof(s_display_controls[0])),
                                   evt, on_display_default, on_display_apply);
}

WND* open_display_settings_window(void) {
    memset(&g_state_display, 0, sizeof(AppletState_display));
    g_state_display.checks[0] = TRUE;
    g_state_display.checks[1] = FALSE;
    g_state_display.checks[2] = FALSE;
    g_state_display.checks[3] = TRUE;
    g_state_display.checks[4] = TRUE;
    g_state_display.checks[5] = TRUE;
#if defined(BTRON_PS2_TARGET)
    g_state_display.checks[6] = (gl_backend_selected() == GL_BACKEND_TINYGL);
    g_state_display.checks[7] = (gl_backend_selected() == GL_BACKEND_EEGS);
#endif

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
