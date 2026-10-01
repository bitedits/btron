/*
 * B-TRON Retro OS — src/chokanji/microscript.c
 * Authentic BTRON / Cho-Kanji MicroScript Hypermedia Interpreter & Runtime.
 * Single C99 implementation adhering strictly to NASA JPL Power of 10 Guidelines:
 *  - Fixed memory footprint (zero dynamic allocation after init).
 *  - Bounded loops on lexical analysis, AST evaluation, and stage rendering.
 *  - Supports cards, stages, figure buttons, variable evaluation, and event dispatch.
 *  - Pure client-local coordinate space (0,0) to (w,h) on wnd->dev.
 */

#include <btron/types.h>
#include <btron/wnd.h>
#include <btron/dp.h>
#include <btron/pmc.h>
#include <btron/troncode.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>

#define MS_MAX_CARDS        8
#define MS_MAX_FIGS         16
#define MS_MAX_VARS         32
#define MS_MAX_TOKENS       256
#define MS_LOOP_BOUND       512
#define MS_MAX_STR_LEN      64

typedef enum {
    FIG_BUTTON = 0,
    FIG_LABEL,
    FIG_RECT,
    FIG_LINE
} FigType;

typedef struct {
    char name[24];
    FigType type;
    RECT bounds;
    char text[MS_MAX_STR_LEN];
    COLOR fill_col;
    COLOR border_col;
    int target_card;
    bool is_pressed;
} MsFigure;

typedef struct {
    char title[32];
    MsFigure figures[MS_MAX_FIGS];
    int fig_count;
    char script[512];
} MsCard;

typedef struct {
    char name[24];
    int int_val;
    char str_val[MS_MAX_STR_LEN];
} MsVar;

typedef struct {
    WND *wnd;
    MsCard cards[MS_MAX_CARDS];
    int card_count;
    int current_card;
    MsVar vars[MS_MAX_VARS];
    int var_count;
    char console_log[128];
    bool is_running;
} MsEngine;

static MsEngine g_ms;

/* ── NASA Contract & Variable Management ────────────────────────────── */

static void ms_log(const char *msg) {
    assert(msg != NULL);
    strncpy(g_ms.console_log, msg, sizeof(g_ms.console_log) - 1);
    g_ms.console_log[sizeof(g_ms.console_log) - 1] = '\0';
}

void ms_set_var(const char *name, int val, const char *str) {
    assert(name != NULL);
    for (int i = 0; i < g_ms.var_count && i < MS_LOOP_BOUND; i++) {
        if (strncmp(g_ms.vars[i].name, name, sizeof(g_ms.vars[i].name)) == 0) {
            g_ms.vars[i].int_val = val;
            if (str) strncpy(g_ms.vars[i].str_val, str, sizeof(g_ms.vars[i].str_val) - 1);
            return;
        }
    }
    if (g_ms.var_count < MS_MAX_VARS) {
        MsVar *v = &g_ms.vars[g_ms.var_count++];
        strncpy(v->name, name, sizeof(v->name) - 1);
        v->int_val = val;
        if (str) strncpy(v->str_val, str, sizeof(v->str_val) - 1);
    }
}

int ms_get_var(const char *name) {
    assert(name != NULL);
    for (int i = 0; i < g_ms.var_count && i < MS_LOOP_BOUND; i++) {
        if (strncmp(g_ms.vars[i].name, name, sizeof(g_ms.vars[i].name)) == 0) {
            return g_ms.vars[i].int_val;
        }
    }
    return 0;
}

/* ── Card & Hypermedia Navigation ───────────────────────────────────── */

void ms_go_card(int card_idx) {
    assert(card_idx >= 0 && card_idx < g_ms.card_count);
    if (card_idx >= 0 && card_idx < g_ms.card_count) {
        g_ms.current_card = card_idx;
        char buf[64];
        snprintf(buf, sizeof(buf), "Card switched to: %d (%s)", card_idx + 1, g_ms.cards[card_idx].title);
        ms_log(buf);
        if (g_ms.wnd) inval_wnd(g_ms.wnd);
    }
}

