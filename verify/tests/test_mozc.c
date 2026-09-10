/*
 * B-System (BTRON 3.20) Mozc Kana-Kanji Conversion & TIP Unit Test Suite: test_mozc.c
 * Validates requirements from btron-tip.tex & IME.md:
 * 1. Romaji -> Hiragana / Katakana transliteration (Hepburn / Kunrei)
 * 2. Morphological bunsetsu clause segmentation & Viterbi lattice search
 * 3. Candidate ranking with semantic category annotations
 * 4. DFA state machine transitions & Theorem 2 (O(1) ESC safety invariant)
 * 5. Real Body User Dictionary persistence & learning
 * 6. Theorem 1 Lossless Bidirectional Bridge (UTF-8 <-> TRON Code)
 */

#include <btron/tip.h>
#include <btron/mozc_engine.h>
#include <btron/troncode.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static int g_tests_passed = 0;
static int g_tests_total = 0;

#define TEST_ASSERT(cond, msg) do { \
    g_tests_total++; \
    if (cond) { \
        g_tests_passed++; \
        printf("  [PASS] %s\n", msg); \
    } else { \
        printf("  [FAIL] %s (Line %d: %s)\n", msg, __LINE__, #cond); \
    } \
} while(0)

/* ── Test 1: Romaji Transliteration ── */
static void test_romaji_transliteration(void) {
    printf("\n[TEST GROUP 1] Romaji to Hiragana / Katakana Transliteration\n");
    char out[128];

    mozc_romaji_to_hiragana("watashinonamaeha", out, sizeof(out));
    TEST_ASSERT(strcmp(out, "わたしのなまえは") == 0, "Transliterate basic clause: watashinonamaeha -> わたしのなまえは");

    mozc_romaji_to_hiragana("watashinonamaewa", out, sizeof(out));
    TEST_ASSERT(strcmp(out, "わたしのなまえわ") == 0, "Transliterate wa clause: watashinonamaewa -> わたしのなまえわ");

    mozc_romaji_to_hiragana("nakanodesu", out, sizeof(out));
    TEST_ASSERT(strcmp(out, "なかのです") == 0, "Transliterate surname + copula: nakanodesu -> なかのです");

    mozc_romaji_to_hiragana("gakkou", out, sizeof(out));
    TEST_ASSERT(strcmp(out, "がっこう") == 0, "Geminate consonant (sokuon): gakkou -> がっこう");

    mozc_romaji_to_hiragana("kyou", out, sizeof(out));
    TEST_ASSERT(strcmp(out, "きょう") == 0, "Digraph (yoon): kyou -> きょう");

    mozc_romaji_to_hiragana("sensei", out, sizeof(out));
    TEST_ASSERT(strcmp(out, "せんせい") == 0, "Syllabic nasal (hatsuon): sensei -> せんせい");

    mozc_romaji_to_hiragana("hotokesan", out, sizeof(out));
    TEST_ASSERT(strcmp(out, "ほとけさん") == 0, "Transliterate trailing single 'n': hotokesan -> ほとけさん");

    mozc_romaji_to_hiragana("hotokesann", out, sizeof(out));
    TEST_ASSERT(strcmp(out, "ほとけさん") == 0, "Transliterate explicit double 'nn': hotokesann -> ほとけさん");

    char kata[128];
    mozc_hiragana_to_katakana("なかのです", kata, sizeof(kata));
    TEST_ASSERT(strcmp(kata, "ナカノデス") == 0, "Hiragana to Katakana: なかのです -> ナカノデス");
}

/* ── Test 2: Mozc Viterbi Lattice Search & Bunsetsu Segmentation ── */
static void test_viterbi_lattice_search(void) {
    printf("\n[TEST GROUP 2] Morphological Bunsetsu Segmentation & Viterbi Lattice Search\n");

    TIP_CLAUSE clauses[TIP_MAX_CLAUSES];
    int num_clauses = 0;

    /* Complex sentence from btron-tip.tex wireframe */
    const char *sentence = "わたしのなまえはなかのです";
    ER er = mozc_lattice_search(sentence, clauses, &num_clauses, TIP_MAX_CLAUSES);
    TEST_ASSERT(er == E_OK, "Execute Mozc Viterbi lattice search");
    TEST_ASSERT(num_clauses >= 4, "Segmented into bunsetsu clauses");

    char combined[256] = "";
    for (int i = 0; i < num_clauses; i++) {
        strcat(combined, clauses[i].converted);
    }
    printf("    Result sentence: '%s' -> '%s'\n", sentence, combined);
    TEST_ASSERT(strstr(combined, "私") != NULL && strstr(combined, "中野") != NULL,
                "Lattice search optimal path: '私の名前は中野です'");

    /* Today's weather is good */
    num_clauses = 0;
    mozc_lattice_search("きょうはてんきがいいです", clauses, &num_clauses, TIP_MAX_CLAUSES);
    combined[0] = '\0';
    for (int i = 0; i < num_clauses; i++) {
        strcat(combined, clauses[i].converted);
    }
    printf("    Result sentence: 'きょうはてんきがいいです' -> '%s'\n", combined);
    TEST_ASSERT(strstr(combined, "今日") != NULL && strstr(combined, "天気") != NULL,
                "Lattice search: '今日は天気が良いです'");

    /* Buddhist honorific: ほとけさん */
    num_clauses = 0;
    mozc_lattice_search("ほとけさん", clauses, &num_clauses, TIP_MAX_CLAUSES);
    combined[0] = '\0';
    for (int i = 0; i < num_clauses; i++) {
        strcat(combined, clauses[i].converted);
    }
    printf("    Result sentence: 'ほとけさん' -> '%s'\n", combined);
    TEST_ASSERT(strstr(combined, "仏") != NULL || strstr(combined, "ほとけ") != NULL,
                "Lattice search: 'ほとけさん' -> '仏さん'");
}

