/*
 * B-System (BTRON 3.20) Hypermedia & DND Automated Verification Harness
 * verify/tests/test_clarity_hypermedia.c
 * Validates Real/Virtual Body persistence, image decoding, TAD segment serialization,
 * and Cabinet -> Clarity direct-manipulation workflows.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <btron/types.h>
#include <btron/tad.h>
#include <btron/vobj.h>
#include <btron/dnd.h>
#include <btron/image_decode.h>
#include "../../src/apps/clarity_doc.h"

/* Helpers */
static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define TEST_ASSERT(cond, msg) do {     if (cond) {         printf("  [PASS] %s\n", msg);         g_tests_passed++;     } else {         printf("  [FAIL] %s (line %d)\n", msg, __LINE__);         g_tests_failed++;     } } while (0)

/* ── 1. Real Body Persistence Test ────────────────────────────────── */
static void test_vobj_persistence(void) {
    printf("==> Test 1: Real Body Storage & Persistence\n");
    ER err = init_vobj_sys("btron_store");
    TEST_ASSERT(err == E_OK, "init_vobj_sys initialized storage root");

    ROBJ *r = cre_robj("ceremony_test_robj.tad", VOBJ_TYPE_TEXT);
    TEST_ASSERT(r != NULL && r->robj_id >= 100, "cre_robj allocated new Real Body slot");

    const char *payload = "BTRON 3.20 Hypermedia Real Body Persistent Storage Payload";
    UW plen = (UW)strlen(payload);
    err = wr_vobj_data(r, payload, plen);
    TEST_ASSERT(err == E_OK, "wr_vobj_data successfully wrote to disk");

    ID saved_id = r->robj_id;
    cls_robj(r);

    ROBJ *r_read = opn_robj(saved_id);
    TEST_ASSERT(r_read != NULL, "opn_robj re-opened saved Real Body");

    char read_buf[128];
    memset(read_buf, 0, sizeof(read_buf));
    UW read_bytes = 0;
    err = rd_vobj_data(r_read, read_buf, sizeof(read_buf) - 1, &read_bytes);
    TEST_ASSERT(err == E_OK, "rd_vobj_data read payload from disk");
    TEST_ASSERT(read_bytes == plen, "rd_vobj_data read exact byte count");
    TEST_ASSERT(strcmp(read_buf, payload) == 0, "Payload content matched byte-for-byte");
    cls_robj(r_read);
}

/* ── 2. Unified Image Decoder Test ────────────────────────────────── */
static void test_image_decoder(void) {
    printf("==> Test 2: Unified Image Decoder (PNG & GIF)\n");
    UB *pixels = NULL;
    H w = 0, h = 0;

    int ret = decode_image_rgba("assets/icons/clarity.png", &pixels, &w, &h);
    TEST_ASSERT(ret == 0 && pixels != NULL, "decode_image_rgba decoded assets/icons/clarity.png");
    TEST_ASSERT(w > 0 && h > 0, "clarity.png has valid positive dimensions");
    if (pixels) {
        /* Check that pixel buffer contains non-zero data */
        BOOL has_data = FALSE;
        for (int i = 0; i < w * h * 4; i++) {
            if (pixels[i] != 0) { has_data = TRUE; break; }
        }
        TEST_ASSERT(has_data, "clarity.png RGBA pixel payload is populated");
        free(pixels);
    }
}

/* ── 3. TAD Format Serialization & Roundtrip ──────────────────────── */
extern UB *clarity_export_serialize(const ClarityDoc *doc, UW *out_len);
extern ER  clarity_export_deserialize(ClarityDoc *doc, const UB *buf, UW len);

