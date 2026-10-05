/*
 * B-System (BTRON 3.20) Cassette Audio Deck: audio_player.c
 * Classic Precision Hi-Fi Stereo Cassette Deck.
 *
 * Precision Industrial Design with SONY_* color palette,
 * Digital Audio LED/VFD Screen, Cassette Tape Type Selectors (Type I - IV),
 * Dolby B-C NR & HX Pro badges, Dual-Channel Segmented Peak VU Meters,
 * and animated dual rotating tape spools with variable tape pack thickness.
 * Conforms to TRON HMI Standard and ./b-system/ specifications.
 */

#include <btron/apps.h>
#include <btron/wnd.h>
#include <btron/dp.h>
#include <btron/app_menu.h>
#include <btron/troncode.h>

#include <btron/libc_shim.h>

#define CASSETTE_WND_W 660
#define CASSETTE_WND_H 430

/* SONY Precision Industrial Color Palette */
#define SONY_COL_CANVAS       0xFF16191E  /* Deep Titanium Slate */
#define SONY_COL_HEADER       0xFF1E222A  /* Brushed Graphite Header */
#define SONY_COL_PANEL        0xFF1A1D24  /* Sub-Panel / Bay Background */
#define SONY_COL_INSET        0xFF111317  /* Inset Monitor / Bay Black */
#define SONY_COL_BORDER_HI    0xFF3B4454  /* Bevel Highlight Border */
#define SONY_COL_BORDER_LO    0xFF0D0F12  /* Bevel Shadow Border */
#define SONY_COL_BORDER_MID   0xFF2A313D  /* Framing Hairline */
#define SONY_COL_GOLD         0xFFF59E0B  /* SONY Amber / Accent Gold */
#define SONY_COL_CYAN         0xFF38BDF8  /* Diagnostic High-Tech Cyan */
#define SONY_COL_GREEN        0xFF22C55E  /* LED Active Green */
#define SONY_COL_RED          0xFFEF4444  /* Peak / Rec Red */
#define SONY_COL_TEXT_WHITE   0xFFF8FAFC  /* Platinum White Text */
#define SONY_COL_TEXT_SILVER  0xFFCBD5E1  /* High-Readability Silver */
#define SONY_COL_TEXT_DIM     0xFF94A3B8  /* Technical Metric Gray */
#define SONY_COL_BTN_BG       0xFF282F3B  /* Tactile Button Face */
#define SONY_COL_TAPE_SHELL   0xFF232730  /* Cassette Polymer Shell */
#define SONY_COL_TAPE_BROWN   0xFF42220C  /* Magnetic Tape Oxide Brown */

/* Cassette Tape Types */
typedef enum {
    TAPE_TYPE_I_NORM = 0,   /* Normal Bias (Fe2O3) 120µs EQ */
    TAPE_TYPE_II_CRO2,      /* High Bias (CrO2) 70µs EQ */
    TAPE_TYPE_III_FECR,     /* Ferrichrome (FeCr) 70µs EQ */
    TAPE_TYPE_IV_METAL      /* Metal Bias (Metal Powder) 70µs EQ */
} CASSETTE_TAPE_TYPE;

/* Dolby Noise Reduction Modes */
typedef enum {
    DOLBY_OFF = 0,
    DOLBY_B,
    DOLBY_C
} DOLBY_MODE;

/* Audio Deck Transport State */
typedef enum {
    TAPE_STOP = 0,
    TAPE_PLAY,
    TAPE_REV_PLAY,
    TAPE_FF,
    TAPE_REW,
    TAPE_PAUSE,
    TAPE_REC
} TAPE_STATE;

typedef struct {
    TAPE_STATE         state;
    CASSETTE_TAPE_TYPE tape_type;
    DOLBY_MODE         dolby_mode;
    BOOL               hx_pro;
    BOOL               mpx_filter;
    BOOL               power_on;
    BOOL               tape_loaded;
    int                source_idx;     /* 0: TAPE, 1: LINE, 2: CD, 3: TUNER */
    int                track_num;
    int                seconds_elapsed;
    int                vu_left;        /* 0 .. 100 */
    int                vu_right;       /* 0 .. 100 */
    int                vu_peak_l;
    int                vu_peak_r;
    int                reel_angle;     /* 0 .. 359 degrees */
    int                bias_fine;      /* -20% .. +20% */
    int                rec_level;      /* 0 .. 10 */
} CassetteDeck;

static CassetteDeck g_deck;
static WND *g_audio_wnd = NULL;

static const char *k_sources[] = { "TAPE", "LINE", "CD", "TUNER" };
static const char *k_tape_names[] = { "TYPE I (NORM)", "TYPE II (CrO2)", "TYPE III (FeCr)", "TYPE IV (METAL)" };

/* Tactile beveled box helper */
static void draw_beveled_box(GDEV *dev, const RECT *r, COLOR fill_col, COLOR hi, COLOR lo) {
    if (!dev || !r) return;
    fill_rec(dev, r, fill_col);
    drw_lin(dev, r->left, r->top, r->right - 1, r->top);
    drw_lin(dev, r->left, r->top, r->left, r->bottom - 1);
    drw_lin(dev, r->left + 1, r->bottom - 1, r->right, r->bottom - 1);
    drw_lin(dev, r->right - 1, r->top + 1, r->right - 1, r->bottom);
    (void)hi;
    (void)lo;
}

