/*
 * B-System (BTRON 3.20) Clarity DTP Engine – Native TAD Export Module (src/apps/clarity_export.c)
 * Standard TRON Application Databus (TAD) stream persistence with Real/Virtual Body integration.
 * Segments: TS_INFO (0xE0), TS_DFUSEN (0xE7), TS_TEXT (0xE1), TS_VOBJ (0xE6), TS_IMAGE (0xE5).
 */

#include "clarity_doc.h"
#include <btron/tad.h>
#include <btron/vobj.h>
#include <btron/error.h>
#include <btron/file.h>
#include <btron/fs/vol_api.h>
#include <btron/fs/fs_internal.h>
#include <sys/stat.h>

#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
#include <stdio.h>
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
static inline size_t strlen(const char *s) { size_t n = 0; while (s && s[n]) n++; return n; }
#endif

/* ── Little-Endian 16-bit and 32-bit helpers ────────────────────────── */
static inline void put_u16(UB *buf, UH v) {
    buf[0] = (UB)(v & 0xFF);
    buf[1] = (UB)((v >> 8) & 0xFF);
}

static inline UH get_u16(const UB *buf) {
    return (UH)(buf[0] | ((UH)buf[1] << 8));
}

static inline void put_u32(UB *buf, UW v) {
    buf[0] = (UB)(v & 0xFF);
    buf[1] = (UB)((v >> 8) & 0xFF);
    buf[2] = (UB)((v >> 16) & 0xFF);
    buf[3] = (UB)((v >> 24) & 0xFF);
}

static inline UW get_u32(const UB *buf) {
    return (UW)(buf[0] | ((UW)buf[1] << 8) | ((UW)buf[2] << 16) | ((UW)buf[3] << 24));
}

/* ── TAD Segment Serialization ──────────────────────────────────────── */