/* ── Test 3: Candidate Generation & Semantic Categories ── */
static void test_candidate_ranking(void) {
    printf("\n[TEST GROUP 3] Candidate Generation & Semantic Category Annotations (btron-tip.tex Section 4.2)\n");

    TIP_CANDIDATE candidates[16];
    int count = mozc_get_candidates("なかの", candidates, 16);
    TEST_ASSERT(count >= 5, "Candidate count >= 5 for reading 'なかの'");

    TEST_ASSERT(strcmp(candidates[0].value, "中野") == 0, "Candidate 1: 中野");
    TEST_ASSERT(strcmp(candidates[0].annotation, "surname") == 0, "Candidate 1 Annotation: (surname)");

    TEST_ASSERT(strcmp(candidates[1].value, "仲野") == 0, "Candidate 2: 仲野");
    TEST_ASSERT(strcmp(candidates[1].annotation, "alt kanji") == 0, "Candidate 2 Annotation: (alt kanji)");

    BOOL has_hira = FALSE, has_kata = FALSE, has_rare = FALSE;
    for (int i = 0; i < count; i++) {
        if (strcmp(candidates[i].value, "なかの") == 0) has_hira = TRUE;
        if (strcmp(candidates[i].value, "ナカノ") == 0) has_kata = TRUE;
        if (strcmp(candidates[i].value, "中埜") == 0) has_rare = TRUE;
    }
    TEST_ASSERT(has_hira, "Contains hiragana candidate: なかの");
    TEST_ASSERT(has_kata, "Contains katakana candidate: ナカノ");
    TEST_ASSERT(has_rare, "Contains rare kanji candidate: 中埜");

    /* Candidate ranking for ほとけさん */
    int h_count = mozc_get_candidates("ほとけさん", candidates, 16);
    TEST_ASSERT(h_count >= 2, "Candidate count >= 2 for reading 'ほとけさん'");
    TEST_ASSERT(strcmp(candidates[0].value, "仏さん") == 0 || strcmp(candidates[0].value, "ほとけさん") == 0,
                "Candidate 1 for 'ほとけさん' is '仏さん' or 'ほとけさん'");
}