/* Subelement Calculation Structures */
typedef struct {
    RECT bay_box;
    RECT window_box;
    H    spool_left_x;
    H    spool_left_y;
    H    spool_right_x;
    H    spool_right_y;
} CassetteBayLayout;

typedef struct {
    RECT screen_box;
    RECT counter_box;
    RECT vu_box_l;
    RECT vu_box_r;
    RECT tape_type_boxes[4];
    RECT dolby_box;
    RECT hx_box;
} LedScreenLayout;

typedef struct {
    RECT bar_box;
    RECT btn_rew;
    RECT btn_rev;
    RECT btn_play;
    RECT btn_ff;
    RECT btn_stop;
    RECT btn_pause;
    RECT btn_rec;
} TransportLayout;

typedef struct {
    CassetteBayLayout bay;
    LedScreenLayout   screen;
    TransportLayout   transport;
    RECT              footer_box;
} CassetteDeckLayout;

/* Pre-calculate all subelements before establishing bounding boxes */
static void calculate_cassette_layout(CassetteDeckLayout *layout, H w, H h) {
    if (!layout) return;

    /* 1. Cassette Bay (Left half) */
    H bay_w = 264;
    H bay_h = 175;
    layout->bay.bay_box.left = 16;
    layout->bay.bay_box.top = 58;
    layout->bay.bay_box.right = layout->bay.bay_box.left + bay_w;
    layout->bay.bay_box.bottom = layout->bay.bay_box.top + bay_h;

    layout->bay.window_box.left = layout->bay.bay_box.left + 16;
    layout->bay.window_box.top = layout->bay.bay_box.top + 28;
    layout->bay.window_box.right = layout->bay.bay_box.right - 16;
    layout->bay.window_box.bottom = layout->bay.bay_box.bottom - 18;

    layout->bay.spool_left_x = layout->bay.window_box.left + 54;
    layout->bay.spool_left_y = layout->bay.window_box.top + 64;
    layout->bay.spool_right_x = layout->bay.window_box.right - 54;
    layout->bay.spool_right_y = layout->bay.window_box.top + 64;

    /* 2. Digital LED/VFD Screen (Right half) */
    layout->screen.screen_box.left = layout->bay.bay_box.right + 12;
    layout->screen.screen_box.top = 58;
    layout->screen.screen_box.right = w - 16;
    layout->screen.screen_box.bottom = layout->screen.screen_box.top + bay_h;

    layout->screen.counter_box.left = layout->screen.screen_box.left + 12;
    layout->screen.counter_box.top = layout->screen.screen_box.top + 10;
    layout->screen.counter_box.right = layout->screen.counter_box.left + 100;
    layout->screen.counter_box.bottom = layout->screen.counter_box.top + 28;

    /* Tape Type Selector Buttons (4 badges) */
    H type_btn_w = 74;
    H type_btn_h = 18;
    H type_start_x = layout->screen.screen_box.left + 12;
    H type_y = layout->screen.screen_box.top + 46;
    for (int i = 0; i < 4; i++) {
        layout->screen.tape_type_boxes[i].left = type_start_x + i * (type_btn_w + 6);
        layout->screen.tape_type_boxes[i].top = type_y;
        layout->screen.tape_type_boxes[i].right = layout->screen.tape_type_boxes[i].left + type_btn_w;
        layout->screen.tape_type_boxes[i].bottom = type_y + type_btn_h;
    }

    /* Dolby & HX Pro Badges */
    layout->screen.dolby_box.left = layout->screen.screen_box.left + 12;
    layout->screen.dolby_box.top = type_y + 24;
    layout->screen.dolby_box.right = layout->screen.dolby_box.left + 140;
    layout->screen.dolby_box.bottom = layout->screen.dolby_box.top + 18;

    layout->screen.hx_box.left = layout->screen.dolby_box.right + 10;
    layout->screen.hx_box.top = layout->screen.dolby_box.top;
    layout->screen.hx_box.right = layout->screen.hx_box.left + 144;
    layout->screen.hx_box.bottom = layout->screen.dolby_box.bottom;

    /* VU Meter Boxes */
    layout->screen.vu_box_l.left = layout->screen.screen_box.left + 46;
    layout->screen.vu_box_l.top = layout->screen.hx_box.bottom + 22;
    layout->screen.vu_box_l.right = layout->screen.screen_box.right - 14;
    layout->screen.vu_box_l.bottom = layout->screen.vu_box_l.top + 14;

    layout->screen.vu_box_r.left = layout->screen.vu_box_l.left;
    layout->screen.vu_box_r.top = layout->screen.vu_box_l.bottom + 10;
    layout->screen.vu_box_r.right = layout->screen.vu_box_l.right;
    layout->screen.vu_box_r.bottom = layout->screen.vu_box_r.top + 14;

    /* 3. Transport Bar */
    layout->transport.bar_box.left = 16;
    layout->transport.bar_box.top = layout->bay.bay_box.bottom + 12;
    layout->transport.bar_box.right = w - 16;
    layout->transport.bar_box.bottom = layout->transport.bar_box.top + 48;

    H btn_w = 78;
    H btn_h = 32;
    H btn_y = layout->transport.bar_box.top + 8;
    H bx = layout->transport.bar_box.left + 10;
    H b_gap = 10;

    layout->transport.btn_rew.left = bx; layout->transport.btn_rew.top = btn_y;
    layout->transport.btn_rew.right = bx + btn_w; layout->transport.btn_rew.bottom = btn_y + btn_h;
    bx += btn_w + b_gap;

    layout->transport.btn_rev.left = bx; layout->transport.btn_rev.top = btn_y;
    layout->transport.btn_rev.right = bx + btn_w; layout->transport.btn_rev.bottom = btn_y + btn_h;
    bx += btn_w + b_gap;

    layout->transport.btn_play.left = bx; layout->transport.btn_play.top = btn_y;
    layout->transport.btn_play.right = bx + btn_w; layout->transport.btn_play.bottom = btn_y + btn_h;
    bx += btn_w + b_gap;

    layout->transport.btn_ff.left = bx; layout->transport.btn_ff.top = btn_y;
    layout->transport.btn_ff.right = bx + btn_w; layout->transport.btn_ff.bottom = btn_y + btn_h;
    bx += btn_w + b_gap;

    layout->transport.btn_stop.left = bx; layout->transport.btn_stop.top = btn_y;
    layout->transport.btn_stop.right = bx + btn_w; layout->transport.btn_stop.bottom = btn_y + btn_h;
    bx += btn_w + b_gap;

    layout->transport.btn_pause.left = bx; layout->transport.btn_pause.top = btn_y;
    layout->transport.btn_pause.right = bx + btn_w; layout->transport.btn_pause.bottom = btn_y + btn_h;
    bx += btn_w + b_gap;

    layout->transport.btn_rec.left = bx; layout->transport.btn_rec.top = btn_y;
    layout->transport.btn_rec.right = bx + btn_w; layout->transport.btn_rec.bottom = btn_y + btn_h;

    /* 4. Footer Box */
    layout->footer_box.left = 0;
    layout->footer_box.top = h - 36;
    layout->footer_box.right = w;
    layout->footer_box.bottom = h;
}