UB *clarity_export_serialize(const ClarityDoc *doc, UW *out_len)
{
    if (!doc || !out_len) return NULL;

    /* Estimate maximum buffer capacity required */
    UW total_est = 64; /* TS_INFO header */
    for (int i = 0; i < doc->frame_count; i++) {
        const ClarityFrame *f = &doc->frames[i];
        total_est += 64; /* TS_DFUSEN */
        total_est += 16 + (UW)f->text_len * 2; /* TS_TEXT */
        total_est += (UW)f->vobj_count * 128;   /* TS_VOBJ segments */
        if (f->bitmap && f->bmp_w > 0 && f->bmp_h > 0) {
            total_est += 16 + (UW)f->bmp_w * (UW)f->bmp_h * 4; /* TS_IMAGE */
        }
    }

    UB *buf = (UB *)malloc(total_est);
    if (!buf) return NULL;

    UW pos = 0;

    /* 1. TS_INFO (0xE0) — Document & Page Metadata */
    buf[pos++] = 0xFF;
    buf[pos++] = TS_INFO;
    put_u16(&buf[pos], 12); pos += 2; /* len = 12 */

    buf[pos++] = (UB)doc->fmt;
    put_u16(&buf[pos], (UH)doc->page_w_mm);   pos += 2;
    put_u16(&buf[pos], (UH)doc->page_h_mm);   pos += 2;
    buf[pos++] = (UB)doc->page_count;
    put_u16(&buf[pos], (UH)doc->zoom_pct);    pos += 2;
    buf[pos++] = (UB)doc->frame_count;
    buf[pos++] = 0; /* reserved */
    buf[pos++] = 0;
    buf[pos++] = 0;

    /* 2. Frames Serialization */
    for (int i = 0; i < doc->frame_count; i++) {
        const ClarityFrame *f = &doc->frames[i];

        /* TS_DFUSEN (0xE7) — Designated Fusen with Clarity Signature 'C''L' */
        buf[pos++] = 0xFF;
        buf[pos++] = TS_DFUSEN;
        UH dfusen_len = 19; /* Exact 19 bytes payload */
        put_u16(&buf[pos], dfusen_len); pos += 2;

        buf[pos++] = 'C';
        buf[pos++] = 'L';
        buf[pos++] = f->id;
        buf[pos++] = (UB)f->type;
        put_u16(&buf[pos], (UH)f->bounds.left);   pos += 2;
        put_u16(&buf[pos], (UH)f->bounds.top);    pos += 2;
        put_u16(&buf[pos], (UH)f->bounds.right);  pos += 2;
        put_u16(&buf[pos], (UH)f->bounds.bottom); pos += 2;
        buf[pos++] = (UB)f->flow;
        put_u32(&buf[pos], (UW)f->robj_id);       pos += 4;
        put_u16(&buf[pos], (UH)f->vobj_count);    pos += 2;

        /* If Text Frame: write TS_TEXT and TS_VOBJ segments */
        if (f->type == FRAME_TEXT) {
            /* TS_TEXT segment */
            buf[pos++] = 0xFF;
            buf[pos++] = TS_TEXT;
            UH text_bytes = (UH)(f->text_len * 2);
            put_u16(&buf[pos], text_bytes); pos += 2;

            for (UW j = 0; j < f->text_len; j++) {
                put_u16(&buf[pos], f->text[j]);
                pos += 2;
            }

            /* Emit TS_VOBJ segments for each Virtual Body */
            for (int vi = 0; vi < f->vobj_count; vi++) {
                const ClarityVObjLink *vl = &f->vobjs[vi];
                buf[pos++] = 0xFF;
                buf[pos++] = TS_VOBJ;

                UB l_len = (UB)strlen(vl->label);
                UB p_len = (UB)strlen(vl->path);
                UH vobj_seg_len = 4 + 1 + 1 + l_len + 1 + p_len;
                put_u16(&buf[pos], vobj_seg_len); pos += 2;

                put_u32(&buf[pos], (UW)vl->target_robj); pos += 4;
                buf[pos++] = (UB)vl->type;
                buf[pos++] = l_len;
                memcpy(&buf[pos], vl->label, l_len); pos += l_len;
                buf[pos++] = p_len;
                memcpy(&buf[pos], vl->path, p_len);  pos += p_len;
            }
        }
        /* If Image Frame: write TS_IMAGE */
        else if (f->type == FRAME_IMAGE) {
            buf[pos++] = 0xFF;
            buf[pos++] = TS_IMAGE;
            UB path_len = (UB)strlen(f->img_path);
            UW img_data_sz = (f->bitmap && f->bmp_w > 0 && f->bmp_h > 0) ? ((UW)f->bmp_w * (UW)f->bmp_h * 4) : 0;
            UH img_hdr_sz = 2 + 2 + 1 + 1 + path_len;
            put_u16(&buf[pos], (UH)(img_hdr_sz + ((img_data_sz < 60000) ? (UH)img_data_sz : 0))); pos += 2;

            put_u16(&buf[pos], (UH)f->bmp_w); pos += 2;
            put_u16(&buf[pos], (UH)f->bmp_h); pos += 2;
            buf[pos++] = 4; /* 4 bytes per pixel: RGBA */
            buf[pos++] = path_len;
            if (path_len > 0) {
                memcpy(&buf[pos], f->img_path, path_len);
                pos += path_len;
            }

            if (f->bitmap && img_data_sz > 0 && img_data_sz < 60000) {
                memcpy(&buf[pos], f->bitmap, img_data_sz);
                pos += img_data_sz;
            }
        }
        else if (f->type == FRAME_TAD) {
            buf[pos++] = 0xFF;
            buf[pos++] = TS_VOBJ;
            UB p_len = (UB)strlen(f->tad_path);
            UB t_len = (UB)strlen(f->tad_title);
            UH seg_len = 4 + 1 + 1 + t_len + 1 + p_len;
            put_u16(&buf[pos], seg_len); pos += 2;

            put_u32(&buf[pos], (UW)f->robj_id); pos += 4;
            buf[pos++] = (UB)VOBJ_TYPE_TEXT;
            buf[pos++] = t_len;
            memcpy(&buf[pos], f->tad_title, t_len); pos += t_len;
            buf[pos++] = p_len;
            memcpy(&buf[pos], f->tad_path, p_len);   pos += p_len;
        }
    }

    *out_len = pos;
    return buf;
}

/* ── TAD Segment Deserialization ────────────────────────────────────── */