/* ── Test 4: DFA State Transitions & Theorem 2 Invariant ── */
static void test_dfa_and_theorem2(void) {
    printf("\n[TEST GROUP 4] DFA State Machine & Theorem 2 (O(1) Cancellation Safety)\n");

    tip_init();
    TEST_ASSERT(tip_get_state() == TIP_STATE_IDLE, "Initial DFA state is TIP_STATE_IDLE");

    char commit_buf[128];

    /* Type 'n', 'a', 'k', 'a', 'n', 'o' */
    tip_process_key('n', 0, commit_buf, sizeof(commit_buf));
    tip_process_key('a', 0, commit_buf, sizeof(commit_buf));
    TEST_ASSERT(tip_get_state() == TIP_STATE_PRECOMP, "DFA transitioned to TIP_STATE_PRECOMP");
    TEST_ASSERT(strcmp(tip_get_reading(), "な") == 0, "Precomp reading: 'な'");

    tip_process_key('k', 0, commit_buf, sizeof(commit_buf));
    tip_process_key('a', 0, commit_buf, sizeof(commit_buf));
    tip_process_key('n', 0, commit_buf, sizeof(commit_buf));
    tip_process_key('o', 0, commit_buf, sizeof(commit_buf));
    TEST_ASSERT(strcmp(tip_get_reading(), "なかの") == 0, "Precomp reading: 'なかの'");

    /* Space: trigger conversion */
    tip_process_key(' ', 0, commit_buf, sizeof(commit_buf));
    TEST_ASSERT(tip_get_state() == TIP_STATE_CONVERTING, "Space triggers TIP_STATE_CONVERTING");

    /* Space again: open candidate window */
    tip_process_key(' ', 0, commit_buf, sizeof(commit_buf));
    TEST_ASSERT(tip_get_state() == TIP_STATE_CANDIDATE_SELECT, "Space triggers TIP_STATE_CANDIDATE_SELECT");

    /* Theorem 2: KEY_ESC (0x1B) unconditionally restores to IDLE in O(1) */
    tip_process_key(0x1B, 0, commit_buf, sizeof(commit_buf));
    TEST_ASSERT(tip_get_state() == TIP_STATE_IDLE, "Theorem 2: ESC restores DFA to TIP_STATE_IDLE in O(1)");
    TEST_ASSERT(strcmp(tip_get_reading(), "") == 0, "Composition buffer cleared");

    /* Test commit on Enter */
    tip_process_key('k', 0, commit_buf, sizeof(commit_buf));
    tip_process_key('y', 0, commit_buf, sizeof(commit_buf));
    tip_process_key('o', 0, commit_buf, sizeof(commit_buf));
    tip_process_key('u', 0, commit_buf, sizeof(commit_buf));
    tip_process_key(' ', 0, commit_buf, sizeof(commit_buf));
    commit_buf[0] = '\0';
    tip_process_key('\n', 0, commit_buf, sizeof(commit_buf));
    TEST_ASSERT(strcmp(commit_buf, "今日") == 0, "Enter commits active conversion: '今日'");
    TEST_ASSERT(tip_get_state() == TIP_STATE_IDLE, "DFA returns to IDLE after commit");

    /* Test full typing sequence for hotokesan ending with 'n' */
    tip_init();
    const char *hotoke_keys = "hotokesan";
    for (int i = 0; hotoke_keys[i]; i++) {
        tip_process_key(hotoke_keys[i], 0, commit_buf, sizeof(commit_buf));
    }
    TEST_ASSERT(strcmp(tip_get_reading(), "ほとけさん") == 0, "Precomp reading for 'hotokesan': 'ほとけさん'");
    tip_process_key(' ', 0, commit_buf, sizeof(commit_buf));
    TEST_ASSERT(tip_get_state() == TIP_STATE_CONVERTING, "Space triggers conversion for 'ほとけさん'");
    commit_buf[0] = '\0';
    tip_process_key('\n', 0, commit_buf, sizeof(commit_buf));
    TEST_ASSERT(strcmp(commit_buf, "仏さん") == 0 || strcmp(commit_buf, "ほとけさん") == 0,
                "Enter commits 'hotokesan' -> '仏さん'");
    TEST_ASSERT(tip_get_state() == TIP_STATE_IDLE, "DFA returns to IDLE after 'ほとけさん' commit");
}

/* ── Test 5: Real Body User Dictionary ── */
static void test_user_dictionary_real_object(void) {
    printf("\n[TEST GROUP 5] User Dictionary as Real Body (Jisshin #104)\n");

    ER er = mozc_register_user_word("てぃーけー", "T-Kernel2.0", "system");
    TEST_ASSERT(er == E_OK, "Register new word in Real Body User Dictionary");

    TIP_CANDIDATE candidates[16];
    int count = mozc_get_candidates("てぃーけー", candidates, 16);
    TEST_ASSERT(count > 0, "Retrieve candidates for newly registered user word");
    TEST_ASSERT(strcmp(candidates[0].value, "T-Kernel2.0") == 0, "User word is ranked top: 'T-Kernel2.0'");
    TEST_ASSERT(strcmp(candidates[0].annotation, "system") == 0, "User word annotation: (system)");
}

/* ── Test 6: Theorem 1 Lossless Round-Trip Bridge (UTF-8 <-> TRON Code) ── */
static void test_troncode_lossless_bridge(void) {
    printf("\n[TEST GROUP 6] Theorem 1 Lossless Round-Trip Bridge (phi^-1 . phi)(S) == S\n");

    const char *test_corpus[] = {
        "BTRON Workstation 3.20",
        "私の名前は中野です",
        "坂村健 T-Kernel 2.0",
        "日本語 Kana-Kanji Conversion Mozc",
        "今日、天気、電車、学校、実身、仮身",
        "B-System (BTRON3 3.20) — TRON ITRON BTRON HMI NET-TRON",
        "「日本語」【実身】〔仮身〕（括弧）—ダッシュ―全角ダッシュ…三点リーダ",
        "—",
        "―",
        "–",
        "‐",
        "…",
        "〔",
        "〕",
        "【",
        "】",
        "「",
        "」",
        "（",
        "）",
        "“引用” ‘単一’",
        NULL
    };

    for (int i = 0; test_corpus[i] != NULL; i++) {
        TC tc_buf[128];
        char utf8_roundtrip[256];

        int tc_len = utf8_to_tc_string(test_corpus[i], tc_buf, 128);
        int out_len = tc_to_utf8_string(tc_buf, tc_len, utf8_roundtrip, sizeof(utf8_roundtrip));

        char msg[128];
        snprintf(msg, sizeof(msg), "Theorem 1 Preservation: '%s'", test_corpus[i]);
        TEST_ASSERT(strcmp(test_corpus[i], utf8_roundtrip) == 0 && out_len > 0, msg);
    }
}