/* Draw a segmented digital LED VU Meter Bar */
static void draw_segmented_vu_bar(GDEV *dev, const RECT *r, int value, int peak_val) {
    if (!dev || !r) return;

    fill_rec(dev, r, SONY_COL_INSET);
    drw_rec(dev, r);

    int num_segments = 16;
    H total_w = r->right - r->left - 4;
    H seg_w = (total_w / num_segments) - 2;
    if (seg_w < 4) seg_w = 4;
    H seg_h = r->bottom - r->top - 4;

    int active_segs = (value * num_segments) / 100;
    int peak_seg = (peak_val * num_segments) / 100;

    for (int i = 0; i < num_segments; i++) {
        H sx = r->left + 2 + i * (seg_w + 2);
        H sy = r->top + 2;
        RECT sr = { sx, sy, sx + seg_w, sy + seg_h };

        COLOR col_on;
        if (i < 8)       col_on = SONY_COL_GREEN;   /* -30 to -5 dB */
        else if (i < 11) col_on = SONY_COL_CYAN;    /* -5 to 0 dB */
        else if (i < 13) col_on = SONY_COL_GOLD;    /* 0 to +3 dB */
        else             col_on = SONY_COL_RED;     /* +4 to +8 dB */

        if (i < active_segs) {
            fill_rec(dev, &sr, col_on);
        } else if (i == peak_seg && peak_seg > 0) {
            fill_rec(dev, &sr, SONY_COL_RED); /* Peak Hold indicator */
        } else {
            fill_rec(dev, &sr, 0xFF181C22);   /* Unlit segment */
        }
    }
}

/* Draw a stylized circular cassette spool hub with teeth */
static void draw_tape_spool(GDEV *dev, H cx, H cy, H pack_radius) {
    /* 1. Outer Tape Pack (Wound magnetic tape) */
    RECT pack_r = { cx - pack_radius, cy - pack_radius, cx + pack_radius, cy + pack_radius };
    fill_rec(dev, &pack_r, SONY_COL_TAPE_BROWN);
    /* Chamfer corners for round spool effect */
    drw_lin(dev, pack_r.left, pack_r.top, pack_r.left + 3, pack_r.top);
    drw_lin(dev, pack_r.right - 4, pack_r.top, pack_r.right - 1, pack_r.top);
    drw_lin(dev, pack_r.left, pack_r.bottom - 1, pack_r.left + 3, pack_r.bottom - 1);
    drw_lin(dev, pack_r.right - 4, pack_r.bottom - 1, pack_r.right - 1, pack_r.bottom - 1);

    /* 2. Plastic Spool Hub */
    H hub_r = 14;
    RECT hub_rct = { cx - hub_r, cy - hub_r, cx + hub_r, cy + hub_r };
    fill_rec(dev, &hub_rct, SONY_COL_TEXT_WHITE);
    drw_rec(dev, &hub_rct);

    /* 3. Center Spindle Hole */
    H hole_r = 5;
    RECT hole_rct = { cx - hole_r, cy - hole_r, cx + hole_r, cy + hole_r };
    fill_rec(dev, &hole_rct, SONY_COL_INSET);

    /* 4. Rotating Spindle Teeth (Cross spokes) */
    drw_lin(dev, cx - 11, cy, cx + 11, cy);
    drw_lin(dev, cx, cy - 11, cx, cy + 11);
    drw_lin(dev, cx - 8, cy - 8, cx + 8, cy + 8);
    drw_lin(dev, cx - 8, cy + 8, cx + 8, cy - 8);
}

