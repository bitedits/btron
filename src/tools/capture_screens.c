/*
 * B-System (BTRON 3.20) Automated Headless Window Screenshot Capturer
 * src/tools/capture_screens.c
 *
 * Renders authentic C99 windows directly onto a headless GDEV framebuffer,
 * extracts pixel-perfect isolated window bounding boxes (including 3D bevels,
 * sliding tabs, fonts, and controls) with transparent background around the window,
 * and dumps raw ARGB frames for PNG conversion.
 *
 * Guaranteed Strict Window Isolation:
 * Resets window manager state and clears canvas before each individual capture.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <btron/types.h>
#include <btron/dp.h>
#include <btron/wnd.h>
#include <btron/event.h>
#include <btron/settings.h>
#include <btron/language_settings.h>
#include <btron/app_menu.h>
#include <btron/tip.h>
#include <btron/t_editor.h>

/* Forward declarations for Settings applet window openers */
extern WND* open_control_panel_window(void);
extern WND* open_appearance_settings_window(void);
extern WND* open_desktop_settings_window(void);
extern WND* open_display_settings_window(void);
extern WND* open_input_settings_window(void);
extern WND* open_language_settings_window(void);
extern WND* open_media_settings_window(void);
extern WND* open_network_settings_window(void);
extern WND* open_security_settings_window(void);
extern WND* open_sound_settings_window(void);
extern WND* open_system_settings_window(void);
extern WND* open_terminal_settings_window(void);

extern WND* open_vobj_manager_window(void);
extern WND* open_t_editor_window(void);
extern WND* open_tad_browser_window(const char *filepath, const char *title);
extern WND* open_audio_player_window(void);
extern WND* open_orchestra_window(void);
extern WND* open_gterm_window(void);
extern WND* open_about_window(void);
extern WND* open_teditor_about_window(void);
extern WND* open_vobj_about_window(void);
extern WND* open_tad_browser_about_window(void);
extern WND* open_gterm_about_window(void);
extern WND* open_orchestra_about_window(void);
extern WND* open_cassette_about_window(void);
extern WND* open_t_editor_window_with_file(const char *filepath);
extern H    tip_get_caret_x(void);
extern H    tip_get_caret_y(void);

/* Forward declarations for GTerm internals */
typedef struct GTermState GTermState;
extern void gterm_append_line(GTermState *st, const char *text, COLOR col);

/* Forward declarations for Chat */
typedef struct ChatClient ChatClient;
extern void chat_ipc_init(void);
extern ChatClient* chat_ipc_register_client(const char *pref_nick);
extern WND* open_chat_main_window(ChatClient *client);
extern WND* open_chat_muc_window(ChatClient *client, const char *room_name);

/* Forward declarations for Global System Menu */
extern void global_menu_init(void);
extern void global_menu_render_bar(GDEV *dev);
extern void global_menu_render_overlay(GDEV *dev);
extern void global_menu_handle_mouse_down(H x, H y);
extern void global_menu_close(void);
extern BOOL global_menu_is_open(void);
extern void render_desktop_background(GDEV *screen);
extern void render_system_panel(GDEV *screen);
extern void tracker_init(void);

/* Minimal stubs for GTerm interactive commands */
void btron_core_print_ver(void *out_fn, void *user_data, const char *arg) { (void)out_fn; (void)user_data; (void)arg; }
void sys_get_devconf(void *p) { (void)p; }
void sys_get_mem_stats(void *p) { (void)p; }
void sys_mouse_get_pos(int *x, int *y) { if (x) *x = 0; if (y) *y = 0; }
void sys_mouse_set_pos(int x, int y) { (void)x; (void)y; }
void sys_mouse_click(int b) { (void)b; }
ER init_evt_sys(void) { return E_OK; }
ER init_vobj_sys(const char *storage_root) { (void)storage_root; return E_OK; }

/* Helper to dump raw ARGB rectangle to file */
static void dump_window_rect(GDEV *dev, WND *wnd, const char *out_filename) {
    if (!dev || !wnd) return;

    /* Window total geometry including title tab & 3D borders */
    int x0 = wnd->bounds.left - 4;
    int y0 = wnd->bounds.top - 30;
    int x1 = wnd->bounds.right + 6;
    int y1 = wnd->bounds.bottom + 6;

    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > dev->width) x1 = dev->width;
    if (y1 > dev->height) y1 = dev->height;

    int crop_w = x1 - x0;
    int crop_h = y1 - y0;

    FILE *fp = fopen(out_filename, "wb");
    if (!fp) {
        fprintf(stderr, "Failed to open output file: %s\n", out_filename);
        return;
    }

    /* Write 8-byte header: width (4 bytes LE), height (4 bytes LE) */
    fwrite(&crop_w, sizeof(int), 1, fp);
    fwrite(&crop_h, sizeof(int), 1, fp);

    for (int y = y0; y < y1; y++) {
        COLOR *row = &dev->pixels[y * dev->width + x0];
        fwrite(row, sizeof(COLOR), crop_w, fp);
    }

    fclose(fp);
    printf("  [CAPTURED] %-35s -> %s (%dx%d px)\n", wnd->title, out_filename, crop_w, crop_h);
}

