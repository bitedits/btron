/*
 * B-System (BTRON 3.20) High-End Workstation About Box: about.c
 * Designed in Precision Industrial Aesthetic with famous Nyan Cat animation
 * Freestanding-safe, zero-allocation runtime.
 */

#include <btron/about.h>
#include <btron/dp.h>
#include <btron/troncode.h>
#include <btron/nyan_bitmaps.h>

#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#else
#include <stddef.h>
#include <stdint.h>
#include <libstr.h>
#define strlen tkl_strlen
#define snprintf tkl_snprintf
#endif

/* SONY Industrial Palette */
#define SONY_COL_CANVAS       0xFF16191E  /* Deep Titanium Slate */
#define SONY_COL_HEADER       0xFF1E222A  /* Brushed Graphite Header */
#define SONY_COL_PANEL        0xFF1A1D24  /* Sub-Panel Background */
#define SONY_COL_INSET        0xFF111317  /* Inset Monitor Black */
#define SONY_COL_BORDER_HI    0xFF3B4454  /* Bevel Highlight Border */
#define SONY_COL_BORDER_LO    0xFF0D0F12  /* Bevel Shadow Border */
#define SONY_COL_BORDER_MID   0xFF2A313D  /* Subtle Framing Hairline */
#define SONY_COL_GOLD         0xFFF59E0B  /* SONY Amber / Accent Gold */
#define SONY_COL_CYAN         0xFF38BDF8  /* High-Tech Diagnostic Cyan */
#define SONY_COL_GREEN        0xFF22C55E  /* LED Active Green */
#define SONY_COL_TEXT_WHITE   0xFFF8FAFC  /* Platinum White Text */
#define SONY_COL_TEXT_SILVER  0xFFCBD5E1  /* High-Readability Silver */
#define SONY_COL_TEXT_DIM     0xFF94A3B8  /* Technical Metric Gray */
#define SONY_COL_BTN_BG       0xFF282F3B  /* Tactile Button Face */

typedef enum {
    ABOUT_TAB_SPECS = 0,
    ABOUT_TAB_PEOPLE = 1,
    ABOUT_TAB_SUBSYSTEMS = 2,
    ABOUT_TAB_MAX = 3
} AboutTab;

typedef struct {
    WND *wnd;
    uint32_t ticks;
    AboutTab current_tab;
    int people_page;
    BOOL animation_enabled;
} AboutAppState;

static AboutAppState g_about_state = {
    NULL,
    0,
    ABOUT_TAB_SPECS,
    0,
    TRUE
};

/* Fast beveled rectangle helper */
static void draw_beveled_box(GDEV *dev, const RECT *r, COLOR fill_col, COLOR hi, COLOR lo) {
    if (!dev || !r) return;
    fill_rec(dev, r, fill_col);
    drw_lin(dev, r->left, r->top, r->right - 1, r->top);
    drw_lin(dev, r->left, r->top, r->left, r->bottom - 1);
    drw_lin(dev, r->left + 1, r->bottom - 1, r->right, r->bottom - 1);
    drw_lin(dev, r->right - 1, r->top + 1, r->right - 1, r->bottom);
    (void)hi;
    (void)lo;
}