static void test_tad_serialization(void) {
    printf("==> Test 3: TAD Segment Format Serialization & Deserialization\n");

    ClarityDoc doc;
    memset(&doc, 0, sizeof(ClarityDoc));
    doc.fmt = FMT_A4;
    doc.page_w_mm = 210;
    doc.page_h_mm = 297;
    doc.page_count = 2;
    doc.zoom_pct = 75;

    /* Frame 0: Image Frame */
    ClarityFrame *f0 = clarity_doc_add_frame(&doc, FRAME_IMAGE, 30, 30, 100, 100);
    TEST_ASSERT(f0 != NULL, "Created test Image Frame");
    f0->bmp_w = 16;
    f0->bmp_h = 16;
    f0->bitmap = (UB *)malloc(16 * 16 * 4);
    memset(f0->bitmap, 0x7F, 16 * 16 * 4);
    f0->robj_id = 105;

    /* Frame 1: Text Frame with Virtual Body Links */
    ClarityFrame *f1 = clarity_doc_add_frame(&doc, FRAME_TEXT, 150, 30, 300, 150);
    TEST_ASSERT(f1 != NULL, "Created test Text Frame");
    const char *txt = "Hypermedia TAD Segment Showcase: ";
    for (int i = 0; txt[i]; i++) f1->text[f1->text_len++] = (UH)(unsigned char)txt[i];
    f1->cursor_pos = (int)f1->text_len;

    clarity_frame_insert_vobj(f1, 201, VOBJ_TYPE_TEXT, "01_btron3_spec.tad", "tad_bin/01_btron3_spec.tad");
    clarity_frame_insert_vobj(f1, 202, VOBJ_TYPE_TEXT, "HYPERMEDIA.md", "doc/md/HYPERMEDIA.md");
    TEST_ASSERT(f1->vobj_count == 2, "Inserted 2 Virtual Body links into Text Frame");

    /* Serialize to TAD */
    UW tad_len = 0;
    UB *tad_buf = clarity_export_serialize(&doc, &tad_len);
    TEST_ASSERT(tad_buf != NULL && tad_len > 0, "clarity_export_serialize generated TAD stream");
    TEST_ASSERT(tad_buf[0] == 0xFF && tad_buf[1] == TS_INFO, "TAD stream begins with 0xFF TS_INFO segment");

    /* Deserialize back into fresh ClarityDoc */
    ClarityDoc restored;
    memset(&restored, 0, sizeof(ClarityDoc));
    ER err = clarity_export_deserialize(&restored, tad_buf, tad_len);
    TEST_ASSERT(err == E_OK, "clarity_export_deserialize parsed TAD stream");
    TEST_ASSERT(restored.fmt == FMT_A4, "Restored page format matches FMT_A4");
    TEST_ASSERT(restored.frame_count == 2, "Restored exactly 2 frames");

    /* Check restored Image Frame */
        ClarityFrame *rf0 = &restored.frames[0];
    TEST_ASSERT(rf0->type == FRAME_IMAGE, "Restored Frame 0 is FRAME_IMAGE");
    TEST_ASSERT(rf0->bmp_w == 16 && rf0->bmp_h == 16, "Restored Frame 0 bitmap dimensions match 16x16");
    TEST_ASSERT(rf0->robj_id == 105, "Restored Frame 0 Real Body ID matches 105");

    /* Check restored Text Frame & Virtual Bodies */
    ClarityFrame *rf1 = &restored.frames[1];
    TEST_ASSERT(rf1->type == FRAME_TEXT, "Restored Frame 1 is FRAME_TEXT");
    TEST_ASSERT(rf1->vobj_count == 2, "Restored Frame 1 has exactly 2 Virtual Body links");
    TEST_ASSERT(rf1->vobjs[0].target_robj == 201, "Restored VB 0 target_robj == 201");
    TEST_ASSERT(strcmp(rf1->vobjs[0].label, "01_btron3_spec.tad") == 0, "Restored VB 0 label matches");
    TEST_ASSERT(rf1->vobjs[1].target_robj == 202, "Restored VB 1 target_robj == 202");
    TEST_ASSERT(strcmp(rf1->vobjs[1].label, "HYPERMEDIA.md") == 0, "Restored VB 1 label matches");

    free(tad_buf);
    if (f0->bitmap) free(f0->bitmap);
    if (rf0->bitmap) free(rf0->bitmap);
}