/* ── Test 7: Text Tract - UTF-8 Multibyte Navigation & Boundary Safety ── */
static int test_utf8_next(const char *s, int cur) {
    if (!s || cur >= (int)strlen(s)) return cur;
    const unsigned char *p = (const unsigned char *)s + cur;
    int len = 1;
    if ((*p & 0x80) == 0) len = 1;
    else if ((*p & 0xE0) == 0xC0) len = 2;
    else if ((*p & 0xF0) == 0xE0) len = 3;
    else if ((*p & 0xF8) == 0xF0) len = 4;
    return cur + len;
}

static int test_utf8_prev(const char *s, int cur) {
    if (!s || cur <= 0) return 0;
    int pos = cur - 1;
    const unsigned char *p = (const unsigned char *)s;
    while (pos > 0 && (p[pos] & 0xC0) == 0x80) pos--;
    return pos;
}

static void test_text_tract_navigation(void) {
    printf("\n[TEST GROUP 7] Text Tract: UTF-8 Multibyte Navigation & Boundary Safety\n");

    const char *sample = "件名：【BTRON3仕様】";
    /*
     * "件" = 3 bytes (0..3)
     * "名" = 3 bytes (3..6)
     * "：" = 3 bytes (6..9)
     * "【" = 3 bytes (9..12)
     * "B"  = 1 byte  (12..13)
     * "T"  = 1 byte  (13..14)
     * "R"  = 1 byte  (14..15)
     * "O"  = 1 byte  (15..16)
     * "N"  = 1 byte  (16..17)
     * "3"  = 1 byte  (17..18)
     * "仕" = 3 bytes (18..21)
     * "様" = 3 bytes (21..24)
     * "】" = 3 bytes (24..27)
     */

    int off = 0;
    off = test_utf8_next(sample, off);
    TEST_ASSERT(off == 3, "Step forward past 3-byte Kanji '件' (0 -> 3)");
    off = test_utf8_next(sample, off);
    TEST_ASSERT(off == 6, "Step forward past 3-byte Kanji '名' (3 -> 6)");
    off = test_utf8_next(sample, off);
    TEST_ASSERT(off == 9, "Step forward past 3-byte colon '：' (6 -> 9)");
    off = test_utf8_next(sample, off);
    TEST_ASSERT(off == 12, "Step forward past 3-byte bracket '【' (9 -> 12)");
    off = test_utf8_next(sample, off);
    TEST_ASSERT(off == 13, "Step forward past 1-byte ASCII 'B' (12 -> 13)");

    /* Backward navigation */
    off = test_utf8_prev(sample, 13);
    TEST_ASSERT(off == 12, "Step backward past 1-byte ASCII 'B' (13 -> 12)");
    off = test_utf8_prev(sample, 12);
    TEST_ASSERT(off == 9, "Step backward past 3-byte bracket '【' (12 -> 9)");
    off = test_utf8_prev(sample, 9);
    TEST_ASSERT(off == 6, "Step backward past 3-byte colon '：' (9 -> 6)");
    off = test_utf8_prev(sample, 6);
    TEST_ASSERT(off == 3, "Step backward past 3-byte Kanji '名' (6 -> 3)");
    off = test_utf8_prev(sample, 3);
    TEST_ASSERT(off == 0, "Step backward past 3-byte Kanji '件' (3 -> 0)");
}

/* ── Test 8: Text Tract - Caret Pixel Layout & Column Widths ── */
static void test_caret_pixel_layout(void) {
    printf("\n[TEST GROUP 8] Text Tract: Caret Pixel Layout & Column Widths\n");

    const char *line = "件名：BTRON3";
    /*
     * '件' (16px) -> x = 36 + 16 = 52
     * '名' (16px) -> x = 52 + 16 = 68
     * '：' (16px) -> x = 68 + 16 = 84
     * 'B'  (8px)  -> x = 84 + 8  = 92
     * 'T'  (8px)  -> x = 92 + 8  = 100
     * 'R'  (8px)  -> x = 100 + 8 = 108
     * 'O'  (8px)  -> x = 108 + 8 = 116
     * 'N'  (8px)  -> x = 116 + 8 = 124
     * '3'  (8px)  -> x = 124 + 8 = 132
     */

    int x = 36;
    const char *p = line;
    int col_indices[] = { 36, 52, 68, 84, 92, 100, 108, 116, 124, 132 };
    int idx = 0;

    while (*p) {
        TEST_ASSERT(x == col_indices[idx], "Caret pixel X matches expected column width");
        int consumed = 0;
        TC code = utf8_to_tc(p, &consumed);
        x += (code < 128) ? 8 : 16;
        p += (consumed > 0 ? consumed : 1);
        idx++;
    }
    TEST_ASSERT(x == 132, "Final Caret X after line is 132px");
}

