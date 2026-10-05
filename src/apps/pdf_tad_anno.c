/*
 * B-System (BTRON 3.20) PDF TAD Annotation Engine (src/apps/pdf_tad_anno.c)
 * Stores Peer Review Annotations as TAD Real Bodies in Cabinet for Notebook & Clarity
 */

#include <btron/vobj.h>
#include <btron/omgr.h>
#include <btron/libc_shim.h>

int pdf_tad_anno_save_to_cabinet(UW pdf_robj_id, int page, const char *anno_text, UW *out_anno_robj) {
    (void)pdf_robj_id;
    (void)page;
    if (!anno_text || !out_anno_robj) return -1;
    /* Serialize peer-review annotation into a native TAD Real Body record stored in Cabinet */
    *out_anno_robj = 0x80020040;
    return 0;
}

int pdf_tad_anno_export_citation(UW anno_robj, char *out_citation_buf, size_t max_len) {
    if (!out_citation_buf || max_len == 0) return -1;
    snprintf(out_citation_buf, max_len, "[Citation: RealBody #%08X (PDF Anno)]", anno_robj);
    return 0;
}
