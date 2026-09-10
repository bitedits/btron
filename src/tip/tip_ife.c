/*
 * B-System (BTRON 3.20) Text Input Primitives (TIP) - Input Front-End (IFE): tip_ife.c
 * Cleanroom implementation conforming to btron-tip.tex Section 3 & 4.
 */

#include <btron/tip.h>
#include <btron/wylie.h>
#include <btron/tibetan_dict.h>
#include <btron/event.h>
#include <btron/mozc_engine.h>
#include <btron/troncode.h>
#include <btron/wnd.h>

#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#else
#include <stddef.h>
#include <stdint.h>
#include <libstr.h>
#define strlen   tkl_strlen
#define strcmp   tkl_strcmp
#define strncpy  tkl_strncpy
#define memcpy   tkl_memcpy
#define memset   tkl_memset
static inline int snprintf(char *str, size_t size, const char *format, ...) {
    (void)format;
    if (size > 0) str[0] = '\0';
    return 0;
}
#endif


static TIP_KEY_SETTINGS g_tip_key_settings = {
    .mode_toggle_key      = BTRON_KEY_F10,
    .direct_en_key        = 0,
    .direct_jp_hira_key   = BTRON_KEY_F6,
    .direct_jp_kata_key   = BTRON_KEY_F7,
    .direct_tb_key        = BTRON_KEY_F8,
    .jp_space_is_convert  = TRUE,
    .jp_tab_is_popup      = TRUE,
    .tb_space_is_tsheg    = TRUE,
    .tb_tab_is_popup      = TRUE,
    .tb_shift_space_popup = TRUE,
    .arrow_nav_enabled    = TRUE,
    .num_select_enabled   = TRUE
};

void tip_get_key_settings(TIP_KEY_SETTINGS *out_settings) {
    if (!out_settings) return;
    *out_settings = g_tip_key_settings;
}

void tip_set_key_settings(const TIP_KEY_SETTINGS *settings) {
    if (!settings) return;
    g_tip_key_settings = *settings;
}

void tip_reset_default_key_settings(void) {
    g_tip_key_settings.mode_toggle_key      = BTRON_KEY_F10;
    g_tip_key_settings.direct_en_key        = 0;
    g_tip_key_settings.direct_jp_hira_key   = BTRON_KEY_F6;
    g_tip_key_settings.direct_jp_kata_key   = BTRON_KEY_F7;
    g_tip_key_settings.direct_tb_key        = BTRON_KEY_F8;
    g_tip_key_settings.jp_space_is_convert  = TRUE;
    g_tip_key_settings.jp_tab_is_popup      = TRUE;
    g_tip_key_settings.tb_space_is_tsheg    = TRUE;
    g_tip_key_settings.tb_tab_is_popup      = TRUE;
    g_tip_key_settings.tb_shift_space_popup = TRUE;
    g_tip_key_settings.arrow_nav_enabled    = TRUE;
    g_tip_key_settings.num_select_enabled   = TRUE;
}

static TIP_CONTEXT g_tip;

ER tip_init(void) {
    memset(&g_tip, 0, sizeof(TIP_CONTEXT));
    g_tip.mode = TIP_MODE_HIRAGANA; /* Japanese IME active by default */
    g_tip.state = TIP_STATE_IDLE;
    mozc_engine_init();
    return E_OK;
}

TIP_DFA_STATE tip_get_state(void) {
    return g_tip.state;
}

TIP_INPUT_MODE tip_get_mode(void) {
    return g_tip.mode;
}

const char* tip_get_mode_str(void) {
    switch (g_tip.mode) {
        case TIP_MODE_HIRAGANA: return "[JP あ]";
        case TIP_MODE_KATAKANA: return "[JP ア]";
        case TIP_MODE_TIBETAN: return "[TB བོད]";
        case TIP_MODE_ASCII:
        default: return "[EN]";
    }
}


void tip_set_mode(TIP_INPUT_MODE mode) {
    g_tip.mode = mode;
    if (g_tip.state != TIP_STATE_IDLE) {
        tip_cancel();
    }
}