static void dump_raw_region(GDEV *dev, int x0, int y0, int crop_w, int crop_h, const char *name, const char *out_filename) {
    if (!dev) return;

    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x0 + crop_w > dev->width) crop_w = dev->width - x0;
    if (y0 + crop_h > dev->height) crop_h = dev->height - y0;

    FILE *fp = fopen(out_filename, "wb");
    if (!fp) {
        fprintf(stderr, "Failed to open output file: %s\n", out_filename);
        return;
    }

    fwrite(&crop_w, sizeof(int), 1, fp);
    fwrite(&crop_h, sizeof(int), 1, fp);

    for (int y = y0; y < y0 + crop_h; y++) {
        COLOR *row = &dev->pixels[y * dev->width + x0];
        fwrite(row, sizeof(COLOR), crop_w, fp);
    }

    fclose(fp);
    printf("  [CAPTURED] %-35s -> %s (%dx%d px)\n", name, out_filename, crop_w, crop_h);
}

typedef struct {
    const char *name;
    WND* (*open_fn)(void);
} WINDOW_TARGET;

static void reset_isolation_state(GDEV *dev) {
    if (!dev || !dev->pixels) return;
    init_wnd_mgr(dev);
    memset((void*)dev->pixels, 0, dev->width * dev->height * sizeof(COLOR));
}

static void simulate_menu_click(WND *wnd, int header_idx) {
    if (!wnd || !wnd->event_handler) return;
    EVT evt;
    memset(&evt, 0, sizeof(EVT));
    evt.type = EV_BUT_DOWN;
    evt.pos.x = wnd->bounds.left + 4 + 20 + header_idx * 75;
    evt.pos.y = wnd->bounds.top + 26 + 10;
    wnd->event_handler(wnd, &evt);
}