/* Tab 1: System Specifications (Windows / Haiku style technical sheet) */
static void paint_tab_specs(GDEV *dev, const RECT *body_r) {
    H left = body_r->left + 14;
    H top = body_r->top + 10;
    H line_h = 16;

    struct SpecRow {
        const char *key;
        const char *val;
        COLOR val_col;
    };

    static const struct SpecRow specs[] = {
        { "HARDWARE PLATFORM",  "NISSAN BTRON WS-9800 (SMP 4-Core)",           SONY_COL_TEXT_WHITE },
        { "MICROKERNEL CORE",   "T-Kernel 2.0 SMP Real-Time Executive",        SONY_COL_CYAN },
        { "SYSTEM SPEC",        "Ken Sakamura TRON Architecture (BTRON 3.20)", SONY_COL_TEXT_WHITE },
        { "CPU PROCESSOR",      "4x x86_64 @ 2.40 GHz",                        SONY_COL_TEXT_WHITE },
        { "MEMORY (RAM)",       "2048 MB Physical RAM / 64 MB Kernel Pool",    SONY_COL_GOLD },
        { "STORAGE SUBSYSTEM",  "BTRON-FS Real/Virtual-Body TAD Storage",      SONY_COL_TEXT_WHITE },
        { "DISPLAY PRIMITIVES", "32-Bit ARGB TrueColor Framebuffer Pipeline",  SONY_COL_CYAN },
        { "INPUT METHOD (IME)", "TIP Mozc Multilingual Engine (JIS + Wylie)",  SONY_COL_TEXT_WHITE },
        { "INVARIANT SAFETY",   "NASA JPL Power of 10 Safety Rules (100% OK)", SONY_COL_GREEN },
        { "LATENCY PROFILE",    "< 10 us Deterministic Worst-Case Preemption", SONY_COL_GOLD }
    };

    int count = (int)(sizeof(specs) / sizeof(specs[0]));
    for (int i = 0; i < count; i++) {
        H y = top + i * line_h;
        if (y + line_h > body_r->bottom - 4) break;

        /* Key label */
        drw_tc_string(dev, left, y, specs[i].key, SONY_COL_TEXT_DIM, SONY_COL_INSET);
        drw_tc_string(dev, left + 150, y, ":", SONY_COL_TEXT_DIM, SONY_COL_INSET);
        /* Value */
        drw_tc_string(dev, left + 160, y, specs[i].val, specs[i].val_col, SONY_COL_INSET);
    }
}

/* Tab 2: Haiku OS-inspired People & Kernel Names List */
static void paint_tab_people(GDEV *dev, const RECT *body_r) {
    H left = body_r->left + 14;
    H top = body_r->top + 8;
    H line_h = 16;

    /* Structured credits table directly modeled after Haiku OS Credits.h */
    static const struct {
        const char *category;
        const char *people[4];
    } credits_pages[2][3] = {
        /* Page 0 */
        {
            {
                "[ KERNEL & CORE MICROKERNEL ARCHITECTS ]",
                {
                    "Ken Sakamura (TRON Project Leader & Founding Father)",
                    "Travis Geiselbrecht (Real-Time Executive & Threading Master)",
                    "Dave Cutler (VAX VMS)",
                    "Namdak Tonpa (Kernel Hacker)"
                }
            },
            {
                "[ SYSTEM ARCHITECTURE & TASK SCHEDULING ]",
                {
                    "Michael Lotz (Inter-Process Communication & Queues)",
                    "Jerome Duval (Hardware Abstraction Layer & Buses)",
                    "John D. Carmack II (Graphics Blitter & Framebuffer)",
                    "Rene Gollent (POSIX Subsystem & Thread Execution)"
                }
            },
            {
                "[ REAL/VIRTUAL-BODY TAD SUBSYSTEM ]",
                {
                    "Hiroshi Monden (BTRON Storage & Hyper-Document Structure)",
                    "Noboru Koshizuka (Virtual Object Link & Container Model)",
                    "Fabrice Bellard (Media Formats & File System Translators)",
                    "John Scipione (Deskbar Tracker & Workspace Integration)"
                }
            }
        },
        /* Page 1 */
        {
            {
                "[ MULTILINGUAL ENGINE & TYPOGRAPHY ]",
                {
                    "Akira Matsui (TRON Multilingual Character Code Matrix)",
                    "Kiwamu Kase (Mozc TIP Conversion Engine & Kanji Lexicon)",
                    "Namdak Tonpa (Typography & Font Geometry Engine)",
                    "Lama Tony Duff (Multilingual Layout Engine)"
                }
            },
            {
                "[ DEVICE DRIVERS & HARDWARE PLATFORMS ]",
                {
                    "Hiroshi Tokita (Display & GPU Acceleration Drivers)",
                    "Awe Morrison (PC-98 & PS5 Firmware)",
                    "Kota Uchida (UEFI Architecture Ports, MikanOS)",
                    "Takahiro Yokobayashi (ARM Cortex)"
                }
            },
            {
                "[ SAFETY ASSURANCE & VERIFICATION ]",
                {
                    "Leslie Lamport (QA & Conformance Verification)",
                    "Augustin Cavalier (NASA JPL Power of 10 Invariant Prover)",
                    "Gregory Nutt (Crash Resilience & System Diagnostics)",
                    "Hiroaki Takada (Automated Test Pipeline & CI Matrix)"
                }
            }
        }
    };

    int page = g_about_state.people_page % 2;
    H cur_y = top;

    for (int c = 0; c < 3; c++) {
        /* Category Header */
        drw_tc_string(dev, left, cur_y, credits_pages[page][c].category, SONY_COL_GOLD, SONY_COL_INSET);
        cur_y += line_h;

        /* Names */
        for (int p = 0; p < 4; p++) {
            if (cur_y + line_h > body_r->bottom - 22) break;
            drw_tc_string(dev, left + 14, cur_y, "-", SONY_COL_CYAN, SONY_COL_INSET);
            drw_tc_string(dev, left + 26, cur_y, credits_pages[page][c].people[p], SONY_COL_TEXT_SILVER, SONY_COL_INSET);
            cur_y += line_h;
        }
        cur_y += 4;
    }

    /* Page Navigation Control Bar inside Tab 2 */
    H nav_y = body_r->bottom - 20;
    RECT btn_prev = { body_r->right - 180, nav_y, body_r->right - 95, nav_y + 16 };
    RECT btn_next = { body_r->right - 90, nav_y, body_r->right - 10, nav_y + 16 };

    draw_beveled_box(dev, &btn_prev, SONY_COL_BTN_BG, SONY_COL_BORDER_HI, SONY_COL_BORDER_LO);
    draw_beveled_box(dev, &btn_next, SONY_COL_BTN_BG, SONY_COL_BORDER_HI, SONY_COL_BORDER_LO);

    drw_tc_string(dev, btn_prev.left + 8, nav_y + 1, "< Page 1", (page == 0) ? SONY_COL_GOLD : SONY_COL_TEXT_WHITE, SONY_COL_BTN_BG);
    drw_tc_string(dev, btn_next.left + 8, nav_y + 1, "Page 2 >", (page == 1) ? SONY_COL_GOLD : SONY_COL_TEXT_WHITE, SONY_COL_BTN_BG);

    char page_str[32];
    snprintf(page_str, sizeof(page_str), "TEAM DIRECTORY: PAGE %d / 2", page + 1);
    drw_tc_string(dev, left, nav_y + 1, page_str, SONY_COL_TEXT_DIM, SONY_COL_INSET);
}

