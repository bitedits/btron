/*
 * B-TRON Retro OS — chokanji.h
 * Authentic BTRON3 / Cho-Kanji Application Suite API.
 * Single-header declarations for Cabinet, Document Editor, MicroScript, and PMC Style.
 */

#ifndef _BTRON_CHOKANJI_H_
#define _BTRON_CHOKANJI_H_

#include <btron/types.h>
#include <btron/dp.h>
#include <btron/wnd.h>
#include <btron/pmc.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Cabinet (キャビネット) ── */
typedef enum {
    CAB_SORT_NAME = 0,
    CAB_SORT_DATE,
    CAB_SORT_SIZE,
    CAB_SORT_KIND
} CabSortMode;

void cab_app_init(void);
void cab_paint(WND *wnd, GDEV *dev);
void cab_handle_click(H x, H y, bool is_double_click);
void cab_sort(CabSortMode mode);

/* ── MicroScript (マイクロスクリプト) ── */
void ms_app_init(void);
void ms_paint(WND *wnd, GDEV *dev);
void ms_handle_click(H x, H y);
void ms_go_card(int card_idx);
void ms_eval_script(const char *script);
void ms_set_var(const char *name, int val, const char *str);
int  ms_get_var(const char *name);

/* ── Clock (時計) ── */
void clk_app_init(void);

/* ── kconv / xfconv / unpack — Top-level window launchers ── */
WND* open_chokanji_cabinet_window(void);
WND* open_chokanji_microscript_window(void);
WND* open_chokanji_clock_window(void);
WND* open_chokanji_kconv_window(void);
WND* open_chokanji_xfconv_window(void);
WND* open_chokanji_unpack_window(void);

#ifdef __cplusplus
}
#endif

#endif /* _BTRON_CHOKANJI_H_ */