/* Render Cassette Bay with Realistic Animated Tape Spools */
static void draw_cassette_bay(GDEV *dev, const CassetteBayLayout *bay) {
    if (!dev || !bay) return;

    /* Outer Bay Inset */
    draw_beveled_box(dev, &bay->bay_box, SONY_COL_PANEL, SONY_COL_BORDER_HI, SONY_COL_BORDER_LO);

    /* Bay Label */
    drw_tc_string(dev, bay->bay_box.left + 10, bay->bay_box.top + 8,
                  "CASSETTE MECHANISM WELL", SONY_COL_GOLD, SONY_COL_PANEL);

    /* Viewing Window Frame */
    draw_beveled_box(dev, &bay->window_box, SONY_COL_INSET, SONY_COL_BORDER_LO, SONY_COL_BORDER_HI);

    if (!g_deck.tape_loaded) {
        drw_tc_string(dev, bay->window_box.left + 35, bay->window_box.top + 50,
                      "[ NO CASSETTE LOADED ]", SONY_COL_TEXT_DIM, SONY_COL_INSET);
        return;
    }

    /* Cassette Shell Boundary */
    RECT shell_r = { bay->window_box.left + 6, bay->window_box.top + 6,
                     bay->window_box.right - 6, bay->window_box.bottom - 6 };
    draw_beveled_box(dev, &shell_r, SONY_COL_TAPE_SHELL, SONY_COL_BORDER_MID, SONY_COL_BORDER_LO);

    /* Cassette Label Strip */
    RECT label_r = { shell_r.left + 10, shell_r.top + 6, shell_r.right - 10, shell_r.top + 24 };
    fill_rec(dev, &label_r, SONY_COL_HEADER);
    drw_rec(dev, &label_r);

    const char *tape_brand = "SONY HF-ES 90";
    if (g_deck.tape_type == TAPE_TYPE_IV_METAL) tape_brand = "SONY Metal Master 90";
    else if (g_deck.tape_type == TAPE_TYPE_II_CRO2) tape_brand = "SONY UX-Pro 90";
    else if (g_deck.tape_type == TAPE_TYPE_III_FECR) tape_brand = "SONY Duad 90";

    drw_tc_string(dev, label_r.left + 10, label_r.top + 4, tape_brand, SONY_COL_GOLD, SONY_COL_HEADER);

    /* Tape Pack Thickness (Dynamically moves as tape plays) */
    int prog = g_deck.seconds_elapsed % 3600;
    H pack_l_r = 28 - (prog / 150);
    if (pack_l_r < 16) pack_l_r = 16;
    H pack_r_r = 16 + (prog / 150);
    if (pack_r_r > 28) pack_r_r = 28;

    /* Draw Left & Right Spools */
    draw_tape_spool(dev, bay->spool_left_x, bay->spool_left_y, pack_l_r);
    draw_tape_spool(dev, bay->spool_right_x, bay->spool_right_y, pack_r_r);

    /* Center Head Well & 3-Head Indicator */
    RECT head_well = { (bay->spool_left_x + bay->spool_right_x) / 2 - 24, shell_r.bottom - 22,
                       (bay->spool_left_x + bay->spool_right_x) / 2 + 24, shell_r.bottom - 4 };
    fill_rec(dev, &head_well, SONY_COL_INSET);
    drw_rec(dev, &head_well);
    drw_tc_string(dev, head_well.left + 5, head_well.top + 2, "3-HEAD", SONY_COL_CYAN, SONY_COL_INSET);
}