static WND* open_isolated_chat_roster(void) {
    chat_ipc_init();
    ChatClient *client = chat_ipc_register_client(NULL);
    if (!client) return NULL;
    return open_chat_main_window(client);
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    printf("==========================================================\n");
    printf(" B-System Automated Headless Window Screenshot Capturer\n");
    printf(" (Strict Window Isolation, Active Content & Opened Menus)\n");
    printf("==========================================================\n");

    if (system("mkdir -p b-system/img/screens /tmp/btron_raw_screens") != 0) {
        /* Ignore error */
    }

    /* Create 1280x800 desktop canvas */
    GDEV *dev = opn_dev(1280, 800);
    if (!dev) {
        fprintf(stderr, "Error: Failed to allocate 1280x800 framebuffer\n");
        return 1;
    }
    init_wnd_mgr(dev);

    /* 1. Settings Applet Windows (_Settings suffix) */
    WINDOW_TARGET settings_targets[] = {
        { "Appearance_Settings", open_appearance_settings_window },
        { "Desktop_Settings", open_desktop_settings_window },
        { "Display_Settings", open_display_settings_window },
        { "Input_Settings", open_input_settings_window },
        { "Language_Settings", open_language_settings_window },
        { "Media_Settings", open_media_settings_window },
        { "Network_Settings", open_network_settings_window },
        { "Security_Settings", open_security_settings_window },
        { "Sound_Settings", open_sound_settings_window },
        { "System_Settings", open_system_settings_window },
        { "Terminal_Settings", open_terminal_settings_window },
        { "Preferences_Settings", open_control_panel_window },
        { NULL, NULL }
    };

    for (int i = 0; settings_targets[i].name != NULL; i++) {
        reset_isolation_state(dev);

        WND *w = settings_targets[i].open_fn();
        if (!w) {
            fprintf(stderr, "Error opening window: %s\n", settings_targets[i].name);
            continue;
        }

        redraw_all_windows();

        char raw_path[256];
        snprintf(raw_path, sizeof(raw_path), "/tmp/btron_raw_screens/%s.raw", settings_targets[i].name);
        dump_window_rect(dev, w, raw_path);
    }

    /* 2. Core Application Windows (_Application suffix) */
    {
        /* Cabinet Application Window */
        reset_isolation_state(dev);
        WND *w_cab = open_vobj_manager_window();
        if (w_cab) {
            redraw_all_windows();
            dump_window_rect(dev, w_cab, "/tmp/btron_raw_screens/Cabinet_Application.raw");
        }

        /* Editor Application Window */
        reset_isolation_state(dev);
        WND *w_ted = open_t_editor_window();
        if (w_ted) {
            redraw_all_windows();
            dump_window_rect(dev, w_ted, "/tmp/btron_raw_screens/Editor_Application.raw");
        }

        /* TAD Browser Application Window */
        reset_isolation_state(dev);
        WND *w_brw = open_tad_browser_window("tad_bin/01_btron3_spec.tad", "【仕様書】BTRON3 3.20 OS Specification");
        if (w_brw) {
            redraw_all_windows();
            dump_window_rect(dev, w_brw, "/tmp/btron_raw_screens/Browser_Application.raw");
        }

        /* Terminal Application Window (GTerm with live output) */
        reset_isolation_state(dev);
        WND *w_term = open_gterm_window();
        if (w_term) {
            GTermState *st = (GTermState*)(uintptr_t)w_term->user_data;
            if (st) {
                gterm_append_line(st, "btron:/> uname -a", COLOR_WHITE);
                gterm_append_line(st, "B-System 3.20 SMP (EMT64/ACPI) #1 SMP x86_64", COLOR_GREEN);
                gterm_append_line(st, "btron:/> tip_status", COLOR_WHITE);
                gterm_append_line(st, "TIP Engine: Mozc (あ) / Tibetan (EWTS) Active", COLOR_YELLOW);
            }
            redraw_all_windows();
            dump_window_rect(dev, w_term, "/tmp/btron_raw_screens/Terminal_Application.raw");
        }

        /* Cassette Application Window */
        reset_isolation_state(dev);
        WND *w_cas = open_audio_player_window();
        if (w_cas) {
            redraw_all_windows();
            dump_window_rect(dev, w_cas, "/tmp/btron_raw_screens/Cassette_Application.raw");
        }

        /* Orchestra Application Window (Live Stage DAW / MIDI Server) */
        reset_isolation_state(dev);
        WND *w_orch = open_orchestra_window();
        if (w_orch) {
            redraw_all_windows();
            dump_window_rect(dev, w_orch, "/tmp/btron_raw_screens/Orchestra_Application.raw");
        }

        /* Chat Application Window (Strict Single Roster Window) */
        reset_isolation_state(dev);
        WND *w_cht = open_isolated_chat_roster();
        if (w_cht) {
            redraw_all_windows();
            dump_window_rect(dev, w_cht, "/tmp/btron_raw_screens/Chat_Application.raw");
        }

        /* About Application Dialog Window (Isolated) */
        reset_isolation_state(dev);
        WND *w_abt = open_about_window();
        if (w_abt) {
            redraw_all_windows();
            dump_window_rect(dev, w_abt, "/tmp/btron_raw_screens/About_Application.raw");
        }

        /* Editor About Box (Isolated) */
        reset_isolation_state(dev);
        WND *w_abt_ed = open_teditor_about_window();
        if (w_abt_ed) {
            redraw_all_windows();
            dump_window_rect(dev, w_abt_ed, "/tmp/btron_raw_screens/Editor_About.raw");
        }

        /* Cabinet About Box (Isolated) */
        reset_isolation_state(dev);
        WND *w_abt_cab = open_vobj_about_window();
        if (w_abt_cab) {
            redraw_all_windows();
            dump_window_rect(dev, w_abt_cab, "/tmp/btron_raw_screens/Cabinet_About.raw");
        }

        /* TAD Browser About Box (Isolated) */
        reset_isolation_state(dev);
        WND *w_abt_brw = open_tad_browser_about_window();
        if (w_abt_brw) {
            redraw_all_windows();
            dump_window_rect(dev, w_abt_brw, "/tmp/btron_raw_screens/Browser_About.raw");
        }

        /* Terminal About Box (Isolated) */
        reset_isolation_state(dev);
        WND *w_abt_term = open_gterm_about_window();
        if (w_abt_term) {
            redraw_all_windows();
            dump_window_rect(dev, w_abt_term, "/tmp/btron_raw_screens/Terminal_About.raw");
        }

        /* Orchestra About Box (Isolated) */
        reset_isolation_state(dev);
        WND *w_abt_orch = open_orchestra_about_window();
        if (w_abt_orch) {
            redraw_all_windows();
            dump_window_rect(dev, w_abt_orch, "/tmp/btron_raw_screens/Orchestra_About.raw");
        }

        /* Cassette About Box (Isolated) */
        reset_isolation_state(dev);
        WND *w_abt_cas = open_cassette_about_window();
        if (w_abt_cas) {
            redraw_all_windows();
            dump_window_rect(dev, w_abt_cas, "/tmp/btron_raw_screens/Cassette_About.raw");
        }
    }

    /* 3. In-App Opened Menu Screenshots (_Menu_Opened suffix) */
    {
        /* Editor File Menu Opened with Live Document Content */
        reset_isolation_state(dev);
        WND *w_ted = open_t_editor_window();
        if (w_ted) {
            redraw_all_windows();
            simulate_menu_click(w_ted, 0); /* File Menu (ファイル) */
            redraw_all_windows();
            dump_window_rect(dev, w_ted, "/tmp/btron_raw_screens/Editor_Menu_Opened.raw");
        }

        /* Cabinet File Menu Opened with Real Body Objects */
        reset_isolation_state(dev);
        WND *w_cab = open_vobj_manager_window();
        if (w_cab) {
            redraw_all_windows();
            simulate_menu_click(w_cab, 0); /* File Menu (ファイル) */
            redraw_all_windows();
            dump_window_rect(dev, w_cab, "/tmp/btron_raw_screens/Cabinet_Menu_Opened.raw");
        }

        /* TAD Browser File Menu Opened with Live Specification Document */
        reset_isolation_state(dev);
        WND *w_brw = open_tad_browser_window("tad_bin/01_btron3_spec.tad", "【仕様書】BTRON3 3.20 OS Specification");
        if (w_brw) {
            redraw_all_windows();
            simulate_menu_click(w_brw, 0); /* File Menu (ファイル) */
            redraw_all_windows();
            dump_window_rect(dev, w_brw, "/tmp/btron_raw_screens/Browser_Menu_Opened.raw");
        }

        /* Terminal File Menu Opened with Command Shell Output */
        reset_isolation_state(dev);
        WND *w_term = open_gterm_window();
        if (w_term) {
            GTermState *st = (GTermState*)(uintptr_t)w_term->user_data;
            if (st) {
                gterm_append_line(st, "btron:/> uname -a", COLOR_WHITE);
                gterm_append_line(st, "B-System 3.20 SMP (EMT64/ACPI) #1 SMP x86_64", COLOR_GREEN);
                gterm_append_line(st, "btron:/> tip_status", COLOR_WHITE);
                gterm_append_line(st, "TIP Engine: Mozc (あ) / Tibetan (EWTS) Active", COLOR_YELLOW);
            }
            redraw_all_windows();
            simulate_menu_click(w_term, 0); /* File Menu (ファイル) */
            redraw_all_windows();
            dump_window_rect(dev, w_term, "/tmp/btron_raw_screens/Terminal_Menu_Opened.raw");
        }
    }

    /* 4. Desktop BTRON System Menu Bar & Opened Main Menu Overlays */
    {
        /* Top Menu Bar Closed (Idle) */
        reset_isolation_state(dev);
        global_menu_init();
        global_menu_render_bar(dev);
        dump_raw_region(dev, 0, 0, 1280, 25, "Global System Menu Bar", "/tmp/btron_raw_screens/GlobalMenu.raw");

        /* Desktop BTRON Main Menu Opened ([BTRON] Tracker / Deskbar Hub) */
        reset_isolation_state(dev);
        global_menu_init();
        global_menu_render_bar(dev);
        global_menu_handle_mouse_down(20, 10); /* Header 0: [BTRON] */
        global_menu_render_overlay(dev);
        dump_raw_region(dev, 0, 0, 360, 480, "Desktop BTRON Main Menu (Opened)", "/tmp/btron_raw_screens/Desktop_MainMenu_Opened.raw");

        /* Global Menu System Dropdown Opened (システム(S)) */
        reset_isolation_state(dev);
        global_menu_init();
        global_menu_render_bar(dev);
        global_menu_handle_mouse_down(120, 10); /* Header 1: システム(S) */
        global_menu_render_overlay(dev);
        dump_raw_region(dev, 70, 0, 290, 280, "Global Menu - System Dropdown", "/tmp/btron_raw_screens/GlobalMenu_System_Opened.raw");

        /* Global Menu Window Dropdown Opened (ウィンドウ(W)) */
        reset_isolation_state(dev);
        global_menu_init();
        global_menu_render_bar(dev);
        global_menu_handle_mouse_down(300, 10); /* Header 3: ウィンドウ(W) */
        global_menu_render_overlay(dev);
        dump_raw_region(dev, 250, 0, 290, 280, "Global Menu - Window Dropdown", "/tmp/btron_raw_screens/GlobalMenu_Window_Opened.raw");
    }

    /* 5. Full Desktop After Load with Opened BTRON Main Menu */
    {
        reset_isolation_state(dev);
        tracker_init();
        global_menu_init();
        open_vobj_manager_window();
        open_t_editor_window();
        open_gterm_window();
        render_desktop_background(dev);
        redraw_all_windows();
        render_system_panel(dev);
        global_menu_handle_mouse_down(20, 10); /* Open ［BTRON］ Deskbar Tracker Menu */
        if (global_menu_is_open()) {
            global_menu_render_overlay(dev);
        }
        dump_raw_region(dev, 0, 0, 1280, 800, "Full Desktop with Opened BTRON Menu", "/tmp/btron_raw_screens/Desktop_Full.raw");
    }

    /* 6. Language Page — TIP Dictionary Candidate Window Showcase */
    {
        /* 6a. Tibetan EWTS mode: T-Editor with Heart Sutra open.
         *     Type EWTS prefix "cho" â Tab opens candidate popup showing:
         *     à½à½¼à½¦ (chos/dharma), à½à½¼à½¦à¼à½à½²à½ (dharmata), à½à½¼à½¦à¼à½¦à½à½´ (dharmakaya), ...
         *     Background document: assets/texts/Heart_Sutra_Tibetan.txt */
        reset_isolation_state(dev);
        tip_init();
        tip_set_mode(TIP_MODE_TIBETAN);
        WND *w_tib = open_t_editor_window_with_file(
                         "assets/texts/Heart_Sutra_Tibetan.txt");
        if (w_tib) {
            w_tib->focused = TRUE;
            TEditor *ed_tib = (TEditor*)(uintptr_t)w_tib->user_data;
            if (ed_tib) {
                ed_tib->cursor_row = (ed_tib->total_lines > 6) ? 6 : 0;
                ed_tib->cursor_col = 0;
            }
            char dummy[64];
            tip_process_key('c', 0, dummy, sizeof(dummy));
            tip_process_key('h', 0, dummy, sizeof(dummy));
            tip_process_key('o', 0, dummy, sizeof(dummy));
            tip_process_key('\t', 0, dummy, sizeof(dummy));
            tip_set_caret_pos(
                w_tib->bounds.left + 46,
                w_tib->bounds.top  + 24 + 6 * 18);
            redraw_all_windows();
            tip_render_candidate_window(
                dev, tip_get_caret_x(), tip_get_caret_y());
            dump_window_rect(dev, w_tib,
                "/tmp/btron_raw_screens/Language_TIP_Tibetan.raw");
        }

        /* 6b. Japanese Mozc mode: T-Editor with BTRON3 Report open.
         *     Romaji "hotoke" â Space opens candidate popup showing:
         *     ä» / ã»ã¨ã / ä»ãã / ä»æ§  (maximum suggestion count) */
        reset_isolation_state(dev);
        tip_init();
        tip_set_mode(TIP_MODE_HIRAGANA);
        WND *w_jp = open_t_editor_window_with_file(
                        "assets/texts/BTRON3_Report.txt");
        if (w_jp) {
            w_jp->focused = TRUE;
            TEditor *ed_jp = (TEditor*)(uintptr_t)w_jp->user_data;
            if (ed_jp) {
                ed_jp->cursor_row = (ed_jp->total_lines > 4) ? 4 : 0;
                ed_jp->cursor_col = 0;
            }
            char dummy2[64];
            tip_process_key('h', 0, dummy2, sizeof(dummy2));
            tip_process_key('o', 0, dummy2, sizeof(dummy2));
            tip_process_key('t', 0, dummy2, sizeof(dummy2));
            tip_process_key('o', 0, dummy2, sizeof(dummy2));
            tip_process_key('k', 0, dummy2, sizeof(dummy2));
            tip_process_key('e', 0, dummy2, sizeof(dummy2));
            tip_process_key(' ', 0, dummy2, sizeof(dummy2));
            tip_set_caret_pos(
                w_jp->bounds.left + 46,
                w_jp->bounds.top  + 24 + 4 * 18);
            redraw_all_windows();
            tip_render_candidate_window(
                dev, tip_get_caret_x(), tip_get_caret_y());
            dump_window_rect(dev, w_jp,
                "/tmp/btron_raw_screens/Language_TIP_Japanese.raw");
        }
    }

    cls_dev(dev);
    printf("==========================================================\n");
    printf(" Successfully captured all real BTRON windows & menus!\n");
    printf("==========================================================\n");
    return 0;
}
