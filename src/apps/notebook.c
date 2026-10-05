/*
 * B-System (BTRON 3.20) Scientific Notebook Application (src/apps/notebook.c)
 * Interactive Mathematica & Jupyter-style Notebook for Scientists (TAD + Language VMs)
 */

#include <btron/wnd.h>
#include <btron/dp.h>
#include <btron/tad_browser.h>
#include <btron/event.h>
#include <btron/libc_shim.h>

typedef enum {
    CELL_LANG_APL = 0,
    CELL_LANG_SCHEME,
    CELL_LANG_PYTHON_WASM,
    CELL_LANG_C_MICROVM,
    CELL_LANG_MARKDOWN_TAD
} NotebookCellLang;

typedef struct {
    int cell_id;
    NotebookCellLang lang;
    char code[2048];
    char output[2048];
    UW matrix_source_robj; /* Live link to Matrix TAD data */
    int exec_count;
} NotebookCell;

typedef struct {
    WND *wnd;
    NotebookCell cells[64];
    int cell_count;
    int active_cell;
} NotebookApp;

extern int notebook_vm_execute_cell(NotebookCell *cell);

void notebook_init(NotebookApp *app) {
    if (!app) return;
    memset(app, 0, sizeof(NotebookApp));
}

int notebook_add_cell(NotebookApp *app, NotebookCellLang lang, const char *initial_code) {
    if (!app || app->cell_count >= 64) return -1;
    NotebookCell *c = &app->cells[app->cell_count++];
    c->cell_id = app->cell_count;
    c->lang = lang;
    if (initial_code) strncpy(c->code, initial_code, sizeof(c->code) - 1);
    return app->cell_count - 1;
}

int notebook_run_active(NotebookApp *app) {
    if (!app || app->active_cell < 0 || app->active_cell >= app->cell_count) return -1;
    return notebook_vm_execute_cell(&app->cells[app->active_cell]);
}