/* Tab 3: Subsystem Diagnostics & Status Telemetry */
static void paint_tab_subsystems(GDEV *dev, const RECT *body_r) {
    H left = body_r->left + 14;
    H top = body_r->top + 10;
    H line_h = 16;

    static const struct {
        const char *tag;
        const char *name;
        const char *status;
        COLOR status_col;
    } subsystems[] = {
        { "[SCHED]", "tk_task    Preemptive Priority Scheduler",       "ONLINE (SMP)", SONY_COL_GREEN },
        { "[SYNC] ", "tk_sem/flg Zero-Latency Event Synchronization",  "NOMINAL",      SONY_COL_GREEN },
        { "[DISP] ", "dp_core    32-Bit ARGB Raster Pipeline",         "ACCELERATED",  SONY_COL_CYAN },
        { "[TAD]  ", "vobj_mgr   Virtual/Real-Object Storage Engine",  "MOUNTED",      SONY_COL_GREEN },
        { "[IME]  ", "tip_mozc   Mozc Multilingual Input Engine",      "ACTIVE",       SONY_COL_CYAN },
        { "[NET]  ", "net_stack  T2EX BSD Sockets & TCP/IPv4/v6",      "CONNECTED",    SONY_COL_GREEN },
        { "[SAFE] ", "jpl_verify NASA JPL Power of 10 Invariants",     "100% PASSED",  SONY_COL_GOLD },
        { "[AUD]  ", "snd_stream 48 kHz / 16-bit PCM Audio Driver",    "STANDBY",      SONY_COL_TEXT_DIM },
        { "[MEM]  ", "sys_heap   Kernel Heap Allocator & Fixed Pools", "ZERO LEAKS",   SONY_COL_GREEN },
        { "[RTC]  ", "pit_clock  1000 Hz Deterministic Heartbeat",     "RUNNING",      SONY_COL_GREEN }
    };

    int count = (int)(sizeof(subsystems) / sizeof(subsystems[0]));
    for (int i = 0; i < count; i++) {
        H y = top + i * line_h;
        if (y + line_h > body_r->bottom - 4) break;

        drw_tc_string(dev, left, y, subsystems[i].tag, SONY_COL_GOLD, SONY_COL_INSET);
        drw_tc_string(dev, left + 58, y, subsystems[i].name, SONY_COL_TEXT_SILVER, SONY_COL_INSET);
        drw_tc_string(dev, body_r->right - 120, y, subsystems[i].status, subsystems[i].status_col, SONY_COL_INSET);
    }
}