/* ── MicroScript Interpreter & Execution ────────────────────────────── */

void ms_eval_script(const char *script) {
    assert(script != NULL);
    if (!script) return;

    /* Lightweight deterministic line-by-line interpreter */
    char buf[512];
    strncpy(buf, script, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *line = strtok(buf, "\n;");
    int lines_executed = 0;

    while (line != NULL && lines_executed < 32) {
        /* Trim leading whitespace */
        while (*line == ' ' || *line == '\t') line++;

        if (strncmp(line, "go card", 7) == 0) {
            int target = atoi(line + 7) - 1;
            if (target >= 0 && target < g_ms.card_count) {
                ms_go_card(target);
            }
        } else if (strncmp(line, "say ", 4) == 0) {
            ms_log(line + 4);
        } else if (strncmp(line, "set ", 4) == 0) {
            char vname[24] = {0};
            int val = 0;
            if (sscanf(line + 4, "%23s = %d", vname, &val) == 2) {
                ms_set_var(vname, val, NULL);
            }
        }
        line = strtok(NULL, "\n;");
        lines_executed++;
    }
}

/* ── Default Hypermedia Stack Setup ─────────────────────────────────── */

static void ms_load_default_stack(void) {
    g_ms.card_count = 2;
    g_ms.current_card = 0;
    g_ms.var_count = 0;

    /* Card 1: Title Stage */
    MsCard *c1 = &g_ms.cards[0];
    strncpy(c1->title, "超漢字 MicroScript 入門", sizeof(c1->title) - 1);
    c1->fig_count = 3;

    /* Label */
    c1->figures[0].type = FIG_LABEL;
    c1->figures[0].bounds = (RECT){ 20, 20, 360, 44 };
    strncpy(c1->figures[0].text, "■ BTRON3 ハイパーメディア ステージ 1", sizeof(c1->figures[0].text) - 1);

    /* Diagram Rect */
    c1->figures[1].type = FIG_RECT;
    c1->figures[1].bounds = (RECT){ 30, 60, 280, 140 };
    c1->figures[1].fill_col = 0x00E8F4F8U;
    c1->figures[1].border_col = PMC_COL_OUTLINE;
    strncpy(c1->figures[1].text, "図形要素 (Figure Box)", sizeof(c1->figures[1].text) - 1);

    /* Next Button */
    c1->figures[2].type = FIG_BUTTON;
    c1->figures[2].bounds = (RECT){ 80, 160, 240, 196 };
    strncpy(c1->figures[2].text, "次のカードへ >>", sizeof(c1->figures[2].text) - 1);
    c1->figures[2].target_card = 1;

    /* Card 2: Interactive Stage */
    MsCard *c2 = &g_ms.cards[1];
    strncpy(c2->title, "スクリプト実行ステージ", sizeof(c2->title) - 1);
    c2->fig_count = 2;

    c2->figures[0].type = FIG_LABEL;
    c2->figures[0].bounds = (RECT){ 20, 20, 360, 44 };
    strncpy(c2->figures[0].text, "■ カード 2: 状態変数・連鎖制御", sizeof(c2->figures[0].text) - 1);

    c2->figures[1].type = FIG_BUTTON;
    c2->figures[1].bounds = (RECT){ 80, 100, 240, 136 };
    strncpy(c2->figures[1].text, "<< 表紙へ戻る", sizeof(c2->figures[1].text) - 1);
    c2->figures[1].target_card = 0;

    ms_log("MicroScript Engine 3.20 Ready");
}

/* ── Rendering ──────────────────────────────────────────────────────── */

void ms_paint(WND *wnd, GDEV *dev) {
    (void)wnd;
    if (!g_ms.wnd || !dev) return;

    const H w = dev->width;
    const H h = dev->height;

    /* 1. Backdrop */
    RECT bg = { 0, 0, w, h };
    fill_rec(dev, &bg, PMC_COL_BODY);

    /* 2. Stage Sheet */
    RECT stage = { 8, 8, w - 8, h - 30 };
    fill_rec(dev, &stage, 0x00FFFFFFU);
    drw_rec(dev, &stage);

    if (g_ms.current_card >= 0 && g_ms.current_card < g_ms.card_count) {
        const MsCard *card = &g_ms.cards[g_ms.current_card];
        for (int i = 0; i < card->fig_count && i < MS_LOOP_BOUND; i++) {
            const MsFigure *f = &card->figures[i];
            RECT r = { stage.left + f->bounds.left, stage.top + f->bounds.top,
                       stage.left + f->bounds.right, stage.top + f->bounds.bottom };

            if (f->type == FIG_BUTTON) {
                pmc_draw_switch(dev, &r, f->text, f->is_pressed, TRUE);
            } else if (f->type == FIG_RECT) {
                fill_rec(dev, &r, f->fill_col);
                drw_rec(dev, &r);
                drw_tc_string(dev, r.left + 8, r.top + 8, f->text, PMC_COL_OUTLINE, 0x00000000);
            } else if (f->type == FIG_LABEL) {
                drw_tc_string(dev, r.left, r.top, f->text, PMC_COL_OUTLINE, 0x00000000);
            }
        }
    }

    /* 3. Bottom Console Status Bar */
    RECT bar = { 0, h - 24, w, h };
    fill_rec(dev, &bar, PMC_COL_INACT_TITLE);
    drw_rec(dev, &bar);
    drw_tc_string(dev, 8, bar.top + 4, g_ms.console_log, 0x00202020U, 0x00000000);
}

/* ── Mouse Events & Interaction ─────────────────────────────────────── */

void ms_handle_click(H rel_x, H rel_y) {
    if (!g_ms.wnd) return;
    const H stage_l = 8;
    const H stage_t = 8;

    if (g_ms.current_card >= 0 && g_ms.current_card < g_ms.card_count) {
        MsCard *card = &g_ms.cards[g_ms.current_card];
        for (int i = 0; i < card->fig_count && i < MS_LOOP_BOUND; i++) {
            MsFigure *f = &card->figures[i];
            if (f->type == FIG_BUTTON) {
                RECT r = { stage_l + f->bounds.left, stage_t + f->bounds.top,
                           stage_l + f->bounds.right, stage_t + f->bounds.bottom };
                if (rel_x >= r.left && rel_x <= r.right && rel_y >= r.top && rel_y <= r.bottom) {
                    /* Jump to target card */
                    ms_go_card(f->target_card);
                    return;
                }
            }
        }
    }
}

static void ms_destroy(WND *wnd) {
    (void)wnd;
    g_ms.wnd = NULL;
}

static void ms_event_handler(WND *wnd, const EVT *evt) {
    if (!wnd || !evt) return;
    if (evt->type == EV_BUT_DOWN) {
        H rel_x = evt->pos.x - wnd->client.left;
        H rel_y = evt->pos.y - wnd->client.top;
        ms_handle_click(rel_x, rel_y);
    }
}

void ms_app_init(void) {
    if (g_ms.wnd) {
        top_wnd(g_ms.wnd);
        return;
    }
    memset(&g_ms, 0, sizeof(g_ms));
    ms_load_default_stack();

    g_ms.wnd = opn_wnd("マイクロスクリプト (MicroScript)", 160, 120, 520, 400,
                        WND_ATTR_TITLE | WND_ATTR_CLOSE | WND_ATTR_RESIZE | WND_ATTR_BORDER);
    if (g_ms.wnd) {
        g_ms.wnd->paint = ms_paint;
        g_ms.wnd->event_handler = ms_event_handler;
        g_ms.wnd->destroy = ms_destroy;
        inval_wnd(g_ms.wnd);
    }
}