ER clarity_export_deserialize(ClarityDoc *doc, const UB *buf, UW len)
{
    if (!doc || !buf || len < 8) return E_PAR;

    /* Support legacy CLRD format fallback */
    if (buf[0] == 'C' && buf[1] == 'L' && buf[2] == 'R' && buf[3] == 'D') {
        doc->fmt         = (ClarityPageFmt)buf[5];
        int frame_count  = (int)buf[6];
        if (frame_count > CLARITY_MAX_FRAMES) frame_count = CLARITY_MAX_FRAMES;
        doc->frame_count = frame_count;
        UW pos = 7;
        for (int i = 0; i < frame_count && pos + 13 <= len; i++) {
            ClarityFrame *f = &doc->frames[i];
            if (f->bitmap) { free(f->bitmap); f->bitmap = NULL; }
            f->id              = buf[pos++];
            f->type            = (ClarityFrameType)buf[pos++];
            f->bounds.left     = (H)get_u16(&buf[pos]); pos += 2;
            f->bounds.top      = (H)get_u16(&buf[pos]); pos += 2;
            f->bounds.right    = (H)get_u16(&buf[pos]); pos += 2;
            f->bounds.bottom   = (H)get_u16(&buf[pos]); pos += 2;
            f->flow            = (ClarityFlow)buf[pos++];
            f->text_len        = get_u16(&buf[pos]);      pos += 2;
            if (f->text_len > CLARITY_TEXT_BUF) f->text_len = CLARITY_TEXT_BUF;
            for (UW j = 0; j < f->text_len && pos + 2 <= len; j++) {
                f->text[j] = get_u16(&buf[pos]); pos += 2;
            }
            f->bmp_w = (H)get_u16(&buf[pos]); pos += 2;
            f->bmp_h = (H)get_u16(&buf[pos]); pos += 2;
        }
        return E_OK;
    }

    /* Standard TAD Parser */
    UW pos = 0;
    ClarityFrame *cur_frame = NULL;
    doc->frame_count = 0;

    while (pos + 4 <= len) {
        if (buf[pos] != 0xFF) {
            pos++;
            continue;
        }

        UB seg_id = buf[pos + 1];
        UH seg_len = get_u16(&buf[pos + 2]);
        pos += 4;

        if (pos + seg_len > len && seg_len < 0xFF00) {
            break;
        }

        switch (seg_id) {
            case TS_INFO:
                if (seg_len >= 8) {
                    doc->fmt = (ClarityPageFmt)buf[pos];
                    doc->page_w_mm = (int)get_u16(&buf[pos + 1]);
                    doc->page_h_mm = (int)get_u16(&buf[pos + 3]);
                    doc->page_count = (int)buf[pos + 5];
                    doc->zoom_pct = (int)get_u16(&buf[pos + 6]);
                }
                pos += seg_len;
                break;

            case TS_DFUSEN:
                if (seg_len >= 19 && buf[pos] == 'C' && buf[pos + 1] == 'L') {
                    if (doc->frame_count < CLARITY_MAX_FRAMES) {
                        cur_frame = &doc->frames[doc->frame_count++];
                        memset(cur_frame, 0, sizeof(ClarityFrame));
                        cur_frame->id           = buf[pos + 2];
                        cur_frame->type         = (ClarityFrameType)buf[pos + 3];
                        cur_frame->bounds.left  = (H)get_u16(&buf[pos + 4]);
                        cur_frame->bounds.top   = (H)get_u16(&buf[pos + 6]);
                        cur_frame->bounds.right = (H)get_u16(&buf[pos + 8]);
                        cur_frame->bounds.bottom = (H)get_u16(&buf[pos + 10]);
                        cur_frame->flow         = (ClarityFlow)buf[pos + 12];
                        cur_frame->robj_id      = (ID)get_u32(&buf[pos + 13]);
                        cur_frame->vobj_count   = 0;
                    }
                }
                pos += seg_len;
                break;

            case TS_TEXT:
                if (cur_frame) {
                    UH n_chars = seg_len / 2;
                    if (n_chars > CLARITY_TEXT_BUF - 1) n_chars = CLARITY_TEXT_BUF - 1;
                    cur_frame->text_len = n_chars;
                    for (UH j = 0; j < n_chars; j++) {
                        cur_frame->text[j] = get_u16(&buf[pos + j * 2]);
                    }
                    cur_frame->text[cur_frame->text_len] = 0;
                }
                pos += seg_len;
                break;

            case TS_VOBJ:
                if (cur_frame && cur_frame->type == FRAME_TAD && seg_len >= 6) {
                    cur_frame->robj_id = (ID)get_u32(&buf[pos]);
                    UB t_len = buf[pos + 5];
                    UW off = pos + 6;
                    if (t_len > 60) t_len = 60;
                    memcpy(cur_frame->tad_title, &buf[off], t_len);
                    cur_frame->tad_title[t_len] = '\0';
                    off += buf[pos + 5];
                    if (off < pos + seg_len) {
                        UB p_len = buf[off++];
                        if (p_len > 250) p_len = 250;
                        memcpy(cur_frame->tad_path, &buf[off], p_len);
                        cur_frame->tad_path[p_len] = '\0';
                    }
                } else if (cur_frame && cur_frame->vobj_count < CLARITY_MAX_VOBJS && seg_len >= 6) {
                    ClarityVObjLink *vl = &cur_frame->vobjs[cur_frame->vobj_count++];
                    vl->target_robj = (ID)get_u32(&buf[pos]);
                    vl->type = (VOBJ_TYPE)buf[pos + 4];
                    UB l_len = buf[pos + 5];
                    UW off = pos + 6;
                    if (l_len > 60) l_len = 60;
                    memcpy(vl->label, &buf[off], l_len);
                    vl->label[l_len] = '\0';
                    off += buf[pos + 5];
                    if (off < pos + seg_len) {
                        UB p_len = buf[off];
                        off++;
                        if (p_len > 120) p_len = 120;
                        memcpy(vl->path, &buf[off], p_len);
                        vl->path[p_len] = '\0';
                    }
                }
                pos += seg_len;
                break;

            case TS_IMAGE:
                if (cur_frame && seg_len >= 5) {
                    cur_frame->bmp_w = (H)get_u16(&buf[pos]);
                    cur_frame->bmp_h = (H)get_u16(&buf[pos + 2]);
                    UB bpp = buf[pos + 4];
                    UW off = pos + 5;
                    if (seg_len >= 6 && off < pos + seg_len) {
                        UB p_len = buf[off++];
                        if (p_len > 0 && p_len < sizeof(cur_frame->img_path) && off + p_len <= pos + seg_len) {
                            memcpy(cur_frame->img_path, &buf[off], p_len);
                            cur_frame->img_path[p_len] = '\0';
                            off += p_len;
                        }
                    }
                    UW img_sz = (UW)cur_frame->bmp_w * (UW)cur_frame->bmp_h * bpp;
                    if (off + img_sz <= pos + seg_len && img_sz > 0) {
                        cur_frame->bitmap = (UB *)malloc(img_sz);
                        if (cur_frame->bitmap) {
                            memcpy(cur_frame->bitmap, &buf[off], img_sz);
                        }
                    }
                }
                pos += seg_len;
                break;

            default:
                pos += seg_len;
                break;
        }
    }

    return E_OK;
}

