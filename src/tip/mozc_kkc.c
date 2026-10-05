/*
 * B-System (BTRON 3.20) Mozc Statistical Kana-Kanji Conversion (KKC) Engine: mozc_kkc.c
 * Cleanroom implementation conforming to btron-tip.tex Section 3.2 & REQ-2.3.
 */

#include <btron/mozc_engine.h>
#include <btron/tip.h>
#include <btron/libc_shim.h>

/* ── Complete B-System Mozc Romanji-to-Hiragana Conversion Table ── */
typedef struct {
    const char *romaji;
    const char *hiragana;
} RomajiMap;

static const RomajiMap g_romaji_table[] = {
    /* 4-letter combinations */
    {"hwyu", "ふゅ"}, {"t'yu", "てゅ"}, {"d'yu", "でゅ"}, {"xtsu", "っ"}, {"ltsu", "っ"},

    /* 3-letter combinations */
    {"kya", "きゃ"}, {"kyi", "きぃ"}, {"kyu", "きゅ"}, {"kye", "きぇ"}, {"kyo", "きょ"},
    {"gya", "ぎゃ"}, {"gyi", "ぎぃ"}, {"gyu", "ぎゅ"}, {"gye", "ぎぇ"}, {"gyo", "ぎょ"},
    {"sya", "しゃ"}, {"syi", "しぃ"}, {"syu", "しゅ"}, {"sye", "しぇ"}, {"syo", "しょ"},
    {"sha", "しゃ"}, {"shi", "し"}, {"shu", "しゅ"}, {"she", "しぇ"}, {"sho", "しょ"},
    {"zya", "じゃ"}, {"zyi", "じぃ"}, {"zyu", "じゅ"}, {"zye", "じぇ"}, {"zyo", "じょ"},
    {"tya", "ちゃ"}, {"tyi", "ちぃ"}, {"tyu", "ちゅ"}, {"tye", "ちぇ"}, {"tyo", "ちょ"},
    {"cha", "ちゃ"}, {"chi", "ち"}, {"chu", "ちゅ"}, {"che", "ちぇ"}, {"cho", "ちょ"},
    {"cya", "ちゃ"}, {"cyi", "ちぃ"}, {"cyu", "ちゅ"}, {"cye", "ちぇ"}, {"cyo", "ちょ"},
    {"dya", "ぢゃ"}, {"dyi", "ぢぃ"}, {"dyu", "ぢゅ"}, {"dye", "ぢぇ"}, {"dyo", "ぢょ"},
    {"tsa", "つぁ"}, {"tsi", "つぃ"}, {"tse", "つぇ"}, {"tso", "つぉ"}, {"tsu", "つ"},
    {"tha", "てゃ"}, {"thi", "てぃ"}, {"t'i", "てぃ"}, {"thu", "てゅ"}, {"the", "てぇ"}, {"tho", "てょ"},
    {"dha", "でゃ"}, {"dhi", "でぃ"}, {"d'i", "でぃ"}, {"dhu", "でゅ"}, {"dhe", "でぇ"}, {"dho", "でょ"},
    {"twa", "とぁ"}, {"twi", "とぃ"}, {"twu", "とぅ"}, {"twe", "とぇ"}, {"two", "とぉ"}, {"t'u", "とぅ"},
    {"dwa", "どぁ"}, {"dwi", "どぃ"}, {"dwu", "どぅ"}, {"dwe", "どぇ"}, {"dwo", "どぉ"}, {"d'u", "どぅ"},
    {"nya", "にゃ"}, {"nyi", "にぃ"}, {"nyu", "にゅ"}, {"nye", "にぇ"}, {"nyo", "にょ"},
    {"hya", "ひゃ"}, {"hyi", "ひぃ"}, {"hyu", "ひゅ"}, {"hye", "ひぇ"}, {"hyo", "ひょ"},
    {"bya", "びゃ"}, {"byi", "びぃ"}, {"byu", "びゅ"}, {"bye", "びぇ"}, {"byo", "びょ"},
    {"pya", "ぴゃ"}, {"pyi", "ぴぃ"}, {"pyu", "ぴゅ"}, {"pye", "ぴぇ"}, {"pyo", "ぴょ"},
    {"fa", "ふぁ"}, {"fi", "ふぃ"}, {"fu", "ふ"}, {"fe", "ふぇ"}, {"fo", "ふぉ"},
    {"fya", "ふゃ"}, {"fyu", "ふゅ"}, {"fyo", "ふょ"},
    {"hwa", "ふぁ"}, {"hwi", "ふぃ"}, {"hwe", "ふぇ"}, {"hwo", "ふぉ"},
    {"mya", "みゃ"}, {"myi", "みぃ"}, {"myu", "みゅ"}, {"mye", "みぇ"}, {"myo", "みょ"},
    {"rya", "りゃ"}, {"ryi", "りぃ"}, {"ryu", "りゅ"}, {"rye", "りぇ"}, {"ryo", "りょ"},
    {"va", "ゔぁ"}, {"vi", "ゔぃ"}, {"vu", "ゔ"}, {"ve", "ゔぇ"}, {"vo", "ゔぉ"},
    {"vya", "ゔゃ"}, {"vyi", "ゔぃ"}, {"vyu", "ゔゅ"}, {"vye", "ゔぇ"}, {"vyo", "ゔょ"},
    {"jya", "じゃ"}, {"jyi", "じぃ"}, {"jyu", "じゅ"}, {"jye", "じぇ"}, {"jyo", "じょ"},
    {"kwa", "くぁ"}, {"kwi", "くぃ"}, {"kwu", "くぅ"}, {"kwe", "くぇ"}, {"kwo", "くぉ"},
    {"gwa", "ぐぁ"}, {"gwi", "ぐぃ"}, {"gwu", "ぐぅ"}, {"gwe", "ぐぇ"}, {"gwo", "ぐぉ"},
    {"swa", "すぁ"}, {"swi", "すぃ"}, {"swu", "すぅ"}, {"swe", "すぇ"}, {"swo", "すぉ"},
    {"zwa", "ずぁ"}, {"zwi", "ずぃ"}, {"zwu", "ずぅ"}, {"zwe", "ずぇ"}, {"zwo", "ずぉ"},
    {"wha", "うぁ"}, {"whi", "うぃ"}, {"whu", "う"}, {"whe", "うぇ"}, {"who", "うぉ"},
    {"wyi", "ゐ"}, {"wye", "ゑ"},
    {"xya", "ゃ"}, {"xyu", "ゅ"}, {"xyo", "ょ"}, {"xwa", "ゎ"}, {"xka", "ヵ"}, {"xke", "ヶ"},
    {"lya", "ゃ"}, {"lyu", "ゅ"}, {"lyo", "ょ"}, {"lwa", "ゎ"}, {"lka", "ヵ"}, {"lke", "ヶ"},
    {"xtu", "っ"}, {"ltu", "っ"}, {"xyi", "ぃ"}, {"lyi", "ぃ"}, {"xye", "ぇ"}, {"lye", "ぇ"},
    {"tch", "っch"},

    /* 2-letter combinations */
    {"ka", "か"}, {"ki", "き"}, {"ku", "く"}, {"ke", "け"}, {"ko", "こ"},
    {"ga", "が"}, {"gi", "ぎ"}, {"gu", "ぐ"}, {"ge", "げ"}, {"go", "ご"},
    {"sa", "さ"}, {"si", "し"}, {"su", "す"}, {"se", "せ"}, {"so", "そ"},
    {"za", "ざ"}, {"zi", "じ"}, {"zu", "ず"}, {"ze", "ぜ"}, {"zo", "ぞ"},
    {"ta", "た"}, {"ti", "ち"}, {"tu", "つ"}, {"te", "て"}, {"to", "と"},
    {"da", "だ"}, {"di", "ぢ"}, {"du", "づ"}, {"de", "で"}, {"do", "ど"},
    {"na", "な"}, {"ni", "に"}, {"nu", "ぬ"}, {"ne", "ね"}, {"no", "の"},
    {"ha", "は"}, {"hi", "ひ"}, {"hu", "ふ"}, {"he", "へ"}, {"ho", "ほ"},
    {"ba", "ば"}, {"bi", "び"}, {"bu", "ぶ"}, {"be", "べ"}, {"bo", "ぼ"},
    {"pa", "ぱ"}, {"pi", "ぴ"}, {"pu", "ぷ"}, {"pe", "ぺ"}, {"po", "ぽ"},
    {"ma", "ま"}, {"mi", "み"}, {"mu", "む"}, {"me", "め"}, {"mo", "も"},
    {"ya", "や"}, {"yu", "ゆ"}, {"yo", "よ"}, {"ye", "いぇ"},
    {"ra", "ら"}, {"ri", "り"}, {"ru", "る"}, {"re", "れ"}, {"ro", "ろ"},
    {"wa", "わ"}, {"wi", "うぃ"}, {"wu", "う"}, {"we", "うぇ"}, {"wo", "を"},
    {"ja", "じゃ"}, {"ji", "じ"}, {"ju", "じゅ"}, {"je", "じぇ"}, {"jo", "じょ"},
    {"ca", "か"}, {"ci", "し"}, {"cu", "く"}, {"ce", "せ"}, {"co", "こ"},
    {"qa", "くぁ"}, {"qi", "くぃ"}, {"qu", "く"}, {"qe", "くぇ"}, {"qo", "くぉ"},
    {"xa", "ぁ"}, {"xi", "ぃ"}, {"xu", "ぅ"}, {"xe", "ぇ"}, {"xo", "ぉ"},
    {"la", "ぁ"}, {"li", "ぃ"}, {"lu", "ぅ"}, {"le", "ぇ"}, {"lo", "ぉ"},
    {"nn", "ん"}, {"n'", "ん"}, {"xn", "ん"},
    {"z/", "・"}, {"z.", "…"}, {"z,", "‥"}, {"zh", "←"}, {"zj", "↓"}, {"zk", "↑"}, {"zl", "→"}, {"z-", "〜"}, {"z[", "『"}, {"z]", "』"},

    /* 1-letter vowels & symbols */
    {"a", "あ"}, {"i", "い"}, {"u", "う"}, {"e", "え"}, {"o", "お"},
    {"-", "ー"}, {",", "、"}, {".", "。"}, {"~", "〜"}, {"[", "「"}, {"]", "」"},
    {"n", "ん"},

    {NULL, NULL}
};

