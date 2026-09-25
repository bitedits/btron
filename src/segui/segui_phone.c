/*
 * SegUI Phone: minimal Contacts / Chat / People / Settings screens
 * src/segui/segui_phone.c
 *
 * Four screens for a limited-screen phone - the kind of handset that has a
 * radio, a keypad and not much else.  Each screen is ONE widget in ONE tab,
 * because on a small vertical screen a screen *is* a list:
 *
 *   Contacts  a scrolling person list, grouped by section bands
 *   Chat      a message log, newest bubble at the bottom
 *   People    circles and recently-seen - the same list with forward markers
 *   Settings  the same list again, with switches and values
 *
 * Three of the four screens are therefore one widget type whose rows differ
 * only in what their right edge carries, which is the whole argument for
 * segmenting the screen rather than drawing it per device.  Nothing here names
 * a pixel: the segment table, the row pitch and the scroll length all come from
 * the profile, so a shorter panel shows fewer rows and scrolls the rest.
 *
 * Picking a person moves to the chat with their name in its header - the only
 * navigation these screens need.  The tab strip is reached with the centre soft
 * key ([切替]), with the digit keys, or with PAGE_UP / PAGE_DOWN.
 */

#include <btron/segui.h>

/* ── Contacts ──────────────────────────────────────────────────────────── */

static void phone_open_chat(SEGUI_APP *app, SEGUI_CTRL *list, int row);

static SEGUI_ROW s_contacts[] = {
    { SEGUI_ROW_HEADER, "お気に入り / FAVOURITES", "", 0, NULL },
    { SEGUI_ROW_PERSON, "Ken Sakamura",      "090-2841-0320", 0, phone_open_chat },
    { SEGUI_ROW_PERSON, "Ada Lovelace",      "LoRa 04",       0, phone_open_chat },
    { SEGUI_ROW_PERSON, "Grace Hopper",      "LoRa 11",       0, phone_open_chat },
    { SEGUI_ROW_HEADER, "全連絡先 / ALL CONTACTS", "", 0, NULL },
    { SEGUI_ROW_PERSON, "Claude Shannon",    "LoRa 27",       0, phone_open_chat },
    { SEGUI_ROW_PERSON, "Margaret Hamilton", "LoRa 31",       0, phone_open_chat },
    { SEGUI_ROW_PERSON, "Ken Thompson",      "LoRa 08",       0, phone_open_chat },
    { SEGUI_ROW_PERSON, "Barbara Liskov",    "LoRa 19",       0, phone_open_chat },
    { SEGUI_ROW_PERSON, "Butler Lampson",    "LoRa 42",       0, phone_open_chat },
    { SEGUI_ROW_PERSON, "B-System Help",     "117",           0, phone_open_chat },
    { SEGUI_ROW_ARROW,  "新規追加 / New contact", "", 0, NULL }
};

/* ── Chat ──────────────────────────────────────────────────────────────── */

static SEGUI_MSG s_chat[] = {
    { SEGUI_MSG_NOTE, "10:24  LoRa CH 23 / SF9" },
    { SEGUI_MSG_THEM, "着信した？ Did the beacon land?" },
    { SEGUI_MSG_ME,   "3 packets, all ACKed" },
    { SEGUI_MSG_THEM, "RSSI is -84 this far out" },
    { SEGUI_MSG_NOTE, "10:26" },
    { SEGUI_MSG_ME,   "Switching to SF9 for range" },
    { SEGUI_MSG_THEM, "Slower but further" },
    { SEGUI_MSG_ME,   "2.9 kbps. Enough for text." },
    { SEGUI_MSG_THEM, "Send the TAD body after" },
    { SEGUI_MSG_ME,   "OK" },
    { SEGUI_MSG_THEM, "Thanks" },
    { SEGUI_MSG_NOTE, "既読 / read 10:29" }
};

/* ── People ────────────────────────────────────────────────────────────── */

static SEGUI_ROW s_people[] = {
    { SEGUI_ROW_HEADER, "円 / CIRCLES", "", 0, NULL },
    { SEGUI_ROW_ARROW,  "家族 Family",     "4",  0, NULL },
    { SEGUI_ROW_ARROW,  "職場 Work",       "9",  0, NULL },
    { SEGUI_ROW_ARROW,  "LoRa Mesh",       "23", 0, NULL },
    { SEGUI_ROW_ARROW,  "B-Chat Regulars", "12", 0, NULL },
    { SEGUI_ROW_HEADER, "最近 / RECENTLY SEEN", "", 0, NULL },
    { SEGUI_ROW_PERSON, "Ada Lovelace",   "2 min",  0, phone_open_chat },
    { SEGUI_ROW_PERSON, "Claude Shannon", "18 min", 0, phone_open_chat },
    { SEGUI_ROW_PERSON, "Grace Hopper",   "1 hr",   0, phone_open_chat }
};