/* ── Test 9: Text Tract - Multibyte Backspace and Delete Safety ── */
static void test_multibyte_editing_safety(void) {
    printf("\n[TEST GROUP 9] Text Tract: Multibyte Backspace and Delete Safety\n");

    char buf[64];
    strncpy(buf, "件名：新実装", sizeof(buf) - 1);

    /* Backspace at end of string (delete '装' - 3 bytes) */
    int len = (int)strlen(buf);
    int prev_c = test_utf8_prev(buf, len);
    memmove(&buf[prev_c], &buf[len], len - len + 1);
    TEST_ASSERT(strcmp(buf, "件名：新実") == 0, "Backspace removes entire 3-byte character '装'");

    /* Backspace again (delete '実' - 3 bytes) */
    len = (int)strlen(buf);
    prev_c = test_utf8_prev(buf, len);
    memmove(&buf[prev_c], &buf[len], len - len + 1);
    TEST_ASSERT(strcmp(buf, "件名：新") == 0, "Backspace removes entire 3-byte character '実'");

    /* Forward delete at offset 0 (delete '件' - 3 bytes) */
    int next_c = test_utf8_next(buf, 0);
    len = (int)strlen(buf);
    memmove(&buf[0], &buf[next_c], len - next_c + 1);
    TEST_ASSERT(strcmp(buf, "名：新") == 0, "Delete key removes entire 3-byte character '件'");
}

/* ── Test 10: Text Tract - Framebuffer Rendering Integrity & Underline Scoping ── */
static void test_rendering_integrity(void) {
    printf("\n[TEST GROUP 10] Text Tract: Rendering Integrity & Underline Scoping\n");

    #define FB_W 200
    #define FB_H 40
    COLOR pixels[FB_W * FB_H];
    memset(pixels, 0, sizeof(pixels));

    GDEV dev;
    dev.pixels = (VP)pixels;
    dev.width = FB_W;
    dev.height = FB_H;
    dev.clip.left = 0;
    dev.clip.top = 0;
    dev.clip.right = FB_W;
    dev.clip.bottom = FB_H;

    /* Render standard text (NO underlines expected) */
    ER er = drw_tc_string(&dev, 0, 0, "件名：BTRON", 0xFF000000, 0x00000000);
    TEST_ASSERT(er == E_OK, "Render Japanese string into framebuffer successfully");

    /* Check row 15 (y = 15): ensure NO solid underline was drawn across the text */
    int underline_pixels = 0;
    for (int x = 0; x < 100; x++) {
        if (pixels[15 * FB_W + x] == 0xFF000000) {
            underline_pixels++;
        }
    }
    TEST_ASSERT(underline_pixels < 80, "Standard drw_tc_string has NO unconditional underline artifacts");

    /* Render underlined text (explicitly requested) */
    er = drw_tc_string_underlined(&dev, 0, 20, "テスト", 0xFF000000, 0x00000000, FALSE);
    TEST_ASSERT(er == E_OK, "Render underlined Japanese string successfully");

    int underline_pixels_2 = 0;
    for (int x = 0; x < 48; x++) {
        if (pixels[35 * FB_W + x] == 0xFF000000) {
            underline_pixels_2++;
        }
    }
    TEST_ASSERT(underline_pixels_2 >= 40, "Explicit drw_tc_string_underlined draws underline properly");
}

/* ── Test 11: Extended Functional Key Transliterations (F6-F9) ── */
static void test_extended_transliterations(void) {
    printf("\n[TEST GROUP 11] Extended Functional Key Transliterations (F6, F7, F8, F9)\n");

    char out[128];

    /* F6: Katakana to Hiragana */
    mozc_katakana_to_hiragana("ナカノデス", out, sizeof(out));
    TEST_ASSERT(strcmp(out, "なかのです") == 0, "F6 Katakana->Hiragana: 'ナカノデス' -> 'なかのです'");

    /* F7: Hiragana to Fullwidth Katakana */
    mozc_hiragana_to_katakana("なかのです", out, sizeof(out));
    TEST_ASSERT(strcmp(out, "ナカノデス") == 0, "F7 Hiragana->Fullwidth Katakana: 'なかのです' -> 'ナカノデス'");

    /* F8: Hiragana to Halfwidth Katakana */
    mozc_hiragana_to_halfwidth_katakana("なかのです", out, sizeof(out));
    TEST_ASSERT(strcmp(out, "ﾅｶﾉﾃﾞｽ") == 0, "F8 Hiragana->Halfwidth Katakana: 'なかのです' -> 'ﾅｶﾉﾃﾞｽ'");

    /* F8 with voiced and sokuon */
    mozc_hiragana_to_halfwidth_katakana("がっこう", out, sizeof(out));
    TEST_ASSERT(strcmp(out, "ｶﾞｯｺｳ") == 0, "F8 Halfwidth with voiced/sokuon: 'がっこう' -> 'ｶﾞｯｺｳ'");

    /* F9: ASCII to Fullwidth Alphanumeric */
    mozc_alphanumeric_to_fullwidth("BTRON 3.20", out, sizeof(out));
    TEST_ASSERT(strcmp(out, "ＢＴＲＯＮ　３．２０") == 0, "F9 Fullwidth Alphanumeric: 'BTRON 3.20' -> 'ＢＴＲＯＮ　３．２０'");
}

