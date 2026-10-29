/*
 * B-System BTRON3 — sps.h
 * Secure PostScript (sps) Compiler & PDF Transpiler
 * Pure C99, adheres to NASA/JPL Power of Ten rules.
 */

#ifndef _BTRON_SPS_H_
#define _BTRON_SPS_H_

#include <btron/types.h>
#include <btron/core.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SPS_MAX_PAGES     32
#define SPS_PAGE_STREAM_MAX (32 * 1024)

typedef struct {
    int  page_num;
    UW   stream_len;
    char stream_buf[SPS_PAGE_STREAM_MAX];
} SPSPage;

typedef struct {
    int     page_count;
    SPSPage pages[SPS_MAX_PAGES];
    int     cur_page_idx;

    /* Current Font Tracker */
    char    active_font[64];
    float   active_font_size;
} SPSDocument;

/* Transpiles a PostScript file into conformant PDF 1.4 binary */
BOOL sps_transpile_to_pdf(const char *ps_path, const char *pdf_path, char *err_buf, size_t err_len);

/* CLU command entry point for gterm shell */
void clu_sps(const char *args, ShellOutputFn out, void *ud);

#ifdef __cplusplus
}
#endif

#endif /* _BTRON_SPS_H_ */