/* Render Audio Digital LED Screen */
static void draw_digital_led_screen(GDEV *dev, const LedScreenLayout *scr) {
    if (!dev || !scr) return;

    /* Inset Monitor Enclosure */
    draw_beveled_box(dev, &scr->screen_box, SONY_COL_INSET, SONY_COL_BORDER_LO, SONY_COL_BORDER_HI);
    drw_rec(dev, &scr->screen_box);

    /* 1. Digital Tape Counter */
    draw_beveled_box(dev, &scr->counter_box, 0xFF0A0C0E, SONY_COL_BORDER_LO, SONY_COL_BORDER_MID);
    char cnt_buf[32];
    int m = g_deck.seconds_elapsed / 60;
    int s = g_deck.seconds_elapsed % 60;
    snprintf(cnt_buf, sizeof(cnt_buf), "%02d : %02d", m, s);
    drw_tc_string(dev, scr->counter_box.left + 12, scr->counter_box.top + 6, cnt_buf, SONY_COL_CYAN, 0xFF0A0C0E);

    /* Mode Status Badge */
    RECT mode_box = { scr->counter_box.right + 8, scr->counter_box.top, scr->counter_box.right + 72, scr->counter_box.bottom };
    COLOR mode_col = SONY_COL_TEXT_DIM;
    const char *mode_str = "STOP";
    if (g_deck.state == TAPE_PLAY) { mode_col = SONY_COL_GREEN; mode_str = "PLAY >"; }
    else if (g_deck.state == TAPE_REC) { mode_col = SONY_COL_RED; mode_str = "REC (o)"; }
    else if (g_deck.state == TAPE_PAUSE) { mode_col = SONY_COL_GOLD; mode_str = "PAUSE ||"; }
    else if (g_deck.state == TAPE_FF) { mode_col = SONY_COL_CYAN; mode_str = "FF >>"; }
    else if (g_deck.state == TAPE_REW) { mode_col = SONY_COL_CYAN; mode_str = "REW <<"; }

    draw_beveled_box(dev, &mode_box, SONY_COL_HEADER, SONY_COL_BORDER_MID, SONY_COL_BORDER_LO);
    drw_tc_string(dev, mode_box.left + 8, mode_box.top + 6, mode_str, mode_col, SONY_COL_HEADER);

    /* Input Source Readout */
    drw_tc_string(dev, mode_box.right + 12, mode_box.top + 6, "INPUT:", SONY_COL_TEXT_DIM, SONY_COL_INSET);
    drw_tc_string(dev, mode_box.right + 60, mode_box.top + 6, k_sources[g_deck.source_idx], SONY_COL_GOLD, SONY_COL_INSET);

    /* 2. Cassette Type Selector Badges */
    for (int i = 0; i < 4; i++) {
        const RECT *tb = &scr->tape_type_boxes[i];
        BOOL active = (g_deck.tape_type == (CASSETTE_TAPE_TYPE)i);
        COLOR bg = active ? SONY_COL_HEADER : 0xFF0E1115;
        COLOR border = active ? SONY_COL_GOLD : SONY_COL_BORDER_MID;
        COLOR text_col = active ? SONY_COL_GOLD : SONY_COL_TEXT_DIM;

        draw_beveled_box(dev, tb, bg, border, SONY_COL_BORDER_LO);

        /* Indicator LED square */
        RECT led_r = { tb->left + 4, tb->top + 5, tb->left + 9, tb->top + 11 };
        fill_rec(dev, &led_r, active ? SONY_COL_GOLD : 0xFF1E242C);

        const char *short_type[] = { "I:NORM", "II:CrO2", "III:FeCr", "IV:METAL" };
        drw_tc_string(dev, tb->left + 12, tb->top + 3, short_type[i], text_col, bg);
    }

    /* 3. Dolby Noise Reduction & HX Pro System */
    draw_beveled_box(dev, &scr->dolby_box, SONY_COL_HEADER, SONY_COL_BORDER_MID, SONY_COL_BORDER_LO);

    /* Dolby Double-D Symbol and Text */
    COLOR dolby_col = (g_deck.dolby_mode != DOLBY_OFF) ? SONY_COL_CYAN : SONY_COL_TEXT_DIM;
    char dolby_str[32];
    if (g_deck.dolby_mode == DOLBY_B) snprintf(dolby_str, sizeof(dolby_str), "[DD] DOLBY B NR");
    else if (g_deck.dolby_mode == DOLBY_C) snprintf(dolby_str, sizeof(dolby_str), "[DD] DOLBY C NR");
    else snprintf(dolby_str, sizeof(dolby_str), "[DD] DOLBY NR: OFF");
    drw_tc_string(dev, scr->dolby_box.left + 8, scr->dolby_box.top + 3, dolby_str, dolby_col, SONY_COL_HEADER);

    /* HX Pro Headroom Extension Badge */
    draw_beveled_box(dev, &scr->hx_box, SONY_COL_HEADER, SONY_COL_BORDER_MID, SONY_COL_BORDER_LO);
    drw_tc_string(dev, scr->hx_box.left + 8, scr->hx_box.top + 3,
                  g_deck.hx_pro ? "HX PRO: ACTIVE" : "HX PRO: BYPASS",
                  g_deck.hx_pro ? SONY_COL_GREEN : SONY_COL_TEXT_DIM, SONY_COL_HEADER);

    /* 4. Stereo Digital VU / Peak Meters */
    drw_tc_string(dev, scr->screen_box.left + 12, scr->vu_box_l.top, "CH-L", SONY_COL_TEXT_WHITE, SONY_COL_INSET);
    draw_segmented_vu_bar(dev, &scr->vu_box_l, g_deck.vu_left, g_deck.vu_peak_l);

    drw_tc_string(dev, scr->screen_box.left + 12, scr->vu_box_r.top, "CH-R", SONY_COL_TEXT_WHITE, SONY_COL_INSET);
    draw_segmented_vu_bar(dev, &scr->vu_box_r, g_deck.vu_right, g_deck.vu_peak_r);

    /* Graduated dB Scale Line */
    drw_tc_string(dev, scr->vu_box_l.left, scr->vu_box_r.bottom + 4,
                  "-30 -20 -10  -5  -3   0  +2  +4  +6 +8 dB", SONY_COL_TEXT_DIM, SONY_COL_INSET);
}