/* ── 4. Direct Manipulation DND Ingestion Test ────────────────────── */
static void test_dnd_workflows(void) {
    printf("==> Test 4: Cabinet -> Clarity Direct Manipulation DND Workflows\n");

    btron_dnd_init();
    TEST_ASSERT(btron_dnd_is_active() == FALSE, "DND starts in Idle state");

    ClarityDoc doc;
    memset(&doc, 0, sizeof(ClarityDoc));
    doc.fmt = FMT_A4;
    doc.zoom_pct = 100;
    int ox = 24, oy = 24;

    /* Create target frames */
    ClarityFrame *img_f = clarity_doc_add_frame(&doc, FRAME_IMAGE, 40, 40, 160, 160);
    ClarityFrame *txt_f = clarity_doc_add_frame(&doc, FRAME_TEXT, 220, 40, 300, 160);
    (void)img_f;
    (void)txt_f;

    /* A. Drag Image from Cabinet to Clarity Image Frame */
    btron_dnd_begin(1, 301, VOBJ_TYPE_DRAW, "clarity.png", "assets/icons/clarity.png", 10, 10);
    TEST_ASSERT(btron_dnd_is_active() == TRUE, "DND is active after btron_dnd_begin");

    /* Drop at Image Frame coordinates: canvas (ox + 50, oy + 50) */
    H drop_x = (H)(ox + 60);
    H drop_y = (H)(oy + 60);
    clarity_handle_dnd_drop(&doc, btron_dnd_get(), drop_x, drop_y, ox, oy);
    btron_dnd_end();

    TEST_ASSERT(img_f->bitmap != NULL, "Dropping image loaded RGBA bitmap into Image Frame");
    TEST_ASSERT(img_f->robj_id == 301, "Image Frame registered Real Body ID 301");
    TEST_ASSERT(strcmp(img_f->img_path, "assets/icons/clarity.png") == 0, "Image Frame recorded source path");

    /* B. Drag Text Document from Cabinet to Clarity Text Frame */
    btron_dnd_begin(1, 302, VOBJ_TYPE_TEXT, "HYPERMEDIA.md", "doc/md/HYPERMEDIA.md", 10, 10);
    H txt_drop_x = (H)(ox + 250);
    H txt_drop_y = (H)(oy + 60);
    clarity_handle_dnd_drop(&doc, btron_dnd_get(), txt_drop_x, txt_drop_y, ox, oy);
    btron_dnd_end();

    TEST_ASSERT(txt_f->vobj_count == 1, "Dropping document embedded Virtual Body into Text Frame");
    TEST_ASSERT(txt_f->vobjs[0].target_robj == 302, "Embedded Virtual Body points to Real Body 302");
    TEST_ASSERT(strstr((char *)txt_f->vobjs[0].label, "HYPERMEDIA.md") != NULL, "Virtual Body moniker label matches");

    /* C. Hit-test Virtual Body in Text Frame */
    /* Set mock box for hit-testing */
    txt_f->vobjs[0].box = (RECT){ 220, 40, 320, 60 };
    int vhit = clarity_frame_find_vobj_at(txt_f, 250, 50);
    TEST_ASSERT(vhit == 0, "clarity_frame_find_vobj_at hit-tests Virtual Body moniker accurately");

    if (img_f->bitmap) free(img_f->bitmap);
}