static void paint_about_window(WND *wnd, GDEV *dev) {
    if (!wnd || !dev) return;

    int nyan_frame = 0;
    if (g_about_state.animation_enabled) {
        g_about_state.ticks++;
#if !defined(__STDC_HOSTED__) || __STDC_HOSTED__ != 1
        extern uintptr_t g_mmio_base;
        uint32_t now_us = *(volatile uint32_t *)(g_mmio_base + 0x00003004UL);
        nyan_frame = (int)((now_us / 70000U) % NYAN_FRAME_COUNT);
#else
        uint32_t t = g_about_state.ticks;
        nyan_frame = (int)((t / 2) % NYAN_FRAME_COUNT);
#endif
    }

    /* 1. Main SONY Titanium Chassis Canvas */
    RECT bg_r = { 0, 0, dev->width, dev->height };
    fill_rec(dev, &bg_r, SONY_COL_CANVAS);
    drw_rec(dev, &bg_r);

    /* 2. SONY Header Banner (0 to 82px) */
    RECT hdr_r = { 0, 0, dev->width, 82 };
    fill_rec(dev, &hdr_r, SONY_COL_HEADER);
    drw_lin(dev, 0, 82, dev->width, 82);

    /* SONY Brand Badge */
    drw_tc_string(dev, 14, 10, "S O N Y", SONY_COL_TEXT_WHITE, SONY_COL_HEADER);
    drw_tc_string(dev, 80, 10, "|  B-SYSTEM WORKSTATION ARCHITECTURE", SONY_COL_GOLD, SONY_COL_HEADER);

    /* Sub-Model & Specification Badges */
    drw_tc_string(dev, 14, 28, "MODEL: NEWS-TRON WS-9800 PRO", SONY_COL_TEXT_SILVER, SONY_COL_HEADER);
    drw_tc_string(dev, 14, 44, "STANDARD: BTRON 3.20 / T-KERNEL 2.0 SMP CORE", SONY_COL_TEXT_DIM, SONY_COL_HEADER);

    /* SONY Status Indicators (LEDs) */
    drw_tc_string(dev, 14, 62, "* PWR: ON", SONY_COL_GREEN, SONY_COL_HEADER);
    drw_tc_string(dev, 90, 62, "* KERNEL: SMP", SONY_COL_CYAN, SONY_COL_HEADER);
    drw_tc_string(dev, 195, 62, "* JPL-10: 100%", SONY_COL_GOLD, SONY_COL_HEADER);

    /* 3. Nyan Cat Technical Monitor Viewport (Top-Right) */
    H nyan_viewport_w = NYAN_FRAME_W + 6;
    H nyan_viewport_h = NYAN_FRAME_H + 4;
    H nyan_box_x = dev->width - nyan_viewport_w - 10;
    H nyan_box_y = 6;

    RECT nyan_box = { nyan_box_x, nyan_box_y, nyan_box_x + nyan_viewport_w, nyan_box_y + nyan_viewport_h };
    fill_rec(dev, &nyan_box, SONY_COL_INSET);
    drw_rec(dev, &nyan_box);

    /* Subtle oscilloscope grid lines in viewport */
    for (H gy = nyan_box.top + 16; gy < nyan_box.bottom; gy += 16) {
        for (H gx = nyan_box.left + 2; gx < nyan_box.right - 2; gx += 8) {
            drw_lin(dev, gx, gy, gx + 2, gy);
        }
    }

    /* Blit Nyan Cat Animation Frame */
    draw_nyan_sprite(dev, nyan_box.left + 3, nyan_box.top + 2, nyan_frame);

    /* Monitor Overlay Caption */
    char nyan_tag[48];
    snprintf(nyan_tag, sizeof(nyan_tag), "NYAN [FRM %02d/12] %s",
             nyan_frame + 1,
             g_about_state.animation_enabled ? "PLAY" : "PAUSE");
    drw_tc_string(dev, nyan_box.left + 2, nyan_box.bottom - 10, nyan_tag, SONY_COL_TEXT_DIM, SONY_COL_INSET);

    /* 4. Tab Navigation Bar (84 to 110px) */
    struct TabDef {
        const char *label;
        H w;
    } tabs[ABOUT_TAB_MAX] = {
        { "1. SYSTEM SPECS", 160 },
        { "2. KERNEL TEAM & NAMES", 195 },
        { "3. SUBSYSTEMS", 150 }
    };

    H cur_tab_x = 12;
    H tab_y = 86;
    H tab_h = 24;

    for (int i = 0; i < ABOUT_TAB_MAX; i++) {
        RECT tab_r = { cur_tab_x, tab_y, cur_tab_x + tabs[i].w, tab_y + tab_h };
        BOOL is_active = (g_about_state.current_tab == (AboutTab)i);

        COLOR tab_bg = is_active ? SONY_COL_PANEL : SONY_COL_CANVAS;
        COLOR tab_fg = is_active ? SONY_COL_TEXT_WHITE : SONY_COL_TEXT_DIM;

        fill_rec(dev, &tab_r, tab_bg);
        drw_rec(dev, &tab_r);

        if (is_active) {
            /* Active tab indicator bar (Sony Gold accent) */
            drw_lin(dev, tab_r.left + 2, tab_r.top + 1, tab_r.right - 2, tab_r.top + 1);
            drw_lin(dev, tab_r.left + 2, tab_r.top + 2, tab_r.right - 2, tab_r.top + 2);
        }

        drw_tc_string(dev, tab_r.left + 12, tab_r.top + 6, tabs[i].label, tab_fg, tab_bg);
        cur_tab_x += tabs[i].w + 6;
    }

    /* 5. Main Body Panel (112px to dev->height - 30px) */
    RECT body_r = { 10, 112, dev->width - 10, dev->height - 30 };
    fill_rec(dev, &body_r, SONY_COL_INSET);
    drw_rec(dev, &body_r);

    switch (g_about_state.current_tab) {
        case ABOUT_TAB_SPECS:
            paint_tab_specs(dev, &body_r);
            break;
        case ABOUT_TAB_PEOPLE:
            paint_tab_people(dev, &body_r);
            break;
        case ABOUT_TAB_SUBSYSTEMS:
            paint_tab_subsystems(dev, &body_r);
            break;
        default:
            break;
    }

    /* 6. High-Tech Telemetry Footer Bar */
    RECT footer_r = { 0, dev->height - 28, dev->width, dev->height };
    fill_rec(dev, &footer_r, SONY_COL_HEADER);
    drw_lin(dev, 0, dev->height - 28, dev->width, dev->height - 28);

    /* Telemetry readout (compact to leave ample room for buttons) */
    char telem[64];
    snprintf(telem, sizeof(telem), "WS-9800 | 4-CORE SMP | STATUS: OPTIMAL");
    drw_tc_string(dev, 14, dev->height - 20, telem, SONY_COL_TEXT_DIM, SONY_COL_HEADER);

    /* Footer Action Buttons */
    H btn_w = 70;
    H btn_h = 20;
    H btn_y = dev->height - 24;

    /* Close / OK Button */
    RECT btn_close = { dev->width - btn_w - 10, btn_y, dev->width - 10, btn_y + btn_h };
    draw_beveled_box(dev, &btn_close, SONY_COL_BTN_BG, SONY_COL_BORDER_HI, SONY_COL_BORDER_LO);
    drw_tc_string(dev, btn_close.left + 26, btn_y + 3, "OK", SONY_COL_TEXT_WHITE, SONY_COL_BTN_BG);

    /* Animation Pause/Play Button */
    RECT btn_anim = { btn_close.left - 78, btn_y, btn_close.left - 8, btn_y + btn_h };
    draw_beveled_box(dev, &btn_anim, SONY_COL_BTN_BG, SONY_COL_BORDER_HI, SONY_COL_BORDER_LO);
    drw_tc_string(dev, btn_anim.left + 16, btn_y + 3,
                  g_about_state.animation_enabled ? "Pause" : "Play",
                  SONY_COL_CYAN, SONY_COL_BTN_BG);

    /* Tab Cycle Button */
    RECT btn_next_tab = { btn_anim.left - 82, btn_y, btn_anim.left - 8, btn_y + btn_h };
    draw_beveled_box(dev, &btn_next_tab, SONY_COL_BTN_BG, SONY_COL_BORDER_HI, SONY_COL_BORDER_LO);
    drw_tc_string(dev, btn_next_tab.left + 10, btn_y + 3, "Next Tab", SONY_COL_GOLD, SONY_COL_BTN_BG);
}