/* Render Cassette Transport Controls */
static void draw_transport_bar(GDEV *dev, const TransportLayout *tr) {
    if (!dev || !tr) return;

    draw_beveled_box(dev, &tr->bar_box, SONY_COL_PANEL, SONY_COL_BORDER_HI, SONY_COL_BORDER_LO);

    const struct {
        const RECT *box;
        const char *label;
        BOOL        active;
        COLOR       active_col;
    } btns[] = {
        { &tr->btn_rew,   "<< REW",   (g_deck.state == TAPE_REW),   SONY_COL_CYAN },
        { &tr->btn_rev,   "< REV",    (g_deck.state == TAPE_REV_PLAY), SONY_COL_GREEN },
        { &tr->btn_play,  "PLAY >",   (g_deck.state == TAPE_PLAY),  SONY_COL_GREEN },
        { &tr->btn_ff,    "FF >>",    (g_deck.state == TAPE_FF),    SONY_COL_CYAN },
        { &tr->btn_stop,  "STOP",     (g_deck.state == TAPE_STOP),  SONY_COL_TEXT_WHITE },
        { &tr->btn_pause, "PAUSE",    (g_deck.state == TAPE_PAUSE), SONY_COL_GOLD },
        { &tr->btn_rec,   "(o) REC",  (g_deck.state == TAPE_REC),   SONY_COL_RED }
    };

    for (int i = 0; i < 7; i++) {
        const RECT *b = btns[i].box;
        BOOL is_act = btns[i].active;
        COLOR bg = is_act ? SONY_COL_HEADER : SONY_COL_BTN_BG;
        COLOR border = is_act ? btns[i].active_col : SONY_COL_BORDER_HI;
        COLOR text_col = is_act ? btns[i].active_col : SONY_COL_TEXT_WHITE;

        draw_beveled_box(dev, b, bg, border, SONY_COL_BORDER_LO);

        /* Indicator pip */
        if (is_act) {
            RECT pip = { b->left + 4, b->top + 4, b->left + 8, b->top + 8 };
            fill_rec(dev, &pip, btns[i].active_col);
        }

        drw_tc_string(dev, b->left + 12, b->top + 8, btns[i].label, text_col, bg);
    }
}

