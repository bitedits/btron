#include "orchestra_desk.h"
#include <apps/orch_chk.h>

static WND *s_wnd;

static void about_paint(WND *wnd, GDEV *dev)
{
    int w;
    int h;
    orch_check(dev != 0, __LINE__);
    orch_check(wnd != 0, __LINE__);
    if (!dev) return;
    w = (int)dev->width;
    h = (int)dev->height;
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    orch_fill(dev, 0, 0, w, h, ORCH_COL_CANVAS);
    orch_fill(dev, 0, 0, w, 28, ORCH_COL_PANEL_BG);
    orch_text(dev, 16, 8, "Orchestra", ORCH_COL_AMBER);
    if (h < 80) return;
    orch_text(dev, 16, 48, "midisrc   queue   midiroute   shimsynth", ORCH_COL_WHITE);
    if (h < 120) return;
    orch_text(dev, 16, 72, "tee   audiomixer   volume", ORCH_COL_WHITE);
    orch_text(dev, 16, 96, "audioconvert   virtiosndsink", ORCH_COL_WHITE);
    if (h < 180) return;
    orch_text(dev, 16, 132, "32 programs", ORCH_COL_SILVER);
    orch_text(dev, 16, 152, "one hall", ORCH_COL_SILVER);
    orch_text(dev, 16, 172, "eight inserts", ORCH_COL_SILVER);
    if (h < 220) return;
    orch_text(dev, 16, 204, "Rock     Classic     Orchestra", ORCH_COL_DIM);
}

static void about_event(WND *wnd, const EVT *evt)
{
    orch_check(wnd != 0, __LINE__);
    orch_check(evt != 0, __LINE__);
    if (!wnd || !evt) return;
    if (evt->type == EV_KEY_DOWN || evt->type == EV_BUT_DOWN) (void)cls_wnd(wnd);
}

static void on_destroy(WND *wnd)
{
    orch_check(wnd != 0, __LINE__);
    orch_check(s_wnd == 0 || s_wnd == wnd, __LINE__);
    (void)wnd;
    s_wnd = 0;
}

void orch_about_inval(void)
{
    OrchView *v = gst_view();
    orch_check(v != 0, __LINE__);
    orch_check(v->state <= ORCH_GST_PLAYING, __LINE__);
    if (s_wnd) (void)inval_wnd(s_wnd);
}

WND *orch_open_about(void)
{
    WND *w;
    UW attr;
    OrchView *v = gst_view();
    orch_check(v != 0, __LINE__);
    orch_check(v->state <= ORCH_GST_PLAYING, __LINE__);
    if (s_wnd) {
        (void)top_wnd(s_wnd);
        return s_wnd;
    }
    attr = WND_ATTR_TITLE | WND_ATTR_CLOSE | WND_ATTR_BORDER;
    w = opn_wnd("About", 240, 160, 520, 270, attr);
    if (!w) return 0;
    w->paint = about_paint;
    w->event_handler = about_event;
    w->destroy = on_destroy;
    w->menu_open = orch_menu_no;
    s_wnd = w;
    return w;
}

WND *open_orchestra_about_window(void);
