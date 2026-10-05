/*
 * B-System (BTRON 3.20) Matrix Spreadsheet & Calculation Engine (src/apps/matrix.c)
 * 2D Grid with Native TAD Cell Embedding & APL Vector Evaluation
 */

#include <btron/wnd.h>
#include <btron/dp.h>
#include <btron/tad_browser.h>
#include <btron/event.h>
#include <btron/libc_shim.h>

#define MATRIX_MAX_ROWS 1000
#define MATRIX_MAX_COLS 100

typedef enum {
    CELL_TYPE_EMPTY = 0,
    CELL_TYPE_NUMBER,
    CELL_TYPE_TEXT,
    CELL_TYPE_FORMULA,
    CELL_TYPE_APL_EXPR,
    CELL_TYPE_TAD_ROBJ
} MatrixCellType;

typedef struct {
    MatrixCellType type;
    double num_val;
    char text[64];
    UW robj_id;
} MatrixCell;

typedef struct {
    WND *wnd;
    int rows;
    int cols;
    int cursor_row;
    int cursor_col;
    MatrixCell *cells;
} MatrixSheet;

extern int matrix_apl_eval(const char *expr, double *out_res);

int matrix_init(MatrixSheet *sheet, int r, int c) {
    if (!sheet) return -1;
    sheet->rows = (r > 0 && r <= MATRIX_MAX_ROWS) ? r : 100;
    sheet->cols = (c > 0 && c <= MATRIX_MAX_COLS) ? c : 26;
    sheet->cursor_row = 0;
    sheet->cursor_col = 0;
    sheet->cells = (MatrixCell*)calloc(sheet->rows * sheet->cols, sizeof(MatrixCell));
    return sheet->cells ? 0 : -1;
}

void matrix_set_cell_num(MatrixSheet *sheet, int r, int c, double val) {
    if (!sheet || !sheet->cells || r < 0 || r >= sheet->rows || c < 0 || c >= sheet->cols) return;
    MatrixCell *cell = &sheet->cells[r * sheet->cols + c];
    cell->type = CELL_TYPE_NUMBER;
    cell->num_val = val;
}

void matrix_set_cell_apl(MatrixSheet *sheet, int r, int c, const char *apl_expr) {
    if (!sheet || !sheet->cells || r < 0 || r >= sheet->rows || c < 0 || c >= sheet->cols) return;
    MatrixCell *cell = &sheet->cells[r * sheet->cols + c];
    cell->type = CELL_TYPE_APL_EXPR;
    strncpy(cell->text, apl_expr ? apl_expr : "", sizeof(cell->text) - 1);
    matrix_apl_eval(apl_expr, &cell->num_val);
}