/* Master Paint Handler for SONY Cassette Deck */
static void audio_deck_paint(WND *wnd, GDEV *dev) {
    if (!wnd || !dev || dev->width < 10 || dev->height < 10) return;

    CassetteDeckLayout layout;
    calculate_cassette_layout(&layout, dev->width, dev->height);

    /* 1. Main SONY Titanium Canvas */
    RECT bg_r = { 0, 0, dev->width, dev->height };
    fill_rec(dev, &bg_r, SONY_COL_CANVAS);
    drw_rec(dev, &bg_r);

    /* 2. Brushed Graphite Header Bar */
    RECT hdr_r = { 0, 0, dev->width, 48 };
    fill_rec(dev, &hdr_r, SONY_COL_HEADER);
    drw_lin(dev, 0, 48, dev->width, 48);

    /* SONY Brand & High-End Deck Badges */
    drw_tc_string(dev, 14, 8, "NISSAN", SONY_COL_TEXT_WHITE, SONY_COL_HEADER);
    drw_tc_string(dev, 80, 8, "STEREO CASSETTE DECK (ステレオカセットデッキ)",
                  SONY_COL_GOLD, SONY_COL_HEADER);
    drw_tc_string(dev, 14, 24, "PRECISION AUDIO LAB: 3-HEAD / DUAL CAPSTAN DIRECT DRIVE SILENT MECHANISM",
                  SONY_COL_TEXT_SILVER, SONY_COL_HEADER);

    /* Status LEDs */
    drw_tc_string(dev, 14, 36, "* POWER: ON", SONY_COL_GREEN, SONY_COL_HEADER);
    drw_tc_string(dev, 140, 36, "* BIAS: AUTO CAL", SONY_COL_CYAN, SONY_COL_HEADER);
    drw_tc_string(dev, 290, 36, g_deck.tape_loaded ? "* TAPE: LOADED" : "* TAPE: EJECTED",
                  g_deck.tape_loaded ? SONY_COL_GOLD : SONY_COL_RED, SONY_COL_HEADER);

    /* 3. Cassette Mechanism Bay & Digital LED Screen */
    draw_cassette_bay(dev, &layout.bay);
    draw_digital_led_screen(dev, &layout.screen);

    /* 4. Transport Bar */
    draw_transport_bar(dev, &layout.transport);

    /* 5. Lower Dials & Calibration Area */
    H lower_y = layout.transport.bar_box.bottom + 10;
    drw_tc_string(dev, 18, lower_y, "BIAS FINE CAL: 0%", SONY_COL_TEXT_DIM, SONY_COL_CANVAS);
    drw_tc_string(dev, 170, lower_y, "REC LEVEL: 6.5", SONY_COL_TEXT_DIM, SONY_COL_CANVAS);
    drw_tc_string(dev, 320, lower_y, "TIMER: OFF", SONY_COL_TEXT_DIM, SONY_COL_CANVAS);
    drw_tc_string(dev, 440, lower_y, "AUTO TAPE SELECT: ON", SONY_COL_CYAN, SONY_COL_CANVAS);

    /* 6. High-Tech Telemetry Footer Bar */
    fill_rec(dev, &layout.footer_box, SONY_COL_HEADER);
    drw_lin(dev, 0, layout.footer_box.top, dev->width, layout.footer_box.top);

    /* Footer Action Buttons */
    H btn_h = 22;
    H btn_y = layout.footer_box.top + 7;

    RECT btn_close = { dev->width - 90, btn_y, dev->width - 10, btn_y + btn_h };
    draw_beveled_box(dev, &btn_close, SONY_COL_BTN_BG, SONY_COL_BORDER_HI, SONY_COL_BORDER_LO);
    drw_tc_string(dev, btn_close.left + 16, btn_y + 3, "Close", SONY_COL_TEXT_WHITE, SONY_COL_BTN_BG);

    RECT btn_reset = { btn_close.left - 105, btn_y, btn_close.left - 10, btn_y + btn_h };
    draw_beveled_box(dev, &btn_reset, SONY_COL_BTN_BG, SONY_COL_BORDER_HI, SONY_COL_BORDER_LO);
    drw_tc_string(dev, btn_reset.left + 8, btn_y + 3, "Reset 00:00", SONY_COL_CYAN, SONY_COL_BTN_BG);

    RECT btn_eject = { btn_reset.left - 95, btn_y, btn_reset.left - 10, btn_y + btn_h };
    draw_beveled_box(dev, &btn_eject, SONY_COL_BTN_BG, SONY_COL_BORDER_HI, SONY_COL_BORDER_LO);
    drw_tc_string(dev, btn_eject.left + 14, btn_y + 3, "Eject [^]", SONY_COL_GOLD, SONY_COL_BTN_BG);

    RECT btn_about = { btn_eject.left - 85, btn_y, btn_eject.left - 10, btn_y + btn_h };
    draw_beveled_box(dev, &btn_about, SONY_COL_BTN_BG, SONY_COL_BORDER_HI, SONY_COL_BORDER_LO);
    drw_tc_string(dev, btn_about.left + 10, btn_y + 3, "About...", SONY_COL_TEXT_SILVER, SONY_COL_BTN_BG);

    /* Telemetry text (strictly clipped before buttons) */
    char telem[96];
    snprintf(telem, sizeof(telem), "SONY ES | %s | %s",
             k_tape_names[g_deck.tape_type],
             (g_deck.dolby_mode == DOLBY_OFF) ? "NO NR" : (g_deck.dolby_mode == DOLBY_B ? "DOLBY-B" : "DOLBY-C"));
    drw_tc_string(dev, 14, layout.footer_box.top + 10, telem, SONY_COL_TEXT_DIM, SONY_COL_HEADER);
}

