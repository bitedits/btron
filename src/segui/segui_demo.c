/*
 * SegUI Demo Application
 * src/segui/segui_demo.c
 *
 * The reference application for the SegUI toolkit: a window holding four
 * buttons and a text label, plus one tab per capability of the toolkit so a
 * host can see what changes when the segment table changes.
 *
 * This file contains no geometry at all.  Every rectangle on screen comes from
 * the profile the caller picked (or from segui_profile_for(), which picks one
 * from the device's pixel size), so the same five tabs below render as a
 * 480x800 handset with a status band and a tab strip, as a 480x272 appliance
 * panel with four columns across, and as a 640x480 retro desktop window with
 * caption buttons and no tab strip.
 */

#include <btron/segui.h>

#define DEMO_TAB_START 0   /* "SegUI": the label and the four capability tiles */
#define DEMO_TAB_WIDGETS 1
#define DEMO_TAB_THEME  2
#define DEMO_TAB_INFO   3
#define DEMO_TAB_APPS   4

/* ── Callbacks: state changes only, painting stays with the host ───────── */

/* A row of latched tiles behaves as a radio group: one stays chosen.  SegUI
 * has no separate radio widget because this five-line callback *is* one. */
static void demo_radio_pick(SEGUI_APP *app, SEGUI_CTRL *picked)
{
    SEGUI_TAB *t = segui_active_tab(app);
    int i;
    if (!t) return;
    for (i = 0; i < t->n_ctrl; i++)
        if (&t->ctrl[i] != picked) t->ctrl[i].state &= ~SEGUI_ST_SELECTED;
    picked->state |= SEGUI_ST_SELECTED;
}

static void demo_tile_pick(SEGUI_APP *app, SEGUI_CTRL *c)
{
    demo_radio_pick(app, c);
}

static void demo_theme_pick(SEGUI_APP *app, SEGUI_CTRL *c)
{
    const SEGUI_THEME *th = segui_theme_named(c->label);
    demo_radio_pick(app, c);
    /* An override survives a profile switch, which is the point: a look and a
     * segment table are independent axes of the same budget. */
    if (th) app->theme = th;
}

/* ── Info tab rows: a list is the cheapest way to state facts ──────────── */

static SEGUI_ROW s_info_rows[] = {
    { SEGUI_ROW_HEADER, "TOOLKIT",        "",                  0, NULL },
    { SEGUI_ROW_VALUE,  "Toolkit",       "SegUI 1.0",          0, NULL },
    { SEGUI_ROW_VALUE,  "Segments",      "STATUS/TAB/BODY",    0, NULL },
    { SEGUI_ROW_VALUE,  "Profiles",      "3 built in",         0, NULL },
    { SEGUI_ROW_VALUE,  "Widgets",       "button/check/switch",0, NULL },
    { SEGUI_ROW_HEADER, "CONSTRAINTS",    "",                  0, NULL },
    { SEGUI_ROW_VALUE,  "Heap after boot","none",              0, NULL },
    { SEGUI_ROW_VALUE,  "Form factor #if","none",              0, NULL },
    { SEGUI_ROW_VALUE,  "Instance size",  "static, caller held", 0, NULL }
};

/* ── Construction ──────────────────────────────────────────────────────── */

void segui_demo_build(SEGUI_APP *app, const SEGUI_PROFILE *prof)
{
    int home, widgets, theme, info, apps;

    if (!app) return;
    segui_app_init(app, "SegUI Demo", prof);
    segui_set_status(app, "10:30", "3G", 82);

    /* 1. SegUI: the brief - a text label and four buttons. */
    home = segui_add_tab(app, "SegUI", TRUE);
    segui_add_label(app, home, "SegUI: lightweight UI toolkit");
    {
        SEGUI_CTRL *fast = segui_add_button(app, home, "Fast", SEGUI_ICON_BOLT,
                                           demo_tile_pick);
        if (fast) fast->state = SEGUI_ST_SELECTED;   /* as in the reference */
    }
    segui_add_button(app, home, "Modular",  SEGUI_ICON_MODULAR,  demo_tile_pick);
    segui_add_button(app, home, "Scalable", SEGUI_ICON_SCALABLE, demo_tile_pick);
    segui_add_button(app, home, "Retro",    SEGUI_ICON_WINDOW,   demo_tile_pick);

    /* 2. Widgets: numbered buttons plus the two boolean widgets. */
    widgets = segui_add_tab(app, "Widgets", TRUE);
    segui_add_button(app, widgets, "1", SEGUI_ICON_NONE, NULL);
    segui_add_button(app, widgets, "2", SEGUI_ICON_NONE, NULL);
    segui_add_button(app, widgets, "3", SEGUI_ICON_NONE, NULL);
    segui_add_button(app, widgets, "4", SEGUI_ICON_NONE, NULL);
    segui_add_check(app, widgets, "Checkbox 1", TRUE, NULL);
    segui_add_check(app, widgets, "Checkbox 2", FALSE, NULL);
    segui_add_toggle(app, widgets, "Toggle Switch", TRUE, NULL);

    /* 3. Theme: the color tables are data, so switching one is a button. */
    theme = segui_add_tab(app, "Theme", TRUE);
    {
        SEGUI_CTRL *h = segui_add_button(app, theme, "Handset", SEGUI_ICON_NONE,
                                        demo_theme_pick);
        SEGUI_CTRL *r = segui_add_button(app, theme, "Retro", SEGUI_ICON_NONE,
                                        demo_theme_pick);
        SEGUI_CTRL *a = segui_add_button(app, theme, "Appliance", SEGUI_ICON_NONE,
                                        demo_theme_pick);
        if (h) h->state = SEGUI_ST_SELECTED | SEGUI_ST_LATCHED;
        if (r) r->state = SEGUI_ST_LATCHED;
        if (a) a->state = SEGUI_ST_LATCHED;
    }

    /* 4. Info: one list widget, which on every profile fills the body. */
    info = segui_add_tab(app, "Info", FALSE);
    segui_add_list(app, info, s_info_rows,
                   (int)(sizeof(s_info_rows) / sizeof(s_info_rows[0])));

    /* 5. Apps: what the toolkit is for - a phone, a panel, a desktop. */
    apps = segui_add_tab(app, "Apps", TRUE);
    {
        SEGUI_CTRL *p = segui_add_button(app, apps, "Phone", SEGUI_ICON_WINDOW,
                                        demo_tile_pick);
        SEGUI_CTRL *k = segui_add_button(app, apps, "Kiosk", SEGUI_ICON_MODULAR,
                                        demo_tile_pick);
        SEGUI_CTRL *i = segui_add_button(app, apps, "About", SEGUI_ICON_INFO,
                                        demo_tile_pick);
        if (p) p->state = SEGUI_ST_SELECTED | SEGUI_ST_LATCHED;
        if (k) k->state = SEGUI_ST_LATCHED;
        if (i) i->state = SEGUI_ST_LATCHED;
    }
    (void)apps;
}

/* One demo instance for hosts that only need to show it. */
static SEGUI_APP s_demo_app;

SEGUI_APP *segui_demo_app(const SEGUI_PROFILE *prof)
{
    if (s_demo_app.n_tab == 0) segui_demo_build(&s_demo_app, prof);
    else if (prof) s_demo_app.prof = prof;
    return &s_demo_app;
}