/* Transliterate Romaji buffer into Hiragana UTF-8 string */
int mozc_romaji_to_hiragana(const char *romaji, char *out_hiragana, int max_len) {
    if (!romaji || !out_hiragana || max_len <= 0) return 0;
    out_hiragana[0] = '\0';

    char lower_romaji[256];
    int r_len = (int)strlen(romaji);
    if (r_len >= (int)sizeof(lower_romaji)) r_len = sizeof(lower_romaji) - 1;
    for (int i = 0; i < r_len; i++) {
        char c = romaji[i];
        if (c >= 'A' && c <= 'Z') lower_romaji[i] = (char)(c - 'A' + 'a');
        else lower_romaji[i] = c;
    }
    lower_romaji[r_len] = '\0';
    romaji = lower_romaji;

    int r_idx = 0;
    int out_idx = 0;

    while (r_idx < r_len && out_idx < max_len - 4) {
        /* Check double consonant for sokuon 'っ' (e.g. "kk", "tt", "pp", "ss") */
        if (r_idx + 1 < r_len && romaji[r_idx] == romaji[r_idx + 1] &&
            romaji[r_idx] != 'a' && romaji[r_idx] != 'i' && romaji[r_idx] != 'u' &&
            romaji[r_idx] != 'e' && romaji[r_idx] != 'o' && romaji[r_idx] != 'n') {
            const char *sokuon = "っ";
            int slen = (int)strlen(sokuon);
            if (out_idx + slen < max_len) {
                memcpy(&out_hiragana[out_idx], sokuon, slen);
                out_idx += slen;
            }
            r_idx++;
            continue;
        }

        /* Check hatsuon 'ん': trailing 'n' at end of string, double 'nn' / 'n\'', or 'n' before consonants */
        if (romaji[r_idx] == 'n') {
            if (r_idx + 1 < r_len && (romaji[r_idx + 1] == 'n' || romaji[r_idx + 1] == '\'')) {
                const char *hatsuon = "ん";
                int hlen = (int)strlen(hatsuon);
                if (out_idx + hlen < max_len) {
                    memcpy(&out_hiragana[out_idx], hatsuon, hlen);
                    out_idx += hlen;
                }
                r_idx += 2;
                continue;
            } else if (r_idx + 1 == r_len) {
                /* Single 'n' at the very end of string converts to 'ん' */
                const char *hatsuon = "ん";
                int hlen = (int)strlen(hatsuon);
                if (out_idx + hlen < max_len) {
                    memcpy(&out_hiragana[out_idx], hatsuon, hlen);
                    out_idx += hlen;
                }
                r_idx++;
                continue;
            } else if (romaji[r_idx + 1] != 'a' && romaji[r_idx + 1] != 'i' && romaji[r_idx + 1] != 'u' &&
                       romaji[r_idx + 1] != 'e' && romaji[r_idx + 1] != 'o' && romaji[r_idx + 1] != 'y') {
                const char *hatsuon = "ん";
                int hlen = (int)strlen(hatsuon);
                if (out_idx + hlen < max_len) {
                    memcpy(&out_hiragana[out_idx], hatsuon, hlen);
                    out_idx += hlen;
                }
                r_idx++;
                continue;
            }
        }

        /* Match table */
        BOOL matched = FALSE;
        for (int i = 0; g_romaji_table[i].romaji != NULL; i++) {
            int tlen = (int)strlen(g_romaji_table[i].romaji);
            if (r_idx + tlen <= r_len && strncmp(&romaji[r_idx], g_romaji_table[i].romaji, tlen) == 0) {
                int hlen = (int)strlen(g_romaji_table[i].hiragana);
                if (out_idx + hlen < max_len) {
                    memcpy(&out_hiragana[out_idx], g_romaji_table[i].hiragana, hlen);
                    out_idx += hlen;
                }
                r_idx += tlen;
                matched = TRUE;
                break;
            }
        }

        if (!matched) {
            /* Pass through ASCII character */
            out_hiragana[out_idx++] = romaji[r_idx++];
        }
    }

    out_hiragana[out_idx] = '\0';
    return out_idx;
}