/* Event Handler for Cassette Deck Window */
static void audio_deck_event_handler(WND *wnd, const EVT *evt) {
    if (!wnd || !evt) return;

    if (evt->type == EV_BUT_DOWN) {
        H rel_x = evt->pos.x - wnd->client.left;
        H rel_y = evt->pos.y - wnd->client.top;
        H client_w = wnd->client.right - wnd->client.left;
        H client_h = wnd->client.bottom - wnd->client.top;
        if (client_w < 10 || client_h < 10) return;

        CassetteDeckLayout layout;
        calculate_cassette_layout(&layout, client_w, client_h);

        /* 1. Transport Button Clicks */
        if (rel_y >= layout.transport.btn_rew.top && rel_y <= layout.transport.btn_rew.bottom) {
            if (rel_x >= layout.transport.btn_rew.left && rel_x <= layout.transport.btn_rew.right) {
                g_deck.state = TAPE_REW;
            } else if (rel_x >= layout.transport.btn_rev.left && rel_x <= layout.transport.btn_rev.right) {
                g_deck.state = TAPE_REV_PLAY;
            } else if (rel_x >= layout.transport.btn_play.left && rel_x <= layout.transport.btn_play.right) {
                g_deck.state = TAPE_PLAY;
                if (!g_deck.tape_loaded) g_deck.tape_loaded = TRUE;
            } else if (rel_x >= layout.transport.btn_ff.left && rel_x <= layout.transport.btn_ff.right) {
                g_deck.state = TAPE_FF;
            } else if (rel_x >= layout.transport.btn_stop.left && rel_x <= layout.transport.btn_stop.right) {
                g_deck.state = TAPE_STOP;
                g_deck.vu_left = 0;
                g_deck.vu_right = 0;
            } else if (rel_x >= layout.transport.btn_pause.left && rel_x <= layout.transport.btn_pause.right) {
                g_deck.state = (g_deck.state == TAPE_PAUSE) ? TAPE_PLAY : TAPE_PAUSE;
            } else if (rel_x >= layout.transport.btn_rec.left && rel_x <= layout.transport.btn_rec.right) {
                g_deck.state = TAPE_REC;
                if (!g_deck.tape_loaded) g_deck.tape_loaded = TRUE;
            }
            inval_wnd(wnd);
            return;
        }

        /* 2. Tape Type Selector Clicks */
        for (int i = 0; i < 4; i++) {
            const RECT *tb = &layout.screen.tape_type_boxes[i];
            if (rel_x >= tb->left && rel_x <= tb->right &&
                rel_y >= tb->top && rel_y <= tb->bottom) {
                g_deck.tape_type = (CASSETTE_TAPE_TYPE)i;
                inval_wnd(wnd);
                return;
            }
        }

        /* 3. Dolby & HX Pro Clicks */
        if (rel_x >= layout.screen.dolby_box.left && rel_x <= layout.screen.dolby_box.right &&
            rel_y >= layout.screen.dolby_box.top && rel_y <= layout.screen.dolby_box.bottom) {
            g_deck.dolby_mode = (DOLBY_MODE)((g_deck.dolby_mode + 1) % 3);
            inval_wnd(wnd);
            return;
        }
        if (rel_x >= layout.screen.hx_box.left && rel_x <= layout.screen.hx_box.right &&
            rel_y >= layout.screen.hx_box.top && rel_y <= layout.screen.hx_box.bottom) {
            g_deck.hx_pro = !g_deck.hx_pro;
            inval_wnd(wnd);
            return;
        }

        /* 4. Footer Buttons */
        H btn_y = layout.footer_box.top + 7;
        H btn_h = 22;
        if (rel_y >= btn_y - 2 && rel_y <= btn_y + btn_h + 2) {
            if (rel_x >= client_w - 90 && rel_x <= client_w - 10) {
                cls_wnd(wnd);
                return;
            }
            if (rel_x >= client_w - 195 && rel_x <= client_w - 100) {
                g_deck.seconds_elapsed = 0;
                inval_wnd(wnd);
                return;
            }
            if (rel_x >= client_w - 290 && rel_x <= client_w - 205) {
                g_deck.tape_loaded = !g_deck.tape_loaded;
                if (!g_deck.tape_loaded) g_deck.state = TAPE_STOP;
                inval_wnd(wnd);
                return;
            }
            if (rel_x >= client_w - 375 && rel_x <= client_w - 300) {
                open_cassette_about_window();
                return;
            }
        }
    }
}

static void destroy_audio_player(WND *wnd) {
    (void)wnd;
    g_audio_wnd = NULL;
}

/* Open SONY Cassette Deck Window */
WND* open_audio_player_window(void) {
    if (g_audio_wnd) {
        top_wnd(g_audio_wnd);
        return g_audio_wnd;
    }

    /* Initialize Cassette Deck State */
    memset(&g_deck, 0, sizeof(CassetteDeck));
    g_deck.power_on = TRUE;
    g_deck.tape_loaded = TRUE;
    g_deck.state = TAPE_STOP;
    g_deck.tape_type = TAPE_TYPE_IV_METAL; /* SONY Metal Master */
    g_deck.dolby_mode = DOLBY_C;
    g_deck.hx_pro = TRUE;
    g_deck.mpx_filter = TRUE;
    g_deck.track_num = 1;
    g_deck.seconds_elapsed = 165; /* 02:45 */
    g_deck.source_idx = 0; /* TAPE */
    g_deck.vu_left = 68;
    g_deck.vu_right = 65;
    g_deck.vu_peak_l = 82;
    g_deck.vu_peak_r = 79;
    g_deck.rec_level = 7;

    H win_w = CASSETTE_WND_W;
    H win_h = CASSETTE_WND_H;
    H win_x = (1280 - win_w) / 2;
    H win_y = (800 - win_h) / 2;

    g_audio_wnd = opn_wnd("ステレオカセットデッキ (Cassette Deck)",
                          win_x, win_y, win_w, win_h,
                          WND_ATTR_TITLE | WND_ATTR_CLOSE | WND_ATTR_BORDER);
    if (!g_audio_wnd) return NULL;

    g_audio_wnd->paint = audio_deck_paint;
    g_audio_wnd->event_handler = audio_deck_event_handler;
    g_audio_wnd->destroy = destroy_audio_player;

    return g_audio_wnd;
}

/* About Window Creator for Cassette Deck */
WND* open_cassette_about_window(void) {
    return app_menu_create_about_dialog("Cassette", "カセットデッキ",
        "3-Head Dual Capstan Stereo Cassette Deck",
        "Brought to B-System by 5HT",
        240, 160);
}