/* ── Settings ──────────────────────────────────────────────────────────── */

static SEGUI_ROW s_settings[] = {
    { SEGUI_ROW_HEADER, "無線 / LoRa RADIO", "", 0, NULL },
    { SEGUI_ROW_TOGGLE, "Radio",       "", SEGUI_ST_SELECTED, NULL },
    { SEGUI_ROW_VALUE,  "Channel",      "CH 23",   0, NULL },
    { SEGUI_ROW_VALUE,  "Spreading factor", "SF 9", 0, NULL },
    { SEGUI_ROW_VALUE,  "Output power", "+22 dBm", 0, NULL },
    { SEGUI_ROW_HEADER, "端末 / DEVICE", "", 0, NULL },
    { SEGUI_ROW_TOGGLE, "Beacon every 60 s", "", SEGUI_ST_SELECTED, NULL },
    { SEGUI_ROW_TOGGLE, "Sleep between packets", "", 0, NULL },
    { SEGUI_ROW_VALUE,  "Clock",        "10:30",   0, NULL },
    { SEGUI_ROW_ARROW,  "About SegUI", "", 0, NULL }
};

/* ── Navigation ────────────────────────────────────────────────────────── */

/* The chat is the phone's only detail screen: picking a person puts their name
 * in its header and moves the body segment to it. */
static void phone_open_chat(SEGUI_APP *app, SEGUI_CTRL *list, int row)
{
    int i;

    if (!app || !list || row < 0 || row >= list->n_row) return;
    if (list->rows[row].type != SEGUI_ROW_PERSON) return;

    for (i = 0; i < app->n_tab; i++) {
        SEGUI_TAB *ct = &app->tabs[i];
        int k;
        for (k = 0; k < ct->n_ctrl; k++) {
            if (ct->ctrl[k].type != SEGUI_W_CHAT) continue;
            /* segui_activate() has already revealed the picked row, so the
             * list is scrolled when this screen is reached again. */
            segui_set_peer(app, &ct->ctrl[k], list->rows[row].caption);
            segui_select_tab(app, i);
            return;
        }
    }
}

static SEGUI_CTRL *phone_add_rows(SEGUI_APP *app, int tab, SEGUI_ROW *rows,
                                  int n, int focus)
{
    SEGUI_CTRL *c = segui_add_list(app, tab, rows, n);
    /* The window starts at the top and the cursor starts on the first real
     * row, so a section header is visible before anything scrolls. */
    if (c) {
        c->row_focus = focus;
        c->row_top = 0;
    }
    return c;
}

void segui_phone_build(SEGUI_APP *app, const SEGUI_PROFILE *prof)
{
    if (!app) return;
    app->n_tab = 0;
    app->active_tab = 0;
    app->theme = NULL;
    app->prof = prof ? prof : &segui_profile_handset;
    app->pressed_ctrl = -1;
    segui_set_title(app, "SegUI Phone");
    segui_set_status(app, "10:30", "LoRa", 82);

    /* One widget per tab: on a vertical phone a screen *is* its list. */
    segui_add_tab(app, "Contacts", FALSE);
    segui_add_tab(app, "Chat", FALSE);
    segui_add_tab(app, "People", FALSE);
    segui_add_tab(app, "Settings", FALSE);

    phone_add_rows(app, SEGUI_SCREEN_CONTACTS, s_contacts,
                   (int)(sizeof(s_contacts) / sizeof(s_contacts[0])), 1);
    segui_add_chat(app, SEGUI_SCREEN_CHAT, s_chat,
                   (int)(sizeof(s_chat) / sizeof(s_chat[0])), "Ken Sakamura");
    phone_add_rows(app, SEGUI_SCREEN_PEOPLE, s_people,
                   (int)(sizeof(s_people) / sizeof(s_people[0])), 1);
    phone_add_rows(app, SEGUI_SCREEN_SETTINGS, s_settings,
                   (int)(sizeof(s_settings) / sizeof(s_settings[0])), 1);
}

static SEGUI_APP s_phone_app;

SEGUI_APP *segui_phone_app(const SEGUI_PROFILE *prof)
{
    if (s_phone_app.n_tab == 0) segui_phone_build(&s_phone_app, prof);
    else if (prof) s_phone_app.prof = prof;
    return &s_phone_app;
}

void segui_phone_show(SEGUI_APP *app, int screen)
{
    if (!app || screen < 0 || screen >= app->n_tab) return;
    app->active_tab = screen;
    if (screen == SEGUI_SCREEN_SETTINGS) {
        SEGUI_CTRL *c = segui_list_of(app);
        if (c) c->row_focus = 1;   /* the cursor starts below the header */
    }
}

int segui_phone_screen(void)
{
    return s_phone_app.active_tab;
}