/* ── Save / Load Real Bodies via VOBJ ────────────────────────────────── */

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

ER clarity_export_load(ClarityDoc *doc, ID robj_id)
{
    if (!doc) return E_PAR;

    ROBJ *robj = opn_robj(robj_id);
    if (!robj) return E_NOEXS;

    UB *buf = (UB *)malloc(65536);
    if (!buf) {
        cls_robj(robj);
        return E_NOMEM;
    }

    UW read_bytes = 0;
    ER err = rd_vobj_data(robj, buf, 65536, &read_bytes);
    cls_robj(robj);
    if (err != E_OK) {
        free(buf);
        return err;
    }

    err = clarity_export_deserialize(doc, buf, read_bytes);
    free(buf);
    return err;
}

/* ── File-Direct Persistence ────────────────────────────────────────── */

ER clarity_export_save_file(const ClarityDoc *doc, const char *filepath)
{
    if (!doc || !filepath) return E_PAR;
    UW len = 0;
    UB *buf = clarity_export_serialize(doc, &len);
    if (!buf) return E_NOMEM;

    const char *base = strrchr(filepath, '/');
    base = base ? base + 1 : filepath;

    /* 1. If targeting /SYS, save Real Body record on g_sys_vol if mounted */
    if (strncmp(filepath, "/SYS/", 5) == 0 || strncmp(filepath, "SYS/", 4) == 0) {
        if (g_sys_vol) {
            ID fd = opn_fil(base, 0x0002 /* F_WRITE */);
            if (fd < 0) {
                fd = cre_fil(base, 0x0002 /* F_WRITE */);
            }
            if (fd >= 0) {
                del_rec(fd, 0);
                ins_rec(fd, 0, (const char*)buf, (W)len);
                fil_set_rec_type(fd, 0, (unsigned short)RT_TADDATA);
                cls_fil(fd);
                vol_sync(g_sys_vol);
            }
        }
    }

#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
    /* 2. Save to host files */
    mkdir("SYS", 0755);
    mkdir("btron_store", 0755);

    FILE *fp = fopen(filepath, "wb");
    if (!fp && strncmp(filepath, "/SYS/", 5) == 0) {
        char rel_sys[256];
        snprintf(rel_sys, sizeof(rel_sys), "SYS/%s", filepath + 5);
        fp = fopen(rel_sys, "wb");
    }
    if (fp) {
        fwrite(buf, 1, len, fp);
        fclose(fp);
    }

    /* Also mirror to SYS/ and btron_store/ for reliable persistence */
    char sys_alt[256];
    snprintf(sys_alt, sizeof(sys_alt), "SYS/%s", base);
    FILE *fp_sys = fopen(sys_alt, "wb");
    if (fp_sys) {
        fwrite(buf, 1, len, fp_sys);
        fclose(fp_sys);
    }

    char store_alt[256];
    snprintf(store_alt, sizeof(store_alt), "btron_store/%s", base);
    FILE *fp_store = fopen(store_alt, "wb");
    if (fp_store) {
        fwrite(buf, 1, len, fp_store);
        fclose(fp_store);
    }

    free(buf);
    return E_OK;
#else
    free(buf);
    return E_OK;
#endif
}