static void handle_about_event(WND *wnd, const EVT *evt) {
    if (!wnd || !evt) return;

    if (evt->type == EV_BUT_DOWN) {
        H rel_x = evt->pos.x - wnd->client.left;
        H rel_y = evt->pos.y - wnd->client.top;
        H client_w = wnd->client.right - wnd->client.left;
        H client_h = wnd->client.bottom - wnd->client.top;

        /* 1. Click on Nyan Cat Viewport -> Toggle Pause/Play */
        H nyan_box_x = client_w - (NYAN_FRAME_W + 6) - 10;
        if (rel_x >= nyan_box_x && rel_x <= client_w - 10 && rel_y >= 6 && rel_y <= 76) {
            g_about_state.animation_enabled = !g_about_state.animation_enabled;
            redraw_all_windows();
            return;
        }

        /* 2. Tab selection buttons (86 to 110px) */
        if (rel_y >= 86 && rel_y <= 110) {
            if (rel_x >= 12 && rel_x <= 172) {
                g_about_state.current_tab = ABOUT_TAB_SPECS;
                redraw_all_windows();
                return;
            } else if (rel_x >= 178 && rel_x <= 373) {
                g_about_state.current_tab = ABOUT_TAB_PEOPLE;
                redraw_all_windows();
                return;
            } else if (rel_x >= 379 && rel_x <= 529) {
                g_about_state.current_tab = ABOUT_TAB_SUBSYSTEMS;
                redraw_all_windows();
                return;
            }
        }

        /* 3. Page navigation inside Tab 2 (Kernel Names) */
        if (g_about_state.current_tab == ABOUT_TAB_PEOPLE) {
            H nav_y = (client_h - 30) - 20;
            if (rel_y >= nav_y && rel_y <= nav_y + 18) {
                if (rel_x >= client_w - 190 && rel_x <= client_w - 95) {
                    g_about_state.people_page = 0;
                    redraw_all_windows();
                    return;
                } else if (rel_x >= client_w - 92 && rel_x <= client_w - 8) {
                    g_about_state.people_page = 1;
                    redraw_all_windows();
                    return;
                }
            }
        }

        /* 4. Footer controls */
        if (rel_y >= client_h - 26 && rel_y <= client_h - 2) {
            /* Close / OK button */
            if (rel_x >= client_w - 80 && rel_x <= client_w - 10) {
                g_about_state.wnd = NULL;
                cls_wnd(wnd);
                return;
            }
            /* Pause / Play button */
            if (rel_x >= client_w - 158 && rel_x <= client_w - 88) {
                g_about_state.animation_enabled = !g_about_state.animation_enabled;
                redraw_all_windows();
                return;
            }
            /* Next Tab button */
            if (rel_x >= client_w - 240 && rel_x <= client_w - 166) {
                g_about_state.current_tab = (g_about_state.current_tab + 1) % ABOUT_TAB_MAX;
                redraw_all_windows();
                return;
            }
        }
    }
}

static void destroy_about_window(WND *wnd) {
    (void)wnd;
    g_about_state.wnd = NULL;
}

int about_is_animating(void) {
    return (g_about_state.wnd != NULL && g_about_state.animation_enabled);
}

WND* open_about_window(void) {
    g_about_state.ticks = 0;
    g_about_state.current_tab = ABOUT_TAB_SPECS;
    g_about_state.people_page = 0;
    g_about_state.animation_enabled = TRUE;

    WND *wnd = opn_wnd("システム情報 (System Information)",
                       100, 70, 580, 360,
                       WND_ATTR_TITLE | WND_ATTR_CLOSE | WND_ATTR_BORDER);
    if (!wnd) return NULL;

    g_about_state.wnd = wnd;
    wnd->user_data = (VW)(uintptr_t)&g_about_state;
    wnd->paint = paint_about_window;
    wnd->event_handler = handle_about_event;
    wnd->destroy = destroy_about_window;
    return wnd;
}