static void test_tad_placeholder_and_sys_persistence(void) {
    printf("==> Test 5: TAD Placeholders & /SYS/Clarity-Sample.TAD Persistence\n");

    ClarityDoc doc;
    memset(&doc, 0, sizeof(doc));
    clarity_init_sample_page(&doc);

    TEST_ASSERT(doc.frame_count >= 3, "Sample page initialized with at least 3 frames");
    TEST_ASSERT(doc.frames[0].type == FRAME_IMAGE, "Frame 0 is FRAME_IMAGE");
    TEST_ASSERT(doc.frames[0].bitmap != NULL, "Frame 0 loaded image bitmap from /SYS/clarity.png");
    TEST_ASSERT(doc.frames[1].type == FRAME_TEXT, "Frame 1 is FRAME_TEXT");
    TEST_ASSERT(doc.frames[1].vobj_count >= 1, "Frame 1 has Virtual Body links");
    TEST_ASSERT(doc.frames[2].type == FRAME_TAD, "Frame 2 is FRAME_TAD placeholder");
    TEST_ASSERT(strstr(doc.frames[2].tad_path, "01_btron3_spec.tad") != NULL, "Frame 2 points to spec TAD");

    /* Test Saving to /SYS/Clarity-Sample.TAD */
    ER save_err = clarity_export_save_file(&doc, "/SYS/Clarity-Sample.TAD");
    TEST_ASSERT(save_err == E_OK, "clarity_export_save_file to /SYS/Clarity-Sample.TAD succeeded");

    /* Test Loading from /SYS/Clarity-Sample.TAD */
    ClarityDoc loaded;
    memset(&loaded, 0, sizeof(loaded));
    ER load_err = clarity_export_load_file(&loaded, "/SYS/Clarity-Sample.TAD");
    TEST_ASSERT(load_err == E_OK, "clarity_export_load_file from /SYS/Clarity-Sample.TAD succeeded");
    TEST_ASSERT(loaded.frame_count == doc.frame_count, "Restored frame count matches original");
    TEST_ASSERT(loaded.frames[0].type == FRAME_IMAGE, "Restored Frame 0 is FRAME_IMAGE");
    TEST_ASSERT(strstr(loaded.frames[0].img_path, "clarity.png") != NULL, "Restored Frame 0 preserved img_path");
    TEST_ASSERT(loaded.frames[1].type == FRAME_TEXT, "Restored Frame 1 is FRAME_TEXT");
    TEST_ASSERT(loaded.frames[1].vobj_count == doc.frames[1].vobj_count, "Restored Frame 1 Virtual Body count matches");
    TEST_ASSERT(loaded.frames[2].type == FRAME_TAD, "Restored Frame 2 is FRAME_TAD");
    TEST_ASSERT(strstr(loaded.frames[2].tad_path, "01_btron3_spec.tad") != NULL, "Restored Frame 2 preserved tad_path");

    /* Test Drag & Drop creating FRAME_TAD */
    btron_dnd_begin(1, 401, VOBJ_TYPE_TEXT, "02_tkernel_book.tad", "tad_bin/02_tkernel_book.tad", 50, 50);
    clarity_handle_dnd_drop(&loaded, btron_dnd_get(), 200, 550, 24, 24);
    btron_dnd_end();

    TEST_ASSERT(loaded.frame_count == doc.frame_count + 1, "Dropping TAD file created new frame");
    ClarityFrame *new_tad = &loaded.frames[loaded.frame_count - 1];
    TEST_ASSERT(new_tad->type == FRAME_TAD, "New frame is FRAME_TAD placeholder");
    TEST_ASSERT(strstr(new_tad->tad_path, "02_tkernel_book.tad") != NULL, "New FRAME_TAD recorded tad_path");

    /* Cleanup */
    for (int i = 0; i < doc.frame_count; i++) {
        if (doc.frames[i].bitmap) free(doc.frames[i].bitmap);
    }
    for (int i = 0; i < loaded.frame_count; i++) {
        if (loaded.frames[i].bitmap) free(loaded.frames[i].bitmap);
    }
}