ER clarity_export_load_file(ClarityDoc *doc, const char *filepath)
{
    if (!doc || !filepath) return E_PAR;

    const char *base = strrchr(filepath, '/');
    base = base ? base + 1 : filepath;

    /* 1. If targeting /SYS and volume is mounted, check g_sys_vol first */
    if (strncmp(filepath, "/SYS/", 5) == 0 || strncmp(filepath, "SYS/", 4) == 0) {
        if (g_sys_vol) {
            ID fd = opn_fil(base, 0x0001 /* F_READ */);
            if (fd >= 0) {
                ID rec = opn_rec(fd, 0, 0x0001);
                if (rec >= 0) {
                    OpenFile *of = &g_open_files[(int)fd];
                    UW rsize = (of->nrec > 0) ? of->ridx[0].size : 0;
                    if (rsize > 0 && rsize <= 16 * 1024 * 1024) {
                        UB *vbuf = (UB *)malloc((size_t)rsize);
                        if (vbuf) {
                            W actual = 0;
                            rd_rec(rec, (char*)vbuf, (W)rsize, &actual);
                            cls_rec(rec);
                            cls_fil(fd);
                            if (actual > 0) {
                                ER err = clarity_export_deserialize(doc, vbuf, (UW)actual);
                                free(vbuf);
                                if (err == E_OK && doc->frame_count > 0) return E_OK;
                            } else {
                                free(vbuf);
                            }
                        } else {
                            cls_rec(rec);
                            cls_fil(fd);
                        }
                    } else {
                        cls_rec(rec);
                        cls_fil(fd);
                    }
                } else {
                    cls_fil(fd);
                }
            }
        }
    }

#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
    const char *try_paths[6];
    int n_paths = 0;
    try_paths[n_paths++] = filepath;

    char rel_sys[256];
    if (strncmp(filepath, "/SYS/", 5) == 0) {
        snprintf(rel_sys, sizeof(rel_sys), "SYS/%s", filepath + 5);
        try_paths[n_paths++] = rel_sys;
    } else if (strncmp(filepath, "SYS/", 4) == 0) {
        snprintf(rel_sys, sizeof(rel_sys), "/SYS/%s", filepath + 4);
        try_paths[n_paths++] = rel_sys;
    }

    char cur_sys[256];
    snprintf(cur_sys, sizeof(cur_sys), "./SYS/%s", base);
    try_paths[n_paths++] = cur_sys;

    char store_path[256];
    snprintf(store_path, sizeof(store_path), "btron_store/%s", base);
    try_paths[n_paths++] = store_path;

    char ceremony_path[256];
    snprintf(ceremony_path, sizeof(ceremony_path), "btron_store/Ceremony_Demo.tad");
    try_paths[n_paths++] = ceremony_path;

    for (int p = 0; p < n_paths; p++) {
        FILE *fp = fopen(try_paths[p], "rb");
        if (!fp) continue;

        fseek(fp, 0, SEEK_END);
        long sz = ftell(fp);
        fseek(fp, 0, SEEK_SET);

        if (sz > 0 && sz <= 16 * 1024 * 1024) {
            UB *buf = (UB *)malloc((size_t)sz);
            if (buf) {
                size_t nr = fread(buf, 1, (size_t)sz, fp);
                fclose(fp);
                ER err = clarity_export_deserialize(doc, buf, (UW)nr);
                free(buf);
                if (err == E_OK && doc->frame_count > 0) {
                    return E_OK;
                }
            } else {
                fclose(fp);
            }
        } else {
            fclose(fp);
        }
    }
    return E_NOEXS;
#else
    return E_SYS;
#endif
}