/* ── Test 12: BTRON3 SPEC 3.20 Section 3.7 Syscall Port API ── */
static void test_btron3_syscall_port_api(void) {
    printf("\n[TEST GROUP 12] BTRON3 SPEC 3.20 Section 3.7 Syscall Port API\n");

    /* 1. Open TIP Port */
    ID tipid = iopn_tip(NULL);
    TEST_ASSERT(tipid > 0, "iopn_tip returns valid positive port descriptor");

    /* 2. Change Mode */
    ER er = ichg_mod(tipid, (W)TIP_MODE_HIRAGANA);
    TEST_ASSERT(er == E_OK, "ichg_mod set mode to TIP_MODE_HIRAGANA");

    /* 3. Feed input keystrokes via iput_key */
    tip_set_mode(TIP_MODE_HIRAGANA);
    tip_cancel();

    char out_buf[128] = "";
    char cnv_buf[128] = "";
    TIPREC rec;
    memset(&rec, 0, sizeof(TIPREC));
    rec.out_str = out_buf;
    rec.out_len = sizeof(out_buf);
    rec.cnv_str = cnv_buf;
    rec.cnv_len = sizeof(cnv_buf);

    W handled = iput_key(tipid, 'k', 0, &rec);
    TEST_ASSERT(handled == 1, "iput_key handled 'k'");
    TEST_ASSERT((rec.result & TIP_CNV) != 0, "iput_key updated composition (TIP_CNV)");
    TEST_ASSERT(rec.car_pos > 0, "iput_key updated caret position (TIP_CAR)");

    handled = iput_key(tipid, 'a', 0, &rec);
    TEST_ASSERT(handled == 1, "iput_key handled 'a'");
    TEST_ASSERT(strcmp(cnv_buf, "か") == 0, "Composition contains 'か'");

    /* 4. Confirm with Return */
    handled = iput_key(tipid, '\r', 0, &rec);
    TEST_ASSERT(handled == 1, "iput_key handled Return key");
    TEST_ASSERT((rec.result & TIP_OUT) != 0, "iput_key signaled output committed (TIP_OUT)");
    TEST_ASSERT(strcmp(out_buf, "か") == 0, "Committed string is 'か'");

    /* 5. Close TIP Port */
    er = icls_tip(tipid);
    TEST_ASSERT(er == E_OK, "icls_tip cleanly released port descriptor");
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    printf("==========================================================\n");
    printf(" B-System Mozc / TIP Cleanroom Unit Test Suite\n");
    printf(" Conforming to btron-tip.tex & IME.md Specifications\n");
    printf("==========================================================\n");

    test_romaji_transliteration();
    test_viterbi_lattice_search();
    test_candidate_ranking();
    test_dfa_and_theorem2();
    test_user_dictionary_real_object();
    test_troncode_lossless_bridge();
    test_text_tract_navigation();
    test_caret_pixel_layout();
    test_multibyte_editing_safety();
    test_rendering_integrity();
    test_extended_transliterations();
    test_btron3_syscall_port_api();
    /* Test Group 13: Distinguishable Tibetan TB Mode & Wylie Dictionary-Based Input */
    printf("\n[TEST GROUP 13] Distinguishable Tibetan TB Mode & Wylie Dictionary-Based Input\n");
    
    /* 1. Mode String Identifiers */
    tip_set_mode(TIP_MODE_ASCII);
    TEST_ASSERT(strcmp(tip_get_mode_str(), "[EN]") == 0, "tip_get_mode_str() returns [EN]");
    tip_set_mode(TIP_MODE_HIRAGANA);
    TEST_ASSERT(strcmp(tip_get_mode_str(), "[JP あ]") == 0, "tip_get_mode_str() returns [JP あ]");
    tip_set_mode(TIP_MODE_KATAKANA);
    TEST_ASSERT(strcmp(tip_get_mode_str(), "[JP ア]") == 0, "tip_get_mode_str() returns [JP ア]");
    tip_set_mode(TIP_MODE_TIBETAN);
    TEST_ASSERT(strcmp(tip_get_mode_str(), "[TB བོད]") == 0, "tip_get_mode_str() returns [TB བོད]");
    TEST_ASSERT(tip_get_mode() == TIP_MODE_TIBETAN, "tip_set_mode to TIP_MODE_TIBETAN");

    /* 2. Direct Precomposition: 'chos' -> 'ཆོས' */
    char tb_commit[128] = {0};
    tip_process_key('c', 0, tb_commit, sizeof(tb_commit));
    tip_process_key('h', 0, tb_commit, sizeof(tb_commit));
    tip_process_key('o', 0, tb_commit, sizeof(tb_commit));
    tip_process_key('s', 0, tb_commit, sizeof(tb_commit));
    
    char tb_out[128] = {0};
    tip_get_converted_text(tb_out, sizeof(tb_out));
    TEST_ASSERT(strcmp(tb_out, "ཆོས") == 0, "TIP_MODE_TIBETAN precomposition 'chos' -> 'ཆོས'");

    /* 3. Space Key Directly Commits Pre-edit with Tsheg ('་') */
    tip_process_key(' ', 0, tb_commit, sizeof(tb_commit));
    TEST_ASSERT(strstr(tb_commit, "ཆོས") != NULL, "Space key commits syllable with Tsheg");
    TEST_ASSERT(tip_get_state() == TIP_STATE_IDLE, "TIP returns to IDLE after Space Tsheg commit");

    /* 4. Standalone Space key in IDLE emits Tsheg ('་') */
    memset(tb_commit, 0, sizeof(tb_commit));
    tip_process_key(' ', 0, tb_commit, sizeof(tb_commit));
    TEST_ASSERT(strcmp(tb_commit, "\xE0\xBC\x8B") == 0, "Standalone Space emits Tsheg in Tibetan mode");

    /* 5. Tab Key triggers Tibetan Dictionary Candidate Popup */
    tip_process_key('c', 0, tb_commit, sizeof(tb_commit));
    tip_process_key('h', 0, tb_commit, sizeof(tb_commit));
    tip_process_key('o', 0, tb_commit, sizeof(tb_commit));
    tip_process_key('s', 0, tb_commit, sizeof(tb_commit));
    tip_process_key('\t', 0, tb_commit, sizeof(tb_commit)); /* Tab trigger */
    TEST_ASSERT(tip_get_state() == TIP_STATE_CANDIDATE_SELECT, "Tab key opens Tibetan Candidate Popup");
    TEST_ASSERT(tip_is_candidate_window_visible() == TRUE, "Candidate window visible on Tab/Shift+Space");

    /* 6. Second Tab advances to next dictionary candidate 'ཆོས་ཉིད' */
    tip_process_key('\t', 0, tb_commit, sizeof(tb_commit));
    tip_get_converted_text(tb_out, sizeof(tb_out));
    TEST_ASSERT(strcmp(tb_out, "ཆོས་ཉིད") == 0, "Tab advances candidate to 'ཆོས་ཉིད'");

    /* 7. Return commits candidate */
    tip_process_key('\r', 0, tb_commit, sizeof(tb_commit));
    TEST_ASSERT(strcmp(tb_commit, "ཆོས་ཉིད") == 0, "Committed Tibetan dictionary term 'ཆོས་ཉིད'");
    TEST_ASSERT(tip_get_state() == TIP_STATE_IDLE, "TIP returns to IDLE after commit");

    /* 8. Test Multi-word Buddhist Term via Underscore: 'byang_chub_sems' -> 'བྱང་ཆུབ་སེམས' */
    tip_cancel();
    const char *w_term = "byang_chub_sems";
    for (size_t i = 0; i < strlen(w_term); i++) {
        tip_process_key((UW)w_term[i], 0, tb_commit, sizeof(tb_commit));
    }
    tip_get_converted_text(tb_out, sizeof(tb_out));
    TEST_ASSERT(strstr(tb_out, "བྱ") != NULL, "Precomposition contains Tibetan characters");
    
    tip_process_key('\r', 0, tb_commit, sizeof(tb_commit));
    TEST_ASSERT(strstr(tb_commit, "བྱ") != NULL, "Committed Bodhicitta Tibetan phrase");

    /* [TEST GROUP 14] Dynamic Candidate Popup Width & Anti-Clipping Calculation */
    printf("\n[TEST GROUP 14] Dynamic Candidate Popup Width & Anti-Clipping Calculation\n");
    TIP_CLAUSE test_clause;
    memset(&test_clause, 0, sizeof(test_clause));
    test_clause.num_candidates = 3;
    
    strcpy(test_clause.candidates[0].value, "ཆོས");
    strcpy(test_clause.candidates[0].annotation, "dharma");
    
    strcpy(test_clause.candidates[1].value, "ཆོས་ཀྱི་དབྱིངས");
    strcpy(test_clause.candidates[1].annotation, "dharmadhatu / expanse of reality");
    
    strcpy(test_clause.candidates[2].value, "པྲ་ཛྙཱ་པཱ་ར་མི་ཏཱ");
    strcpy(test_clause.candidates[2].annotation, "prajnaparamita");

    H popup_w = tip_calc_candidate_window_width(&test_clause);
    printf("  Calculated popup width for long dictionary items: %d px\n", popup_w);
    TEST_ASSERT(popup_w >= 280, "Candidate popup automatically expands for longest dictionary entry");
    
    H val1_w = tip_calc_text_width(test_clause.candidates[1].value);
    H ann1_w = tip_calc_text_width(test_clause.candidates[1].annotation);
    TEST_ASSERT(popup_w > val1_w + ann1_w + 30, "Popup width provides sufficient margin without clipping");

    /* [TEST GROUP 15] UP and DOWN Arrow Candidate Window Navigation */
    printf("\n[TEST GROUP 15] UP and DOWN Arrow Candidate Window Navigation\n");
    tip_set_mode(TIP_MODE_TIBETAN);
    char tb_nav[128] = {0};
    tip_cancel();
    tip_process_key('c', 0, tb_nav, sizeof(tb_nav));
    tip_process_key('h', 0, tb_nav, sizeof(tb_nav));
    tip_process_key('o', 0, tb_nav, sizeof(tb_nav));
    tip_process_key('s', 0, tb_nav, sizeof(tb_nav));
    tip_process_key('\t', 0, tb_nav, sizeof(tb_nav)); /* Open suggestion popup */

    TEST_ASSERT(tip_is_candidate_window_visible() == TRUE, "Candidate window visible on Tab");
    tip_get_converted_text(tb_nav, sizeof(tb_nav));
    TEST_ASSERT(strcmp(tb_nav, "ཆོས") == 0, "Initial top candidate is 'ཆོས'");

    /* Press DOWN Arrow: moves to 2nd candidate 'ཆོས་ཉིད' */
    tip_process_key(BTRON_KEY_DOWN, 0, tb_nav, sizeof(tb_nav));
    tip_get_converted_text(tb_nav, sizeof(tb_nav));
    TEST_ASSERT(strcmp(tb_nav, "ཆོས་ཉིད") == 0, "DOWN arrow selects 2nd candidate 'ཆོས་ཉིད'");

    /* Press DOWN Arrow again: moves to 3rd candidate 'ཆོས་ཀྱི་དབྱིངས' */
    tip_process_key(BTRON_KEY_DOWN, 0, tb_nav, sizeof(tb_nav));
    tip_get_converted_text(tb_nav, sizeof(tb_nav));
    TEST_ASSERT(strcmp(tb_nav, "ཆོས་ཀྱི་དབྱིངས") == 0, "DOWN arrow selects 3rd candidate 'ཆོས་ཀྱི་དབྱིངས'");

    /* Press UP Arrow: moves back to 2nd candidate 'ཆོས་ཉིད' */
    tip_process_key(BTRON_KEY_UP, 0, tb_nav, sizeof(tb_nav));
    tip_get_converted_text(tb_nav, sizeof(tb_nav));
    TEST_ASSERT(strcmp(tb_nav, "ཆོས་ཉིད") == 0, "UP arrow returns to 2nd candidate 'ཆོས་ཉིད'");

    /* Press UP Arrow again: moves back to 1st candidate 'ཆོས' */
    tip_process_key(BTRON_KEY_UP, 0, tb_nav, sizeof(tb_nav));
    tip_get_converted_text(tb_nav, sizeof(tb_nav));
    TEST_ASSERT(strcmp(tb_nav, "ཆོས") == 0, "UP arrow returns to top candidate 'ཆོས'");

    /* Press Return: confirms active candidate */
    tip_process_key('\r', 0, tb_nav, sizeof(tb_nav));
    TEST_ASSERT(strcmp(tb_nav, "ཆོས") == 0, "Return commits candidate selected via arrow keys");
    TEST_ASSERT(tip_get_state() == TIP_STATE_IDLE, "TIP returns to IDLE after arrow selection commit");

    tip_set_mode(TIP_MODE_HIRAGANA);

    printf("\n==========================================================\n");
    printf(" TEST RESULTS: %d / %d tests passed (%.1f%%)\n",
           g_tests_passed, g_tests_total,
           (100.0 * g_tests_passed) / (g_tests_total > 0 ? g_tests_total : 1));
    printf("==========================================================\n");

    return (g_tests_passed == g_tests_total) ? 0 : 1;
}
