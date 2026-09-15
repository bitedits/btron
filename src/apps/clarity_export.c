/*
 * B-System (BTRON 3.20) Clarity DTP Engine – Export Module (src/apps/clarity_export.c)
 * Save / load ClarityDoc via VOBJ (wr_vobj_data / rd_vobj_data).
 * Binary format: "CLRD" magic + version + ClaritySerialFrame records.
 * All integers little-endian.
 */

#include "clarity_doc.h"
#include <btron/vobj.h>
#include <btron/error.h>

#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
#include <stdlib.h>
#include <string.h>
#else
#include <stddef.h>
#include <stdint.h>
extern void *Imalloc(size_t sz);
extern void  Ifree(void *ptr);
extern void *tkl_memset(void *s, int c, size_t n);
extern void *tkl_memcpy(void *dst, const void *src, size_t n);
#define malloc  Imalloc
#define free    Ifree
#define memset  tkl_memset
#define memcpy  tkl_memcpy
static inline size_t strlen(const char *s) {
    size_t n = 0; while (s && s[n]) n++; return n;
}
static inline int strncmp(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i]) return (unsigned char)a[i] - (unsigned char)b[i];
        if (!a[i]) return 0;
    }
    return 0;
}
#endif

/* ------------------------------------------------------------------ */
/* Helper: little-endian put/get for 16-bit and 32-bit                  */
/* ------------------------------------------------------------------ */

static void put_u16(UB *buf, UH v)
{
    buf[0] = (UB)(v & 0xFF);
    buf[1] = (UB)((v >> 8) & 0xFF);
}

static UH get_u16(const UB *buf)
{
    return (UH)(buf[0] | ((UH)buf[1] << 8));
}

/* ------------------------------------------------------------------ */
/* Serialise                                                            */
/* ------------------------------------------------------------------ */

/*
 * Serialise the entire document into a newly malloc'd buffer.
 * Caller must free() the returned pointer.
 * Returns NULL on allocation failure.
 *
 * Stream layout:
 *   [4B magic] [1B ver] [1B fmt] [1B frame_count]
 *   per frame:
 *     [1B id] [1B type] [2B left] [2B top] [2B right] [2B bottom]
 *     [1B flow] [2B text_len]
 *     [text_len * 2 B  text UH units]
 *     [2B bmp_w] [2B bmp_h]
 *     [bmp_w * bmp_h * 4 B RGBA]
 */
UB *clarity_export_serialize(const ClarityDoc *doc, UW *out_len)
{
    if (!doc || !out_len) return NULL;

    /* Calculate total size first */
    UW total = 7; /* magic(4) + ver(1) + fmt(1) + count(1) */
    for (int i = 0; i < doc->frame_count; i++) {
        const ClarityFrame *f = &doc->frames[i];
        /* fixed per-frame header */
        total += 1 + 1 + 2 + 2 + 2 + 2 + 1 + 2;  /* 13 bytes */
        total += (UW)f->text_len * 2;
        total += 2 + 2; /* bmp_w, bmp_h */
        if (f->bitmap && f->bmp_w > 0 && f->bmp_h > 0)
            total += (UW)f->bmp_w * (UW)f->bmp_h * 4;
    }

    UB *buf = (UB *)malloc(total);
    if (!buf) return NULL;

    UW pos = 0;

    /* Header */
    buf[pos++] = (UB)'C';
    buf[pos++] = (UB)'L';
    buf[pos++] = (UB)'R';
    buf[pos++] = (UB)'D';
    buf[pos++] = (UB)CLARITY_SERIAL_VER;
    buf[pos++] = (UB)doc->fmt;
    buf[pos++] = (UB)doc->frame_count;

    /* Frames */
    for (int i = 0; i < doc->frame_count; i++) {
        const ClarityFrame *f = &doc->frames[i];

        buf[pos++] = f->id;
        buf[pos++] = (UB)f->type;
        put_u16(&buf[pos], (UH)f->bounds.left);   pos += 2;
        put_u16(&buf[pos], (UH)f->bounds.top);    pos += 2;
        put_u16(&buf[pos], (UH)f->bounds.right);  pos += 2;
        put_u16(&buf[pos], (UH)f->bounds.bottom); pos += 2;
        buf[pos++] = (UB)f->flow;
        put_u16(&buf[pos], (UH)f->text_len);      pos += 2;

        /* Text units */
        for (UW j = 0; j < f->text_len; j++) {
            put_u16(&buf[pos], f->text[j]);
            pos += 2;
        }

        /* Bitmap */
        put_u16(&buf[pos], (UH)f->bmp_w); pos += 2;
        put_u16(&buf[pos], (UH)f->bmp_h); pos += 2;
        if (f->bitmap && f->bmp_w > 0 && f->bmp_h > 0) {
            UW bsz = (UW)f->bmp_w * (UW)f->bmp_h * 4;
            memcpy(&buf[pos], f->bitmap, bsz);
            pos += bsz;
        }
    }

    *out_len = pos;
    return buf;
}