/* Transliterate Hiragana UTF-8 to Katakana UTF-8 (F7 key) */
int mozc_hiragana_to_katakana(const char *hiragana, char *out_katakana, int max_len) {
    if (!hiragana || !out_katakana || max_len <= 0) return 0;
    out_katakana[0] = '\0';

    const unsigned char *p = (const unsigned char *)hiragana;
    int out_idx = 0;

    while (*p && out_idx < max_len - 4) {
        if (p[0] == 0xE3 && (p[1] == 0x81 || p[1] == 0x82)) {
            UW cp = ((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
            if (cp >= 0x3041 && cp <= 0x3096) {
                UW kata_cp = cp + 0x60; /* Shift to Katakana block U+30A1..U+30F6 */
                out_katakana[out_idx++] = (char)(0xE0 | ((kata_cp >> 12) & 0x0F));
                out_katakana[out_idx++] = (char)(0x80 | ((kata_cp >> 6) & 0x3F));
                out_katakana[out_idx++] = (char)(0x80 | (kata_cp & 0x3F));
                p += 3;
                continue;
            }
        }
        out_katakana[out_idx++] = *p++;
    }
    out_katakana[out_idx] = '\0';
    return out_idx;
}

/* Transliterate Katakana UTF-8 to Hiragana UTF-8 (F6 key) */
int mozc_katakana_to_hiragana(const char *katakana, char *out_hiragana, int max_len) {
    if (!katakana || !out_hiragana || max_len <= 0) return 0;
    out_hiragana[0] = '\0';

    const unsigned char *p = (const unsigned char *)katakana;
    int out_idx = 0;

    while (*p && out_idx < max_len - 4) {
        if (p[0] == 0xE3 && (p[1] == 0x82 || p[1] == 0x83)) {
            UW cp = ((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
            if (cp >= 0x30A1 && cp <= 0x30F6) {
                UW hira_cp = cp - 0x60; /* Shift to Hiragana block U+3041..U+3096 */
                out_hiragana[out_idx++] = (char)(0xE0 | ((hira_cp >> 12) & 0x0F));
                out_hiragana[out_idx++] = (char)(0x80 | ((hira_cp >> 6) & 0x3F));
                out_hiragana[out_idx++] = (char)(0x80 | (hira_cp & 0x3F));
                p += 3;
                continue;
            }
        }
        out_hiragana[out_idx++] = *p++;
    }
    out_hiragana[out_idx] = '\0';
    return out_idx;
}

/* Transliterate Hiragana/Katakana UTF-8 to Halfwidth Katakana (F8 key) */
int mozc_hiragana_to_halfwidth_katakana(const char *hiragana, char *out_hkana, int max_len) {
    if (!hiragana || !out_hkana || max_len <= 0) return 0;
    out_hkana[0] = '\0';

    char kata_buf[256];
    mozc_hiragana_to_katakana(hiragana, kata_buf, sizeof(kata_buf));

    const unsigned char *p = (const unsigned char *)kata_buf;
    int out_idx = 0;

    while (*p && out_idx < max_len - 6) {
        if (p[0] == 0xE3) {
            UW cp = ((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
            p += 3;

            /* Map Katakana codepoints to Halfwidth Katakana UTF-8 bytes */
            const char *hk = NULL;
            switch (cp) {
                case 0x30A1: hk = "\xEF\xBD\xA7"; break; /* ｧ */
                case 0x30A2: hk = "\xEF\xBD\xB1"; break; /* ｱ */
                case 0x30A3: hk = "\xEF\xBD\xA8"; break; /* ｨ */
                case 0x30A4: hk = "\xEF\xBD\xB2"; break; /* ｲ */
                case 0x30A5: hk = "\xEF\xBD\xA9"; break; /* ｩ */
                case 0x30A6: hk = "\xEF\xBD\xB3"; break; /* ｳ */
                case 0x30A7: hk = "\xEF\xBD\xAA"; break; /* ｴ */
                case 0x30A8: hk = "\xEF\xBD\xB4"; break; /* ｴ */
                case 0x30A9: hk = "\xEF\xBD\xAB"; break; /* ｫ */
                case 0x30AA: hk = "\xEF\xBD\xB5"; break; /* ｵ */
                case 0x30AB: hk = "\xEF\xBD\xB6"; break; /* ｶ */
                case 0x30AC: hk = "\xEF\xBD\xB6\xEF\xBE\x9E"; break; /* ｶﾞ */
                case 0x30AD: hk = "\xEF\xBD\xB7"; break; /* ｷ */
                case 0x30AE: hk = "\xEF\xBD\xB7\xEF\xBE\x9E"; break; /* ｷﾞ */
                case 0x30AF: hk = "\xEF\xBD\xB8"; break; /* ｸ */
                case 0x30B0: hk = "\xEF\xBD\xB8\xEF\xBE\x9E"; break; /* ｸﾞ */
                case 0x30B1: hk = "\xEF\xBD\xB9"; break; /* ｹ */
                case 0x30B2: hk = "\xEF\xBD\xB9\xEF\xBE\x9E"; break; /* ｹﾞ */
                case 0x30B3: hk = "\xEF\xBD\xBA"; break; /* ｺ */
                case 0x30B4: hk = "\xEF\xBD\xBA\xEF\xBE\x9E"; break; /* ｺﾞ */
                case 0x30B5: hk = "\xEF\xBD\xBB"; break; /* ｻ */
                case 0x30B6: hk = "\xEF\xBD\xBB\xEF\xBE\x9E"; break; /* ｻﾞ */
                case 0x30B7: hk = "\xEF\xBD\xBC"; break; /* ｼ */
                case 0x30B8: hk = "\xEF\xBD\xBC\xEF\xBE\x9E"; break; /* ｼﾞ */
                case 0x30B9: hk = "\xEF\xBD\xBD"; break; /* ｽ */
                case 0x30BA: hk = "\xEF\xBD\xBD\xEF\xBE\x9E"; break; /* ｽﾞ */
                case 0x30BB: hk = "\xEF\xBD\xBE"; break; /* ｾ */
                case 0x30BC: hk = "\xEF\xBD\xBE\xEF\xBE\x9E"; break; /* ｾﾞ */
                case 0x30BD: hk = "\xEF\xBD\xBF"; break; /* ｿ */
                case 0x30BE: hk = "\xEF\xBD\xBF\xEF\xBE\x9E"; break; /* ｿﾞ */
                case 0x30BF: hk = "\xEF\xBE\x80"; break; /* ﾀ */
                case 0x30C0: hk = "\xEF\xBE\x80\xEF\xBE\x9E"; break; /* ﾀﾞ */
                case 0x30C1: hk = "\xEF\xBE\x81"; break; /* ﾁ */
                case 0x30C2: hk = "\xEF\xBE\x81\xEF\xBE\x9E"; break; /* ﾁﾞ */
                case 0x30C3: hk = "\xEF\xBD\xAF"; break; /* ｯ */
                case 0x30C4: hk = "\xEF\xBE\x82"; break; /* ﾂ */
                case 0x30C5: hk = "\xEF\xBE\x82\xEF\xBE\x9E"; break; /* ﾂﾞ */
                case 0x30C6: hk = "\xEF\xBE\x83"; break; /* ﾃ */
                case 0x30C7: hk = "\xEF\xBE\x83\xEF\xBE\x9E"; break; /* ﾃﾞ */
                case 0x30C8: hk = "\xEF\xBE\x84"; break; /* ﾄ */
                case 0x30C9: hk = "\xEF\xBE\x84\xEF\xBE\x9E"; break; /* ﾄﾞ */
                case 0x30CA: hk = "\xEF\xBE\x85"; break; /* ﾅ */
                case 0x30CB: hk = "\xEF\xBE\x86"; break; /* ﾆ */
                case 0x30CC: hk = "\xEF\xBE\x87"; break; /* ﾇ */
                case 0x30CD: hk = "\xEF\xBE\x88"; break; /* ﾈ */
                case 0x30CE: hk = "\xEF\xBE\x89"; break; /* ﾉ */
                case 0x30CF: hk = "\xEF\xBE\x8A"; break; /* ﾊ */
                case 0x30D0: hk = "\xEF\xBE\x8A\xEF\xBE\x9E"; break; /* ﾊﾞ */
                case 0x30D1: hk = "\xEF\xBE\x8A\xEF\xBE\x9F"; break; /* ﾊﾟ */
                case 0x30D2: hk = "\xEF\xBE\x8B"; break; /* ﾋ */
                case 0x30D3: hk = "\xEF\xBE\x8B\xEF\xBE\x9E"; break; /* ﾋﾞ */
                case 0x30D4: hk = "\xEF\xBE\x8B\xEF\xBE\x9F"; break; /* ﾋﾟ */
                case 0x30D5: hk = "\xEF\xBE\x8C"; break; /* ﾌ */
                case 0x30D6: hk = "\xEF\xBE\x8C\xEF\xBE\x9E"; break; /* ﾌﾞ */
                case 0x30D7: hk = "\xEF\xBE\x8C\xEF\xBE\x9F"; break; /* ﾌﾟ */
                case 0x30D8: hk = "\xEF\xBE\x8D"; break; /* ﾍ */
                case 0x30D9: hk = "\xEF\xBE\x8D\xEF\xBE\x9E"; break; /* ﾍﾞ */
                case 0x30DA: hk = "\xEF\xBE\x8D\xEF\xBE\x9F"; break; /* ﾍﾟ */
                case 0x30DB: hk = "\xEF\xBE\x8E"; break; /* ﾎ */
                case 0x30DC: hk = "\xEF\xBE\x8E\xEF\xBE\x9E"; break; /* ﾎﾞ */
                case 0x30DD: hk = "\xEF\xBE\x8E\xEF\xBE\x9F"; break; /* ﾎﾟ */
                case 0x30DE: hk = "\xEF\xBE\x8F"; break; /* ﾏ */
                case 0x30DF: hk = "\xEF\xBE\x90"; break; /* ﾐ */
                case 0x30E0: hk = "\xEF\xBE\x91"; break; /* ﾑ */
                case 0x30E1: hk = "\xEF\xBE\x92"; break; /* ﾒ */
                case 0x30E2: hk = "\xEF\xBE\x93"; break; /* ﾓ */
                case 0x30E3: hk = "\xEF\xBD\xAC"; break; /* ｬ */
                case 0x30E4: hk = "\xEF\xBE\x94"; break; /* ﾔ */
                case 0x30E5: hk = "\xEF\xBD\xAD"; break; /* ｭ */
                case 0x30E6: hk = "\xEF\xBE\x95"; break; /* ﾕ */
                case 0x30E7: hk = "\xEF\xBD\xAE"; break; /* ｮ */
                case 0x30E8: hk = "\xEF\xBE\x96"; break; /* ﾖ */
                case 0x30E9: hk = "\xEF\xBE\x97"; break; /* ﾗ */
                case 0x30EA: hk = "\xEF\xBE\x98"; break; /* ﾘ */
                case 0x30EB: hk = "\xEF\xBE\x99"; break; /* ﾙ */
                case 0x30EC: hk = "\xEF\xBE\x9A"; break; /* ﾚ */
                case 0x30ED: hk = "\xEF\xBE\x9B"; break; /* ﾛ */
                case 0x30EF: hk = "\xEF\xBE\x9C"; break; /* ﾜ */
                case 0x30F2: hk = "\xEF\xBD\xA6"; break; /* ｦ */
                case 0x30F3: hk = "\xEF\xBE\x9D"; break; /* ﾝ */
                case 0x30FC: hk = "\xEF\xBD\xB0"; break; /* ｰ */
                default: break;
            }
            if (hk) {
                int hlen = (int)strlen(hk);
                if (out_idx + hlen < max_len) {
                    memcpy(&out_hkana[out_idx], hk, hlen);
                    out_idx += hlen;
                }
                continue;
            }
        }
        out_hkana[out_idx++] = *p++;
    }
    out_hkana[out_idx] = '\0';
    return out_idx;
}

/* Transliterate Alphanumeric/ASCII to Fullwidth Alphanumeric (F9 key) */
int mozc_alphanumeric_to_fullwidth(const char *ascii, char *out_fullwidth, int max_len) {
    if (!ascii || !out_fullwidth || max_len <= 0) return 0;
    out_fullwidth[0] = '\0';

    const unsigned char *p = (const unsigned char *)ascii;
    int out_idx = 0;

    while (*p && out_idx < max_len - 4) {
        unsigned char c = *p++;
        if (c == ' ') {
            /* Fullwidth ideographic space U+3000 */
            out_fullwidth[out_idx++] = (char)0xE3;
            out_fullwidth[out_idx++] = (char)0x80;
            out_fullwidth[out_idx++] = (char)0x80;
        } else if (c >= 0x21 && c <= 0x7E) {
            UW full_cp = 0xFF01 + (c - 0x21); /* U+FF01..U+FF5E */
            out_fullwidth[out_idx++] = (char)(0xE0 | ((full_cp >> 12) & 0x0F));
            out_fullwidth[out_idx++] = (char)(0x80 | ((full_cp >> 6) & 0x3F));
            out_fullwidth[out_idx++] = (char)(0x80 | (full_cp & 0x3F));
        } else {
            out_fullwidth[out_idx++] = (char)c;
        }
    }
    out_fullwidth[out_idx] = '\0';
    return out_idx;
}

/* ── System Dictionary Vocabulary Data ── */
typedef struct {
    const char *reading;
    const char *value;
    const char *annotation;
    MOZC_POS pos;
    int cost;
} RawDictEntry;

static const RawDictEntry g_system_dictionary[] = {
    /* Pronouns & Common Words */
    {"わたし", "私", "pronoun", POS_NOUN, 1200},
    {"わたし", "わたし", "hiragana", POS_NOUN, 1500},
    {"わたくし", "私", "formal", POS_NOUN, 1300},
    {"ぼく", "僕", "pronoun", POS_NOUN, 1200},
    {"おれ", "俺", "pronoun", POS_NOUN, 1400},
    {"あなた", "貴方", "pronoun", POS_NOUN, 1300},
    {"かれ", "彼", "pronoun", POS_NOUN, 1200},
    {"かのじょ", "彼女", "pronoun", POS_NOUN, 1200},

    /* Particles & Auxiliaries */
    {"の", "の", "particle", POS_PARTICLE, 400},
    {"は", "は", "topic particle", POS_PARTICLE, 400},
    {"わ", "は", "topic particle", POS_PARTICLE, 450},
    {"が", "が", "subject particle", POS_PARTICLE, 450},
    {"を", "を", "object particle", POS_PARTICLE, 450},
    {"に", "に", "locative particle", POS_PARTICLE, 450},
    {"へ", "へ", "directional particle", POS_PARTICLE, 500},
    {"で", "で", "instrumental particle", POS_PARTICLE, 450},
    {"と", "と", "conjunctive particle", POS_PARTICLE, 450},
    {"も", "も", "inclusive particle", POS_PARTICLE, 450},
    {"から", "から", "source particle", POS_PARTICLE, 500},
    {"まで", "まで", "limit particle", POS_PARTICLE, 500},
    {"より", "より", "comparison particle", POS_PARTICLE, 550},
    {"です", "です", "polite copula", POS_AUX_VERB, 500},
    {"だ", "だ", "plain copula", POS_AUX_VERB, 500},
    {"ます", "ます", "polite suffix", POS_AUX_VERB, 500},
    {"でした", "でした", "past copula", POS_AUX_VERB, 600},

    /* Nouns */
    {"なまえ", "名前", "noun", POS_NOUN, 1000},
    {"なまえ", "なまえ", "hiragana", POS_NOUN, 1400},
    {"きょう", "今日", "time noun", POS_NOUN, 800},
    {"きょう", "教", "kanji", POS_NOUN, 2000},
    {"あした", "明日", "time noun", POS_NOUN, 900},
    {"きのう", "昨日", "time noun", POS_NOUN, 900},
    {"いま", "今", "time noun", POS_NOUN, 800},
    {"ほん", "本", "noun", POS_NOUN, 900},
    {"にほん", "日本", "proper noun", POS_PROPER_NOUN, 700},
    {"にほんご", "日本語", "language", POS_NOUN, 750},
    {"えいご", "英語", "language", POS_NOUN, 850},
    {"がっこう", "学校", "noun", POS_NOUN, 900},
    {"だいがく", "大学", "noun", POS_NOUN, 900},
    {"かいしゃ", "会社", "noun", POS_NOUN, 900},
    {"しごと", "仕事", "noun", POS_NOUN, 950},
    {"せんせい", "先生", "honorific noun", POS_NOUN, 900},
    {"がくせい", "学生", "noun", POS_NOUN, 950},
    {"ともだち", "友達", "noun", POS_NOUN, 1000},
    {"かぞく", "家族", "noun", POS_NOUN, 1000},
    {"てんき", "天気", "noun", POS_NOUN, 1000},
    {"みず", "水", "noun", POS_NOUN, 900},
    {"ごはん", "ご飯", "noun", POS_NOUN, 950},
    {"くるま", "車", "noun", POS_NOUN, 950},
    {"でんしゃ", "電車", "noun", POS_NOUN, 950},
    {"えき", "駅", "noun", POS_NOUN, 900},

    /* Specific BTRON / Sakamura / Nakano names (from btron-tip.tex wireframes) */
    {"なかの", "中野", "surname", POS_PROPER_NOUN, 800},
    {"なかの", "仲野", "alt kanji", POS_PROPER_NOUN, 1200},
    {"なかの", "なかの", "hiragana", POS_NOUN, 1500},
    {"なかの", "ナカノ", "katakana", POS_NOUN, 1600},
    {"なかの", "中埜", "rare kanji", POS_PROPER_NOUN, 2200},

    {"さかむら", "坂村", "surname", POS_PROPER_NOUN, 800},
    {"さかむら", "酒村", "alt kanji", POS_PROPER_NOUN, 2500},
    {"けん", "健", "given name", POS_PROPER_NOUN, 900},
    {"けん", "研", "research", POS_NOUN, 1500},
    {"けん", "県", "prefecture", POS_NOUN, 1400},
    {"けん", "件", "matter", POS_NOUN, 1400},

    {"とろん", "TRON", "tech", POS_NOUN, 600},
    {"びーとろん", "B-TRON", "system", POS_NOUN, 500},
    {"てぃーかーねる", "T-Kernel", "kernel", POS_NOUN, 500},
    {"もずく", "Mozc", "engine", POS_NOUN, 600},
    {"じっしん", "実身", "Real Body", POS_NOUN, 800},
    {"かしん", "仮身", "Virtual Body", POS_NOUN, 800},

    /* Buddhist & Cultural terms */
    {"ほとけ", "仏", "buddha", POS_NOUN, 900},
    {"ほとけ", "ほとけ", "hiragana", POS_NOUN, 1300},
    {"ほとけさん", "仏さん", "buddha polite", POS_NOUN, 800},
    {"ほとけさん", "ほとけさん", "hiragana", POS_NOUN, 1200},
    {"ほとけさま", "仏様", "buddha honorific", POS_NOUN, 850},
    {"さん", "さん", "suffix/honorific", POS_SUFFIX, 400},
    {"さま", "様", "suffix/honorific", POS_SUFFIX, 400},

    /* Verbs & Adjectives */
    {"いく", "行く", "verb", POS_VERB, 900},
    {"くる", "来る", "verb", POS_VERB, 900},
    {"たべる", "食べる", "verb", POS_VERB, 900},
    {"のむ", "飲む", "verb", POS_VERB, 950},
    {"みる", "見る", "verb", POS_VERB, 900},
    {"きく", "聞く", "verb", POS_VERB, 950},
    {"はなす", "話す", "verb", POS_VERB, 950},
    {"かく", "書く", "verb", POS_VERB, 950},
    {"よむ", "読む", "verb", POS_VERB, 950},
    {"する", "する", "verb", POS_VERB, 700},

    {"いい", "良い", "adjective", POS_ADJECTIVE, 900},
    {"よい", "良い", "adjective", POS_ADJECTIVE, 900},
    {"わるい", "悪い", "adjective", POS_ADJECTIVE, 1000},
    {"おおきい", "大きい", "adjective", POS_ADJECTIVE, 950},
    {"ちいさい", "小さい", "adjective", POS_ADJECTIVE, 950},
    {"あたらしい", "新しい", "adjective", POS_ADJECTIVE, 950},
    {"ふるい", "古い", "adjective", POS_ADJECTIVE, 1000},
    {"たかい", "高い", "adjective", POS_ADJECTIVE, 950},
    {"やすい", "安い", "adjective", POS_ADJECTIVE, 950},

    /* Greetings */
    {"こんにちは", "こんにちは", "greeting", POS_NOUN, 700},
    {"おはよう", "おはよう", "greeting", POS_NOUN, 700},
    {"こんばんは", "こんばんは", "greeting", POS_NOUN, 700},
    {"ありがとう", "ありがとう", "greeting", POS_NOUN, 700},

    {NULL, NULL, NULL, POS_COUNT, 0}
};

/* Dynamic User Dictionary (Real Body backing) */
#define MAX_USER_ENTRIES 128
static MozcEntry g_user_dictionary[MAX_USER_ENTRIES];
static int g_num_user_entries = 0;

/* Bigram Transition Cost Matrix: TransCost(pos1, pos2) */
static const int g_pos_transition_costs[POS_COUNT][POS_COUNT] = {
    /* BOS */       {0, 200, 200, 400, 300, 9999, 9999, 500, 800, 9999},
    /* NOUN */      {9999, 300, 400, 350, 400, 100, 200, 400, 600, 400},
    /* PROPER */    {9999, 300, 400, 350, 400, 100, 200, 400, 600, 400},
    /* VERB */      {9999, 400, 500, 600, 600, 300, 150, 300, 500, 200},
    /* ADJECTIVE */ {9999, 200, 300, 500, 500, 300, 200, 400, 500, 200},
    /* PARTICLE */  {9999, 200, 200, 200, 250, 300, 250, 400, 600, 500},
    /* AUX_VERB */  {9999, 600, 700, 700, 800, 400, 300, 600, 400, 100},
    /* SUFFIX */    {9999, 300, 400, 400, 400, 200, 300, 500, 600, 300},
    /* SYMBOL */    {9999, 300, 300, 400, 400, 400, 500, 500, 500, 200},
    /* EOS */       {0, 9999, 9999, 9999, 9999, 9999, 9999, 9999, 9999, 0}
};

static int get_transition_cost(MOZC_POS p1, MOZC_POS p2) {
    if (p1 >= POS_COUNT || p2 >= POS_COUNT) return 1000;
    return g_pos_transition_costs[p1][p2];
}

ER mozc_engine_init(void) {
    g_num_user_entries = 0;
    return E_OK;
}

ER mozc_engine_cleanup(void) {
    g_num_user_entries = 0;
    return E_OK;
}

ER mozc_register_user_word(const char *reading, const char *value, const char *annotation) {
    if (!reading || !value || g_num_user_entries >= MAX_USER_ENTRIES) return E_PAR;

    for (int i = 0; i < g_num_user_entries; i++) {
        if (strcmp(g_user_dictionary[i].reading, reading) == 0 &&
            strcmp(g_user_dictionary[i].value, value) == 0) {
            /* Update cost / boost frequency */
            g_user_dictionary[i].unigram_cost = 400;
            return E_OK;
        }
    }

    MozcEntry *entry = &g_user_dictionary[g_num_user_entries++];
    strncpy(entry->reading, reading, MOZC_MAX_WORD_LEN - 1);
    strncpy(entry->value, value, MOZC_MAX_WORD_LEN - 1);
    strncpy(entry->annotation, annotation ? annotation : "user", MOZC_MAX_WORD_LEN - 1);
    entry->pos = POS_NOUN;
    entry->unigram_cost = 500;
    entry->next = NULL;

    return E_OK;
}

int mozc_get_candidates(const char *bunsetsu_reading, TIP_CANDIDATE *candidates, int max_candidates) {
    if (!bunsetsu_reading || !candidates || max_candidates <= 0) return 0;
    int count = 0;

    /* 1. Check User Dictionary first */
    for (int i = 0; i < g_num_user_entries && count < max_candidates; i++) {
        if (strcmp(g_user_dictionary[i].reading, bunsetsu_reading) == 0) {
            strncpy(candidates[count].value, g_user_dictionary[i].value, TIP_MAX_STR_LEN - 1);
            strncpy(candidates[count].annotation, g_user_dictionary[i].annotation, TIP_MAX_STR_LEN - 1);
            candidates[count].cost = g_user_dictionary[i].unigram_cost;
            count++;
        }
    }

    /* 2. Check System Dictionary */
    for (int i = 0; g_system_dictionary[i].reading != NULL && count < max_candidates; i++) {
        if (strcmp(g_system_dictionary[i].reading, bunsetsu_reading) == 0) {
            /* Avoid duplicate values */
            BOOL duplicate = FALSE;
            for (int k = 0; k < count; k++) {
                if (strcmp(candidates[k].value, g_system_dictionary[i].value) == 0) {
                    duplicate = TRUE;
                    break;
                }
            }
            if (!duplicate) {
                strncpy(candidates[count].value, g_system_dictionary[i].value, TIP_MAX_STR_LEN - 1);
                strncpy(candidates[count].annotation, g_system_dictionary[i].annotation, TIP_MAX_STR_LEN - 1);
                candidates[count].cost = g_system_dictionary[i].cost;
                count++;
            }
        }
    }

    /* 3. Hiragana fallback */
    if (count < max_candidates) {
        BOOL has_hira = FALSE;
        for (int k = 0; k < count; k++) {
            if (strcmp(candidates[k].value, bunsetsu_reading) == 0) {
                has_hira = TRUE;
                break;
            }
        }
        if (!has_hira) {
            strncpy(candidates[count].value, bunsetsu_reading, TIP_MAX_STR_LEN - 1);
            strncpy(candidates[count].annotation, "hiragana", TIP_MAX_STR_LEN - 1);
            candidates[count].cost = 3000;
            count++;
        }
    }

    /* 4. Katakana fallback */
    if (count < max_candidates) {
        char kata_buf[TIP_MAX_STR_LEN];
        mozc_hiragana_to_katakana(bunsetsu_reading, kata_buf, sizeof(kata_buf));
        BOOL has_kata = FALSE;
        for (int k = 0; k < count; k++) {
            if (strcmp(candidates[k].value, kata_buf) == 0) {
                has_kata = TRUE;
                break;
            }
        }
        if (!has_kata) {
            strncpy(candidates[count].value, kata_buf, TIP_MAX_STR_LEN - 1);
            strncpy(candidates[count].annotation, "katakana", TIP_MAX_STR_LEN - 1);
            candidates[count].cost = 3200;
            count++;
        }
    }

    return count;
}

/*
 * Morphological Bunsetsu Segmentation & True Viterbi Lattice Search
 * Minimizes Cost(Path) = sum [ WordCost(w_i) + TransCost(POS_{i-1} -> POS_i) ]
 */
typedef struct {
    int cost;
    int prev_pos;
    MOZC_POS pos;
} ViterbiLatticeDP;

ER mozc_lattice_search(const char *reading_utf8, TIP_CLAUSE *clauses, int *num_clauses, int max_clauses) {
    if (!reading_utf8 || !clauses || !num_clauses || max_clauses <= 0) return E_PAR;

    *num_clauses = 0;
    if (reading_utf8[0] == '\0') return E_OK;

    int r_len = (int)strlen(reading_utf8);
    if (r_len > 256) r_len = 256;

    ViterbiLatticeDP dp[256 + 1];
    for (int i = 0; i <= r_len; i++) {
        dp[i].cost = 9999999;
        dp[i].prev_pos = -1;
        dp[i].pos = POS_BOS;
    }
    dp[0].cost = 0;
    dp[0].pos = POS_BOS;

    /* Forward Viterbi DP over byte positions */
    for (int i = 0; i < r_len; i++) {
        if (dp[i].cost >= 9999999) continue;

        BOOL has_dict_match = FALSE;

        /* Scan user dictionary matches at position i */
        for (int u = 0; u < g_num_user_entries; u++) {
            int wlen = (int)strlen(g_user_dictionary[u].reading);
            if (i + wlen <= r_len &&
                strncmp(&reading_utf8[i], g_user_dictionary[u].reading, wlen) == 0) {
                int trans_cost = get_transition_cost(dp[i].pos, g_user_dictionary[u].pos);
                int total_cost = dp[i].cost + g_user_dictionary[u].unigram_cost + trans_cost;
                if (total_cost < dp[i + wlen].cost) {
                    dp[i + wlen].cost = total_cost;
                    dp[i + wlen].prev_pos = i;
                    dp[i + wlen].pos = g_user_dictionary[u].pos;
                }
                has_dict_match = TRUE;
            }
        }

        /* Scan system dictionary matches at position i */
        for (int s = 0; g_system_dictionary[s].reading != NULL; s++) {
            int wlen = (int)strlen(g_system_dictionary[s].reading);
            if (i + wlen <= r_len &&
                strncmp(&reading_utf8[i], g_system_dictionary[s].reading, wlen) == 0) {
                int trans_cost = get_transition_cost(dp[i].pos, g_system_dictionary[s].pos);
                int total_cost = dp[i].cost + g_system_dictionary[s].cost + trans_cost;
                if (total_cost < dp[i + wlen].cost) {
                    dp[i + wlen].cost = total_cost;
                    dp[i + wlen].prev_pos = i;
                    dp[i + wlen].pos = g_system_dictionary[s].pos;
                }
                has_dict_match = TRUE;
            }
        }

        /* Fallback single UTF-8 character transition */
        int step = 1;
        unsigned char c = (unsigned char)reading_utf8[i];
        if ((c & 0x80) == 0) step = 1;
        else if ((c & 0xE0) == 0xC0) step = 2;
        else if ((c & 0xF0) == 0xE0) step = 3;
        else if ((c & 0xF8) == 0xF0) step = 4;

        if (i + step <= r_len) {
            int trans_cost = get_transition_cost(dp[i].pos, POS_NOUN);
            int fallback_cost = dp[i].cost + (has_dict_match ? 3500 : 2000) + trans_cost;
            if (fallback_cost < dp[i + step].cost) {
                dp[i + step].cost = fallback_cost;
                dp[i + step].prev_pos = i;
                dp[i + step].pos = POS_NOUN;
            }
        }
    }

    if (dp[r_len].prev_pos == -1) {
        /* Fallback: single clause for entire reading */
        TIP_CLAUSE *c = &clauses[0];
        memset(c, 0, sizeof(TIP_CLAUSE));
        strncpy(c->reading, reading_utf8, TIP_MAX_STR_LEN - 1);
        c->num_candidates = mozc_get_candidates(c->reading, c->candidates, TIP_MAX_CANDIDATES);
        c->selected_candidate = 0;
        if (c->num_candidates > 0) strncpy(c->converted, c->candidates[0].value, TIP_MAX_STR_LEN - 1);
        else strncpy(c->converted, c->reading, TIP_MAX_STR_LEN - 1);
        *num_clauses = 1;
        return E_OK;
    }

    /* Backtrace optimal Viterbi path from r_len to 0 */
    int bounds[TIP_MAX_CLAUSES + 1];
    int num_bounds = 0;
    int curr = r_len;

    while (curr > 0 && num_bounds < TIP_MAX_CLAUSES) {
        bounds[num_bounds++] = curr;
        curr = dp[curr].prev_pos;
    }
    bounds[num_bounds++] = 0;

    /* Populate clauses in forward order */
    int clause_count = 0;
    for (int k = num_bounds - 1; k > 0 && clause_count < max_clauses; k--) {
        int start = bounds[k];
        int end = bounds[k - 1];
        int clen = end - start;

        TIP_CLAUSE *clause = &clauses[clause_count];
        memset(clause, 0, sizeof(TIP_CLAUSE));

        if (clen >= TIP_MAX_STR_LEN) clen = TIP_MAX_STR_LEN - 1;
        memcpy(clause->reading, &reading_utf8[start], clen);
        clause->reading[clen] = '\0';

        clause->num_candidates = mozc_get_candidates(clause->reading, clause->candidates, TIP_MAX_CANDIDATES);
        clause->selected_candidate = 0;
        if (clause->num_candidates > 0) {
            strncpy(clause->converted, clause->candidates[0].value, TIP_MAX_STR_LEN - 1);
        } else {
            strncpy(clause->converted, clause->reading, TIP_MAX_STR_LEN - 1);
        }
        clause_count++;
    }

    *num_clauses = clause_count;
    return E_OK;
}