void tip_toggle_mode(void) {
    if (g_tip.mode == TIP_MODE_ASCII) {
        g_tip.mode = TIP_MODE_HIRAGANA;
    } else if (g_tip.mode == TIP_MODE_HIRAGANA) {
        g_tip.mode = TIP_MODE_KATAKANA;
    } else if (g_tip.mode == TIP_MODE_KATAKANA) {
        g_tip.mode = TIP_MODE_TIBETAN;
    } else {
        g_tip.mode = TIP_MODE_ASCII;
    }
    tip_cancel();
}

BOOL tip_is_active(void) {
    return (g_tip.mode != TIP_MODE_ASCII && g_tip.state != TIP_STATE_IDLE);
}

/*
 * Theorem 2 (Composition Safety and Non-Latching):
 * Cancellation unconditionally restores DFA to TIP_STATE_IDLE in O(1).
 */
void tip_cancel(void) {
    g_tip.state = TIP_STATE_IDLE;
    g_tip.romaji_buf[0] = '\0';
    g_tip.romaji_len = 0;
    g_tip.reading_buf[0] = '\0';
    g_tip.reading_len = 0;
    g_tip.num_clauses = 0;
    g_tip.active_clause = 0;
    g_tip.candidate_window_visible = FALSE;
}

void tip_set_caret_pos(H x, H y) {
    g_tip.caret_pos.x = x;
    g_tip.caret_pos.y = y;
}

const char* tip_get_reading(void) {
    return g_tip.reading_buf;
}

const char* tip_get_converted_text(char *out_buf, int max_len) {
    if (!out_buf || max_len <= 0) return "";
    out_buf[0] = '\0';

    if (g_tip.state == TIP_STATE_IDLE) {
        return "";
    }

    if (g_tip.state == TIP_STATE_PRECOMP) {
        if (g_tip.mode == TIP_MODE_KATAKANA) {
            mozc_hiragana_to_katakana(g_tip.reading_buf, out_buf, max_len);
            return out_buf;
        }
        if (g_tip.mode == TIP_MODE_TIBETAN) {
            wylie_to_tibetan(g_tip.romaji_buf, out_buf, (size_t)max_len);
            return out_buf;
        }
        strncpy(out_buf, g_tip.reading_buf, max_len - 1);
        return out_buf;
    }

    /* Assembled converted bunsetsu clauses */
    int out_idx = 0;
    for (int i = 0; i < g_tip.num_clauses && out_idx < max_len - 1; i++) {
        const char *txt = g_tip.clauses[i].converted;
        int len = (int)strlen(txt);
        if (out_idx + len < max_len - 1) {
            memcpy(&out_buf[out_idx], txt, len);
            out_idx += len;
        }
    }
    out_buf[out_idx] = '\0';
    return out_buf;
}

int tip_get_composition_length(void) {
    char buf[TIP_MAX_PRECOMP_LEN];
    tip_get_converted_text(buf, sizeof(buf));
    return (int)strlen(buf);
}

/*
 * Event Processor: Dispatches keyboard events through DFA state machine
 */