/* ------------------------------------------------------------------ */
/* Save                                                                 */
/* ------------------------------------------------------------------ */

ER clarity_export_save(const ClarityDoc *doc, const char *name)
{
    if (!doc || !name) return E_PAR;

    UW len = 0;
    UB *buf = clarity_export_serialize(doc, &len);
    if (!buf) return E_NOMEM;

    ROBJ *robj = cre_robj(name, VOBJ_TYPE_TEXT);
    ER err = E_OK;
    if (!robj) {
        err = E_NOMEM;
    } else {
        err = wr_vobj_data(robj, buf, len);
        cls_robj(robj);
    }

    free(buf);
    return err;
}

/* ------------------------------------------------------------------ */
/* Load                                                                 */
/* ------------------------------------------------------------------ */

ER clarity_export_load(ClarityDoc *doc, ID robj_id)
{
    if (!doc) return E_PAR;

    ROBJ *robj = opn_robj(robj_id);
    if (!robj) return E_NOEXS;

    /* Read into a temporary buffer.
     * vobj.c's rd_vobj_data returns a static demo string in the prototype;
     * in production it would return the saved blob. */
    UB buf[16384];
    UW read_bytes = 0;
    ER err = rd_vobj_data(robj, buf, sizeof(buf), &read_bytes);
    cls_robj(robj);
    if (err != E_OK) return err;
    if (read_bytes < 7) return E_SYS;

    /* Validate magic */
    if (buf[0] != 'C' || buf[1] != 'L' || buf[2] != 'R' || buf[3] != 'D')
        return E_SYS;

    /* UB ver = buf[4]; */  /* reserved for future use */
    doc->fmt         = (ClarityPageFmt)buf[5];
    int frame_count  = (int)buf[6];
    if (frame_count > CLARITY_MAX_FRAMES) frame_count = CLARITY_MAX_FRAMES;
    doc->frame_count = frame_count;

    UW pos = 7;
    for (int i = 0; i < frame_count && pos < read_bytes; i++) {
        ClarityFrame *f = &doc->frames[i];

        /* Free any pre-existing bitmap */
        if (f->bitmap) { free(f->bitmap); f->bitmap = NULL; }

        if (pos + 13 > read_bytes) break;

        f->id              = buf[pos++];
        f->type            = (ClarityFrameType)buf[pos++];
        f->bounds.left     = (H)get_u16(&buf[pos]); pos += 2;
        f->bounds.top      = (H)get_u16(&buf[pos]); pos += 2;
        f->bounds.right    = (H)get_u16(&buf[pos]); pos += 2;
        f->bounds.bottom   = (H)get_u16(&buf[pos]); pos += 2;
        f->flow            = (ClarityFlow)buf[pos++];
        f->text_len        = get_u16(&buf[pos]);      pos += 2;

        if (f->text_len > CLARITY_TEXT_BUF) f->text_len = CLARITY_TEXT_BUF;
        for (UW j = 0; j < f->text_len && pos + 2 <= read_bytes; j++) {
            f->text[j] = get_u16(&buf[pos]); pos += 2;
        }

        if (pos + 4 > read_bytes) break;
        f->bmp_w = (H)get_u16(&buf[pos]); pos += 2;
        f->bmp_h = (H)get_u16(&buf[pos]); pos += 2;

        if (f->bmp_w > 0 && f->bmp_h > 0) {
            UW bsz = (UW)f->bmp_w * (UW)f->bmp_h * 4;
            if (pos + bsz <= read_bytes) {
                f->bitmap = (UB *)malloc(bsz);
                if (f->bitmap) {
                    memcpy(f->bitmap, &buf[pos], bsz);
                }
                pos += bsz;
            }
        }
    }

    doc->selected_frame = -1;
    doc->dirty          = FALSE;
    return E_OK;
}
