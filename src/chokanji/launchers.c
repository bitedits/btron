/*
 * B-TRON Retro OS — src/chokanji/launchers.c
 * Cho-Kanji App Suite: top-level window-launcher entry points.
 * Each open_chokanji_*_window() function delegates to the corresponding
 * ported cleanroom app module in src/chokanji/.
 */

#include <btron/chokanji.h>
#include <btron/wnd.h>
#include <btron/app_menu.h>
#include <btron/pmc.h>

/* ── Forward declarations from individual app modules ────────── */
/* cab.c */
extern void cab_app_init(void);
/* clock.c */
extern void clk_app_init(void);
/* microscript.c */
extern void ms_app_init(void);
/* Native B-System Document Editor */
extern WND* open_t_editor_window(void);

/* ── Launcher: キャビネット ─────────────────────────────────── */
WND* open_chokanji_cabinet_window(void) {
    cab_app_init();
    return NULL;
}

/* ── Launcher: 文書編集 (Delegates to native B-System T-Editor) ── */
WND* open_chokanji_doc_window(void) {
    return open_t_editor_window();
}

/* ── Launcher: マイクロスクリプト ──────────────────────────── */
WND* open_chokanji_microscript_window(void) {
    ms_app_init();
    return NULL;
}

/* ── Launcher: 時計 ─────────────────────────────────────────── */
WND* open_chokanji_clock_window(void) {
    clk_app_init();
    return NULL;
}