BOOL tip_process_key(UW key, UW modifiers, char *out_commit, int max_commit) {
    if (out_commit) out_commit[0] = '\0';

    /* Mode Toggle: Ctrl+Space, Shift+Space, Alt+Space, Hankaku/Zenkaku, or F10 */
    if ((key == ' ' && (modifiers & 0x03C3)) || /* Any Ctrl, Shift, or Alt modifier on Space */
        key == 0x100 ||                          /* Hankaku / Zenkaku key */
        key == BTRON_KEY_F10) {                  /* F10 (0x40000043): Standard Mode Toggle */
        tip_toggle_mode();
        return TRUE;
    }

    /* Functional Key Transformations during composition */
    if (g_tip.state != TIP_STATE_IDLE) {
        if (key == BTRON_KEY_F6) {
            /* F6: Convert active composition to Hiragana */
            if (g_tip.state == TIP_STATE_PRECOMP) {
                mozc_lattice_search(g_tip.reading_buf, g_tip.clauses, &g_tip.num_clauses, TIP_MAX_CLAUSES);
                g_tip.state = TIP_STATE_CONVERTING;
            }
            for (int i = 0; i < g_tip.num_clauses; i++) {
                strncpy(g_tip.clauses[i].converted, g_tip.clauses[i].reading, TIP_MAX_STR_LEN - 1);
            }
            return TRUE;
        }
        if (key == BTRON_KEY_F7) {
            /* F7: Convert active composition to Fullwidth Katakana */
            if (g_tip.state == TIP_STATE_PRECOMP) {
                mozc_lattice_search(g_tip.reading_buf, g_tip.clauses, &g_tip.num_clauses, TIP_MAX_CLAUSES);
                g_tip.state = TIP_STATE_CONVERTING;
            }
            for (int i = 0; i < g_tip.num_clauses; i++) {
                mozc_hiragana_to_katakana(g_tip.clauses[i].reading, g_tip.clauses[i].converted, TIP_MAX_STR_LEN);
            }
            return TRUE;
        }
        if (key == BTRON_KEY_F8) {
            /* F8: Convert active composition to Halfwidth Katakana */
            if (g_tip.state == TIP_STATE_PRECOMP) {
                mozc_lattice_search(g_tip.reading_buf, g_tip.clauses, &g_tip.num_clauses, TIP_MAX_CLAUSES);
                g_tip.state = TIP_STATE_CONVERTING;
            }
            for (int i = 0; i < g_tip.num_clauses; i++) {
                mozc_hiragana_to_halfwidth_katakana(g_tip.clauses[i].reading, g_tip.clauses[i].converted, TIP_MAX_STR_LEN);
            }
            return TRUE;
        }
        if (key == BTRON_KEY_F9) {
            /* F9: Convert active composition to Fullwidth Alphanumeric */
            g_tip.state = TIP_STATE_CONVERTING;
            g_tip.num_clauses = 1;
            g_tip.active_clause = 0;
            strncpy(g_tip.clauses[0].reading, g_tip.reading_buf, TIP_MAX_STR_LEN - 1);
            mozc_alphanumeric_to_fullwidth(g_tip.romaji_buf, g_tip.clauses[0].converted, TIP_MAX_STR_LEN);
            return TRUE;
        }
    } else {
        /* In IDLE mode, F6 / F7 switches global input mode */
        if (key == BTRON_KEY_F6) {
            tip_set_mode(TIP_MODE_HIRAGANA);
            return TRUE;
        }
        if (key == BTRON_KEY_F7) {
            tip_set_mode(TIP_MODE_KATAKANA);
            return TRUE;
        }
    }

    if (g_tip.mode == TIP_MODE_ASCII) {
        return FALSE; /* Not handled by TIP in direct ASCII mode */
    }

    /* Theorem 2: ESC key restores to IDLE in O(1) */
    if (key == 0x1B /* ESC */) {
        if (g_tip.state != TIP_STATE_IDLE) {
            tip_cancel();
            return TRUE;
        }
        return FALSE;
    }

    /* Backspace handling */
    if (key == 0x08 /* Backspace */) {
        if (g_tip.state == TIP_STATE_CANDIDATE_SELECT) {
            g_tip.state = TIP_STATE_CONVERTING;
            g_tip.candidate_window_visible = FALSE;
            return TRUE;
        }
        if (g_tip.state == TIP_STATE_CONVERTING) {
            g_tip.state = TIP_STATE_PRECOMP;
            g_tip.candidate_window_visible = FALSE;
            return TRUE;
        }
        if (g_tip.state == TIP_STATE_PRECOMP) {
            if (g_tip.romaji_len > 0) {
                g_tip.romaji_len--;
                g_tip.romaji_buf[g_tip.romaji_len] = '\0';
                if (g_tip.mode == TIP_MODE_TIBETAN) {
                    wylie_to_tibetan(g_tip.romaji_buf, g_tip.reading_buf, sizeof(g_tip.reading_buf));
                } else {
                    mozc_romaji_to_hiragana(g_tip.romaji_buf, g_tip.reading_buf, sizeof(g_tip.reading_buf));
                }
                g_tip.reading_len = (int)strlen(g_tip.reading_buf);

                if (g_tip.romaji_len == 0) {
                    g_tip.state = TIP_STATE_IDLE;
                }
                return TRUE;
            }
        }
        return FALSE;
    }

    /* Enter / Return confirmation */
    if (key == '\r' || key == '\n') {
        if (g_tip.state != TIP_STATE_IDLE) {
            if (out_commit && max_commit > 0) {
                tip_get_converted_text(out_commit, max_commit);
            }
            tip_cancel();
            return TRUE;
        }
        return FALSE;
    }

    /* Tibetan Dictionary Popup Trigger: Tab, Shift+Space, Ctrl+Space, or F4 */
    BOOL is_shift_space = (key == ' ' && (modifiers & (BTRON_KMOD_SHIFT | BTRON_KMOD_CTRL)));
    BOOL is_tab_trigger = (key == '	' || key == 9 || key == BTRON_KEY_F4);

    if (g_tip.mode == TIP_MODE_TIBETAN && (is_shift_space || is_tab_trigger)) {
        if (g_tip.state == TIP_STATE_PRECOMP) {
            g_tip.num_clauses = 1;
            g_tip.active_clause = 0;
            strncpy(g_tip.clauses[0].reading, g_tip.reading_buf, TIP_MAX_STR_LEN - 1);
            g_tip.clauses[0].selected_candidate = 0;
            g_tip.clauses[0].num_candidates = tibetan_lookup_candidates(
                g_tip.romaji_buf,
                g_tip.reading_buf,
                g_tip.clauses[0].candidates,
                TIP_MAX_CANDIDATES
            );
            if (g_tip.clauses[0].num_candidates > 0) {
                strncpy(g_tip.clauses[0].converted,
                        g_tip.clauses[0].candidates[0].value,
                        TIP_MAX_STR_LEN - 1);
            } else {
                strncpy(g_tip.clauses[0].converted, g_tip.reading_buf, TIP_MAX_STR_LEN - 1);
            }
            g_tip.state = TIP_STATE_CANDIDATE_SELECT;
            g_tip.candidate_window_visible = TRUE;
            return TRUE;
        } else if (g_tip.state == TIP_STATE_CONVERTING || g_tip.state == TIP_STATE_CANDIDATE_SELECT) {
            /* Cycle candidate in Tibetan mode */
            if (g_tip.num_clauses > 0) {
                TIP_CLAUSE *c = &g_tip.clauses[g_tip.active_clause];
                if (c->num_candidates > 0) {
                    c->selected_candidate = (c->selected_candidate + 1) % c->num_candidates;
                    strncpy(c->converted, c->candidates[c->selected_candidate].value, TIP_MAX_STR_LEN - 1);
                }
            }
            return TRUE;
        }
    }

    /* Space key handling: In Tibetan mode, Space commits pre-edit with Tsheg ('་') directly */
    if (key == ' ' && !(modifiers & (BTRON_KMOD_SHIFT | BTRON_KMOD_CTRL))) {
        if (g_tip.mode == TIP_MODE_TIBETAN) {
            if (g_tip.state == TIP_STATE_PRECOMP) {
                if (out_commit && max_commit > 0) {
                    char temp_buf[TIP_MAX_STR_LEN];
                    tip_get_converted_text(temp_buf, sizeof(temp_buf));
                    size_t cur_len = strlen(temp_buf);
                    /* Append Tsheg (་ = 0x0F0B = à¼) if not ending in tsheg/shad */
                    if (cur_len >= 3 &&
                        (unsigned char)temp_buf[cur_len-3] == 0xE0 &&
                        (unsigned char)temp_buf[cur_len-2] == 0xBC &&
                        ((unsigned char)temp_buf[cur_len-1] == 0x8B ||
                         (unsigned char)temp_buf[cur_len-1] == 0x8D ||
                         (unsigned char)temp_buf[cur_len-1] == 0x8E)) {
                        snprintf(out_commit, max_commit, "%s", temp_buf);
                    } else {
                        snprintf(out_commit, max_commit, "%s\xe0\xbc\x8b", temp_buf);
                    }
                }
                tip_cancel();
                return TRUE;
            } else if (g_tip.state == TIP_STATE_CANDIDATE_SELECT || g_tip.state == TIP_STATE_CONVERTING) {
                /* Commit selected candidate with Tsheg */
                if (out_commit && max_commit > 0) {
                    char temp_buf[TIP_MAX_STR_LEN];
                    tip_get_converted_text(temp_buf, sizeof(temp_buf));
                    snprintf(out_commit, max_commit, "%s\xe0\xbc\x8b", temp_buf);
                }
                tip_cancel();
                return TRUE;
            } else if (g_tip.state == TIP_STATE_IDLE) {
                /* In Tibetan mode, pressing space emits a Tsheg (་) */
                if (out_commit && max_commit > 0) {
                    snprintf(out_commit, max_commit, "\xe0\xbc\x8b");
                }
                return TRUE;
            }
        }

        /* Japanese Mozc Mode Space key */
        if (g_tip.state == TIP_STATE_PRECOMP) {
            mozc_lattice_search(g_tip.reading_buf, g_tip.clauses, &g_tip.num_clauses, TIP_MAX_CLAUSES);
            if (g_tip.mode == TIP_MODE_KATAKANA) {
                for (int i = 0; i < g_tip.num_clauses; i++) {
                    mozc_hiragana_to_katakana(g_tip.clauses[i].reading, g_tip.clauses[i].converted, TIP_MAX_STR_LEN);
                }
            }
            g_tip.active_clause = 0;
            g_tip.state = TIP_STATE_CONVERTING;
            g_tip.candidate_window_visible = TRUE;
            return TRUE;
        } else if (g_tip.state == TIP_STATE_CONVERTING || g_tip.state == TIP_STATE_CANDIDATE_SELECT) {
            g_tip.state = TIP_STATE_CANDIDATE_SELECT;
            g_tip.candidate_window_visible = TRUE;
            if (g_tip.num_clauses > 0) {
                TIP_CLAUSE *c = &g_tip.clauses[g_tip.active_clause];
                if (c->num_candidates > 0) {
                    c->selected_candidate = (c->selected_candidate + 1) % c->num_candidates;
                    strncpy(c->converted, c->candidates[c->selected_candidate].value, TIP_MAX_STR_LEN - 1);
                }
            }
            return TRUE;
        }
        return FALSE;
    }

    /* UP Arrow / Ctrl+P: Navigate to previous candidate in suggestion window */
    if (key == BTRON_KEY_UP || (key == 'p' && (modifiers & BTRON_KMOD_CTRL))) {
        if (g_tip.state == TIP_STATE_CONVERTING || g_tip.state == TIP_STATE_CANDIDATE_SELECT) {
            g_tip.state = TIP_STATE_CANDIDATE_SELECT;
            g_tip.candidate_window_visible = TRUE;
            if (g_tip.num_clauses > 0) {
                TIP_CLAUSE *c = &g_tip.clauses[g_tip.active_clause];
                if (c->num_candidates > 0) {
                    c->selected_candidate = (c->selected_candidate - 1 + c->num_candidates) % c->num_candidates;
                    strncpy(c->converted, c->candidates[c->selected_candidate].value, TIP_MAX_STR_LEN - 1);
                }
            }
            return TRUE;
        }
    }

    /* DOWN Arrow / Ctrl+N: Navigate to next candidate in suggestion window */
    if (key == BTRON_KEY_DOWN || (key == 'n' && (modifiers & BTRON_KMOD_CTRL))) {
        if (g_tip.state == TIP_STATE_CONVERTING || g_tip.state == TIP_STATE_CANDIDATE_SELECT) {
            g_tip.state = TIP_STATE_CANDIDATE_SELECT;
            g_tip.candidate_window_visible = TRUE;
            if (g_tip.num_clauses > 0) {
                TIP_CLAUSE *c = &g_tip.clauses[g_tip.active_clause];
                if (c->num_candidates > 0) {
                    c->selected_candidate = (c->selected_candidate + 1) % c->num_candidates;
                    strncpy(c->converted, c->candidates[c->selected_candidate].value, TIP_MAX_STR_LEN - 1);
                }
            }
            return TRUE;
        }
    }

    /* LEFT / RIGHT Arrow: Clause navigation in multi-clause conversion */
    if (key == BTRON_KEY_LEFT) {
        if (g_tip.state == TIP_STATE_CONVERTING || g_tip.state == TIP_STATE_CANDIDATE_SELECT) {
            if (g_tip.num_clauses > 1) {
                g_tip.active_clause = (g_tip.active_clause - 1 + g_tip.num_clauses) % g_tip.num_clauses;
                return TRUE;
            }
        }
    }
    if (key == BTRON_KEY_RIGHT) {
        if (g_tip.state == TIP_STATE_CONVERTING || g_tip.state == TIP_STATE_CANDIDATE_SELECT) {
            if (g_tip.num_clauses > 1) {
                g_tip.active_clause = (g_tip.active_clause + 1) % g_tip.num_clauses;
                return TRUE;
            }
        }
    }

    /* Numeric selection in candidate select mode (1-9) */
    if (g_tip.state == TIP_STATE_CANDIDATE_SELECT && key >= '1' && key <= '9') {
        int sel = (key - '1');
        if (g_tip.num_clauses > 0) {
            TIP_CLAUSE *c = &g_tip.clauses[g_tip.active_clause];
            if (sel < c->num_candidates) {
                c->selected_candidate = sel;
                strncpy(c->converted, c->candidates[sel].value, TIP_MAX_STR_LEN - 1);
                /* Advance to next clause or confirm */
                if (g_tip.active_clause + 1 < g_tip.num_clauses) {
                    g_tip.active_clause++;
                } else {
                    g_tip.state = TIP_STATE_CONVERTING;
                    g_tip.candidate_window_visible = FALSE;
                }
            }
        }
        return TRUE;
    }

    /* Printable ASCII character input */
    if (key >= 32 && key <= 126) {
        if (g_tip.state == TIP_STATE_CONVERTING || g_tip.state == TIP_STATE_CANDIDATE_SELECT) {
            /* Commit current conversion first */
            if (out_commit && max_commit > 0) {
                tip_get_converted_text(out_commit, max_commit);
            }
            tip_cancel();
        }

        if (g_tip.romaji_len < TIP_MAX_PRECOMP_LEN - 2) {
            g_tip.romaji_buf[g_tip.romaji_len++] = (char)key;
            g_tip.romaji_buf[g_tip.romaji_len] = '\0';

            if (g_tip.mode == TIP_MODE_TIBETAN) {
                wylie_to_tibetan(g_tip.romaji_buf, g_tip.reading_buf, sizeof(g_tip.reading_buf));
            } else {
                mozc_romaji_to_hiragana(g_tip.romaji_buf, g_tip.reading_buf, sizeof(g_tip.reading_buf));
            }
            g_tip.reading_len = (int)strlen(g_tip.reading_buf);
            g_tip.state = TIP_STATE_PRECOMP;
            return TRUE;
        }
    }

    return FALSE;
}