/* ── 6. DND Relink & Multilingual Rerender Verification ────────────── */
static void test_dnd_relink_and_multilingual_rerender(void) {
    printf("==> Test 6: Direct Manipulation Relinking & Tibetan Heart Sutra Ingestion\n");

    ClarityDoc doc;
    memset(&doc, 0, sizeof(doc));
    clarity_init_sample_page(&doc);

    ClarityFrame *txt_f = &doc.frames[1];
    TEST_ASSERT(txt_f->type == FRAME_TEXT, "Frame 1 starts as Text Frame");
    TEST_ASSERT(txt_f->vobj_count == 1, "Frame 1 starts with HYPERMEDIA.md moniker");

    /* Drop Tibetan Heart Sutra onto Frame 1 to replace existing link */
    int ox = 24, oy = 24;
    int zoom = doc.zoom_pct ? doc.zoom_pct : 100;
    H drop_x = (H)(ox + (txt_f->bounds.left * zoom) / 100 + 40);
    H drop_y = (H)(oy + (txt_f->bounds.top  * zoom) / 100 + 40);

    btron_dnd_begin(1, 501, VOBJ_TYPE_TEXT, "Heart_Sutra_Tibetan.txt", "assets/texts/Heart_Sutra_Tibetan.txt", 10, 10);
    clarity_handle_dnd_drop(&doc, btron_dnd_get(), drop_x, drop_y, ox, oy);
    btron_dnd_end();

    TEST_ASSERT(txt_f->vobj_count == 1, "Frame 1 relinked to exactly 1 Virtual Body moniker");
    TEST_ASSERT(txt_f->vobjs[0].target_robj == 501, "Moniker target_robj updated to 501");
    TEST_ASSERT(strstr(txt_f->vobjs[0].label, "Heart_Sutra_Tibetan.txt") != NULL, "Moniker label updated to Heart Sutra");
    TEST_ASSERT(txt_f->text_len > 50, "Frame 1 loaded real body content (>50 characters)");

    /* Verify Tibetan TRON Code units (0x9F00 plane) are present in the text frame */
    BOOL has_tibetan = FALSE;
    for (UW i = 0; i < txt_f->text_len; i++) {
        if (txt_f->text[i] >= 0x9F00 && txt_f->text[i] <= 0x9FFF) {
            has_tibetan = TRUE;
            break;
        }
    }
    TEST_ASSERT(has_tibetan == TRUE, "Frame 1 text buffer contains Tibetan TRON Code units");

    /* Verify dropping TAD file onto Text Frame is rejected (no corruption) */
    UW prev_text_len = txt_f->text_len;
    btron_dnd_begin(1, 601, VOBJ_TYPE_TEXT, "01_btron3_spec.tad", "tad_bin/01_btron3_spec.tad", 10, 10);
    clarity_handle_dnd_drop(&doc, btron_dnd_get(), drop_x, drop_y, ox, oy);
    btron_dnd_end();

    TEST_ASSERT(txt_f->text_len == prev_text_len, "Dropping TAD file onto Text Frame does not corrupt text");
    TEST_ASSERT(strstr(txt_f->vobjs[0].label, "Heart_Sutra_Tibetan.txt") != NULL, "Moniker link preserved Heart Sutra");

    for (int i = 0; i < doc.frame_count; i++) {
        if (doc.frames[i].bitmap) free(doc.frames[i].bitmap);
    }
}

int main(void) {
    setbuf(stdout, NULL);
    printf("=================================================================\n");
    printf(" BTRON 3.20 Hypermedia Phase 2 Automated Verification Suite      \n");
    printf("=================================================================\n");

    test_vobj_persistence();
    test_image_decoder();
    test_tad_serialization();
    test_dnd_workflows();
    test_tad_placeholder_and_sys_persistence();
    test_dnd_relink_and_multilingual_rerender();

    printf("=================================================================\n");
    printf(" RESULTS: %d Passed, %d Failed\n", g_tests_passed, g_tests_failed);
    printf("=================================================================\n");

    return (g_tests_failed == 0) ? 0 : 1;
}