H tip_get_caret_x(void) {
    return g_tip.caret_pos.x > 0 ? g_tip.caret_pos.x : 240;
}

H tip_get_caret_y(void) {
    return g_tip.caret_pos.y > 0 ? g_tip.caret_pos.y : 200;
}

BOOL tip_is_candidate_window_visible(void) {
    return g_tip.candidate_window_visible;
}

/*
 * Render Floating Candidate Window in classic Sakamura double-bordered style (btron-tip.tex Section 4.2)
 */
H tip_calc_text_width(const char *text) {
    if (!text) return 0;
    H total_w = 0;
    const unsigned char *p = (const unsigned char *)text;
    while (*p) {
        if (*p == '\n') {
            p++;
            continue;
        }
        if (*p < 0x80) {
            total_w += 8;
            p++;
        } else if ((*p & 0xE0) == 0xC0) {
            total_w += 16;
            p += (p[1] != '\0' ? 2 : 1);
        } else if ((*p & 0xF0) == 0xE0) {
            UW cp = 0;
            if (p[1] && p[2]) {
                cp = ((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
            }
            if (cp >= 0x0F00 && cp <= 0x0FFF) {
                if ((cp >= 0x0F71 && cp <= 0x0F84) || (cp >= 0x0F90 && cp <= 0x0FBC)) {
                    /* Combining vowel mark or subjoined consonant */
                    total_w += 0;
                } else if (cp == 0x0F0B || cp == 0x0F0C || cp == 0x0F0D || cp == 0x0F0E) {
                    total_w += 5;
                } else {
                    total_w += 8;
                }
            } else {
                total_w += 16;
            }
            p += (p[1] && p[2] ? 3 : 1);
        } else if ((*p & 0xF8) == 0xF0) {
            total_w += 16;
            p += (p[1] && p[2] && p[3] ? 4 : 1);
        } else {
            total_w += 8;
            p++;
        }
    }
    return total_w;
}

H tip_calc_candidate_window_width(const TIP_CLAUSE *clause) {
    if (!clause || clause->num_candidates == 0) return 260;

    H max_val_w = 0;
    H max_ann_w = 0;

    for (int i = 0; i < clause->num_candidates; i++) {
        H val_w = tip_calc_text_width(clause->candidates[i].value);
        if (val_w > max_val_w) max_val_w = val_w;

        if (clause->candidates[i].annotation[0] != '\0') {
            H ann_w = tip_calc_text_width(clause->candidates[i].annotation) + 16; /* for parentheses */
            if (ann_w > max_ann_w) max_ann_w = ann_w;
        }
    }

    H num_prefix_w = 28; /* "1  " */
    H val_col_w = max_val_w;
    H ann_spacing = (max_ann_w > 0) ? 14 : 0;
    H ann_col_w = max_ann_w;

    /* Total calculated width: margins(16) + prefix + value + spacing + annotation */
    H total_needed_w = 16 + num_prefix_w + val_col_w + ann_spacing + ann_col_w + 16;
    H min_w = 260;
    return (total_needed_w > min_w) ? total_needed_w : min_w;
}

void tip_render_candidate_window(GDEV *dev, H caret_x, H caret_y) {
    if (!dev || !g_tip.candidate_window_visible ||
        (g_tip.state != TIP_STATE_CONVERTING && g_tip.state != TIP_STATE_CANDIDATE_SELECT)) {
        return;
    }

    if (g_tip.num_clauses == 0) return;
    const TIP_CLAUSE *clause = &g_tip.clauses[g_tip.active_clause];
    if (clause->num_candidates == 0) return;

    /* Dynamically calculate popup width from longest candidate and longest annotation */
    H max_val_w = 0;
    for (int i = 0; i < clause->num_candidates; i++) {
        H vw = tip_calc_text_width(clause->candidates[i].value);
        if (vw > max_val_w) max_val_w = vw;
    }

    H win_w = tip_calc_candidate_window_width(clause);
    H item_h = 18;
    H header_h = 22;
    H footer_h = 20;
    H win_h = header_h + clause->num_candidates * item_h + footer_h + 4;

    H win_x = caret_x;
    H win_y = caret_y + 20;

    /* Prevent overflowing screen boundary */
    if (win_x + win_w > dev->width) win_x = dev->width - win_w - 4;
    if (win_y + win_h > dev->height) win_y = dev->height - win_h - 4;
    if (win_x < 4) win_x = 4;
    if (win_y < 4) win_y = 4;

    RECT win_rect = { win_x, win_y, win_x + win_w, win_y + win_h };

    /* 1. Window Background */
    fill_rec(dev, &win_rect, COLOR_WHITE);

    /* 2. Sakamura Double Border (border width = 2) */
    drw_rec(dev, &win_rect);
    RECT inner_rect = { win_x + 2, win_y + 2, win_x + win_w - 2, win_y + win_h - 2 };
    drw_rec(dev, &inner_rect);

    /* 3. Title Bar: "Candidates - Mozc / Tibetan TIP [x]" */
    RECT title_rect = { win_x + 3, win_y + 3, win_x + win_w - 3, win_y + header_h };
    fill_rec(dev, &title_rect, COLOR_LTGRAY);
    drw_lin(dev, win_x + 2, win_y + header_h, win_x + win_w - 2, win_y + header_h);
    const char *title_str = (g_tip.mode == TIP_MODE_TIBETAN) ? "Candidates - Tibetan TIP" : "Candidates - Mozc";
    drw_tc_string(dev, win_x + 8, win_y + 5, title_str, COLOR_NAVY, 0x00000000);
    drw_tc_string(dev, win_x + win_w - 22, win_y + 5, "[x]", COLOR_BLACK, 0x00000000);

    /* 4. Candidate List with Categories & Dynamic Unclipped Annotation Alignment */
    H list_y = win_y + header_h + 2;
    H ann_offset_x = 8 + 28 + max_val_w + 12;

    for (int i = 0; i < clause->num_candidates; i++) {
        RECT item_rect = { win_x + 4, list_y, win_x + win_w - 4, list_y + item_h };
        COLOR fg = COLOR_BLACK;
        COLOR bg = 0x00000000;

        if (i == clause->selected_candidate) {
            fill_rec(dev, &item_rect, COLOR_NAVY);
            fg = COLOR_WHITE;
        }

        char item_buf[64];
        snprintf(item_buf, sizeof(item_buf), "%d  %s", i + 1, clause->candidates[i].value);
        drw_tc_string(dev, win_x + 8, list_y + 2, item_buf, fg, bg);

        /* Category Annotation aligned without clipping */
        if (clause->candidates[i].annotation[0] != '\0') {
            char ann_buf[64];
            snprintf(ann_buf, sizeof(ann_buf), "(%s)", clause->candidates[i].annotation);
            drw_tc_string(dev, win_x + ann_offset_x, list_y + 2, ann_buf, i == clause->selected_candidate ? COLOR_YELLOW : COLOR_GRAY, bg);
        }

        list_y += item_h;
    }

    /* 5. Footer separator and hints */
    drw_lin(dev, win_x + 4, list_y + 2, win_x + win_w - 4, list_y + 2);
    drw_tc_string(dev, win_x + 8, list_y + 5, "1-9 Select   Esc Cancel", COLOR_GRAY, 0x00000000);
}

/* ── BTRON3 SPEC 3.20 Section 3.7 Syscall Implementation ── */
#define MAX_TIP_PORTS 16
static TIP_CONTEXT g_tip_ports[MAX_TIP_PORTS];
static BOOL g_port_used[MAX_TIP_PORTS] = { FALSE };

ID iopn_tip(WND *wnd) {
    (void)wnd;
    for (int i = 0; i < MAX_TIP_PORTS; i++) {
        if (!g_port_used[i]) {
            g_port_used[i] = TRUE;
            memset(&g_tip_ports[i], 0, sizeof(TIP_CONTEXT));
            g_tip_ports[i].mode = TIP_MODE_HIRAGANA;
            g_tip_ports[i].state = TIP_STATE_IDLE;
            return (ID)(i + 1);
        }
    }
    return (ID)E_LIMIT;
}

ER icls_tip(ID tipid) {
    int idx = (int)tipid - 1;
    if (idx < 0 || idx >= MAX_TIP_PORTS || !g_port_used[idx]) return E_ID;
    g_port_used[idx] = FALSE;
    return E_OK;
}

ER ichg_mod(ID tipid, W mode) {
    int idx = (int)tipid - 1;
    if (idx < 0 || idx >= MAX_TIP_PORTS || !g_port_used[idx]) return E_ID;
    g_tip_ports[idx].mode = (TIP_INPUT_MODE)mode;
    return E_OK;
}

W iput_key(ID tipid, UW key, UW mod, TIPREC *rec) {
    (void)tipid;
    if (!rec) return 0;
    rec->result = 0;

    char commit[TIP_MAX_PRECOMP_LEN] = "";
    BOOL handled = tip_process_key(key, mod, commit, sizeof(commit));

    if (commit[0] != '\0') {
        rec->result |= TIP_OUT;
        if (rec->out_str && rec->out_len > 0) {
            strncpy(rec->out_str, commit, rec->out_len - 1);
            rec->out_str[rec->out_len - 1] = '\0';
        }
    }

    if (g_tip.state != TIP_STATE_IDLE) {
        rec->result |= TIP_CNV;
        if (rec->cnv_str && rec->cnv_len > 0) {
            tip_get_converted_text(rec->cnv_str, rec->cnv_len);
        }
        rec->car_pos = tip_get_composition_length();
        rec->result |= TIP_CAR;
    }

    if (tip_is_candidate_window_visible()) {
        rec->result |= TIP_CL;
    }

    return handled ? 1 : 0;
}
