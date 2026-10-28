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
#include <btron/itron.h>
#include <btron/settings.h>
#include <btron/language_settings.h>
#include <btron/app_menu.h>
#include <btron/pmc.h>
#include <btron/tip.h>
#include <btron/t_editor.h>
#include <device/virtio.h>
/* The desk's own private header: it declares the seven window openers, the
 * shared painters' helpers and orch_inval_all(), and it pulls in
 * <apps/orchestra.h> -> <media/gst_rt.h> for OrchView.  Declaring the openers
 * again here would be a second copy of a signature that orchestra_*.c already
 * owns, which is how the desk drifts. */
#include "../media/orchestra_desk.h"

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
extern WND* open_gterm_window_rect(H x, H y, H w, H h, UW attr);
extern WND* open_about_window(void);
extern WND* open_teditor_about_window(void);
extern WND* open_vobj_about_window(void);
extern WND* open_tad_browser_about_window(void);
extern WND* open_gterm_about_window(void);
extern WND* open_orchestra_about_window(void);
extern WND* open_cassette_about_window(void);
extern WND* open_drivesetup_window(void);
extern WND* open_drivesetup_about_window(void);
extern WND* open_t_editor_window_with_file(const char *filepath);
extern WND* open_clarity_window(void);
extern WND* open_paint_window(void);
extern WND* open_paint_window_with_file(const char *filepath);
extern WND* open_paint_about_window(void);
extern WND* open_msx_window_with_rom(const char *path);
extern void msx_app_fast_forward(int frames);
extern WND* open_kagee_window(void);
extern H    tip_get_caret_x(void);
extern H    tip_get_caret_y(void);
extern WND* open_review_window(void);
extern WND* open_review_about_window(void);

/* Forward declarations for GTerm internals */
typedef struct GTermState GTermState;
extern void gterm_append_line(GTermState *st, const char *text, COLOR col);
extern void gterm_exec(WND *wnd, const char *cmd_line);

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
void* SDL_CreateSystemCursor(int id) { (void)id; return NULL; }
void  SDL_SetCursor(void *cursor) { (void)cursor; }
ER init_evt_sys(void) { return E_OK; }
ER get_evt(EVT *p_evt, W timeout_ms) { (void)p_evt; (void)timeout_ms; return E_TMOUT; }
ER snd_evt(const EVT *p_evt) { (void)p_evt; return E_OK; }
ER init_vobj_sys(const char *storage_root) { (void)storage_root; return E_OK; }
/* Minimal VOBJ stubs for clarity_export.c in headless build */
#include <btron/vobj.h>
static ROBJ g_stub_robj;
ROBJ* cre_robj(const char *name, VOBJ_TYPE type) { (void)name; (void)type; return &g_stub_robj; }
ROBJ* opn_robj(ID robj_id) { (void)robj_id; return &g_stub_robj; }
ER    cls_robj(ROBJ *robj) { (void)robj; return E_OK; }
ER    wr_vobj_data(ROBJ *robj, const void *buf, UW len) { (void)robj; (void)buf; (void)len; return E_OK; }
ER    rd_vobj_data(ROBJ *robj, void *buf, UW len, UW *rb) { (void)robj; (void)buf; (void)len; if (rb) *rb = 0; return E_OK; }
VOBJ_LINK* cre_vobj_link(ID t, const char *l, H x, H y) { (void)t; (void)l; (void)x; (void)y; return NULL; }
ROBJ* find_robj_by_path(const char *path) { (void)path; return NULL; }

/* The Cho-Kanji launcher entry points referenced by the global menu's execute
 * switch arrive as hard undefined symbols on macOS when chokanji.c isn't linked.
 * The capture pipeline never opens those windows, so provide null entry points. */
static WND* cab_no_win(void) { return NULL; }
WND* open_chokanji_cabinet_window(void)     { return cab_no_win(); }
WND* open_chokanji_doc_window(void)         { return cab_no_win(); }
WND* open_chokanji_microscript_window(void) { return cab_no_win(); }
WND* open_chokanji_clock_window(void)       { return cab_no_win(); }
WND* open_chokanji_kconv_window(void)       { return cab_no_win(); }
WND* open_chokanji_xfconv_window(void)      { return cab_no_win(); }
WND* open_chokanji_unpack_window(void)      { return cab_no_win(); }

/* Real OpenGL applications: XMB and Lil Cu 64 Demoscene */
extern WND* open_xmb_window(void);
extern WND* open_lilcu64_demo_window(void);
void uart_puts_raw(const char *s) { (void)s; }
ER wup_tsk(ID tskid) { (void)tskid; return E_OK; }
static SYSTIME s_sim_clock = 1000;
ER get_tim(SYSTIME *p_time) {
    if (p_time) {
        s_sim_clock += 16;
        *p_time = s_sim_clock;
    }
    return E_OK;
}

/* B-MSX paces its emulator with a 60 Hz task.  There is no task layer in this
 * link, so cre_tsk() fails the way it does on a target with no task slots left,
 * and the window falls back to one machine frame per repaint — which is exactly
 * what lets the loop below boot a cartridge to its title screen. */
ID cre_tsk(const T_CTSK *pk_ctsk) { (void)pk_ctsk; return -1; }
ER sta_tsk(ID tskid, VW exinf)    { (void)tskid; (void)exinf; return E_OK; }
void dly_tsk(W dlytim)            { (void)dlytim; }

/* Kagee will not create its window when this returns failure. The weak
 * stub in lilcu64_synth.c returns -1. virtio_sound_is_ready stays that
 * stub's false, so the MSX pump still drops its block and queues nothing. */
int virtio_sound_open(uint32_t sample_rate, uint8_t channels)
{
    (void)sample_rate;
    (void)channels;
    return 0;
}

/* GTerm's `ps` walks the kernel's task table.  src/cores/core_init.c owns the walking and the
 * row formatting and is not in this link, so answer with the true thing -- "no table to walk"
 * -- rather than the false thing an empty table would say. */
W btron_task_row_text(W idx, char *out, W cap) { (void)idx; (void)out; (void)cap; return BTRON_TSK_NOWALK; }

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

/* Deliver one key-down to a window's handler, then repaint it. */
static void send_key(WND *wnd, UW key) {
    if (!wnd || !wnd->event_handler) return;
    EVT evt;
    memset(&evt, 0, sizeof(EVT));
    evt.type = EV_KEY_DOWN;
    evt.key = key;
    wnd->event_handler(wnd, &evt);
    inval_wnd(wnd);
    redraw_all_windows();
}

/* Open the first regular /SYS file in BE (via SC, F4), overwrite its bytes with
 * `code` by typing hex digits in BE's hex editor (the file on disk is never
 * saved), then pick architecture/bitness through the F2 dialog.  SC lists
 * directories first, so the file is the fifth entry (.., TRASH, cbios, msx, file).
 * arch_key/bits_key are the dialog's digit shortcuts.  With `asm_view` the
 * dialog is confirmed and BE switches to disassembly (F4); otherwise the dialog
 * stays open over the hex view.  `code` is typed `reps` times so the
 * disassembly fills every terminal row; it must outgrow the 22-byte file. */
static void capture_be(GDEV *dev, H x, H y, H w, H h, UW attr,
                       const unsigned char *code, size_t len, int reps,
                       int arch_key, int bits_key, BOOL asm_view, const char *raw_out) {
    static const char hexd[] = "0123456789abcdef";
    reset_isolation_state(dev);
    WND *wnd = open_gterm_window_rect(x, y, w, h, attr);
    if (!wnd) return;
    gterm_exec(wnd, "sc");
    for (int i = 0; i < 4; i++) send_key(wnd, BTRON_KEY_DOWN);
    send_key(wnd, BTRON_KEY_F4);                     /* SC: edit in BE */
    for (int r = 0; r < reps; r++) {
        for (size_t i = 0; i < len; i++) {
            send_key(wnd, (UW)hexd[code[i] >> 4]);
            send_key(wnd, (UW)hexd[code[i] & 15]);
        }
    }
    send_key(wnd, BTRON_KEY_PAGE_UP);                /* back to offset 0 */
    send_key(wnd, BTRON_KEY_HOME);
    send_key(wnd, BTRON_KEY_F2);                     /* BE: architecture dialog */
    send_key(wnd, (UW)arch_key);
    send_key(wnd, BTRON_KEY_TAB);
    send_key(wnd, (UW)bits_key);
    if (asm_view) {
        send_key(wnd, BTRON_KEY_RETURN);
        send_key(wnd, BTRON_KEY_F4);                 /* BE: hex -> disassembly */
    }
    dump_window_rect(dev, wnd, raw_out);
    cls_wnd(wnd);
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

/* ── Orchestra: the whole stage desk shown as one composition ──────────────
 *
 * Every Orchestra window below is captured with the desk album's first track
 * loaded: SYS/bells.orch, which is "鐘のキャロル (Carol of the Bells)",
 * 168 BPM, 3/4 — src/tools/album_encode.c names it piece 1 and then copies the
 * file onto SYS/orchestra.orch, so orch_cmd_open_at(1) loads exactly what the
 * desk boots with.  Opening reads through the host filesystem, and pick_host()
 * falls back from /SYS to SYS/, so `make screenshots` has to run at the repo
 * root; if it cannot read the 73408-byte session the load is refused and
 * v->refuse_session says so.
 *
 * The playhead needs the transport to read as playing.  Piano, Arrange and
 * Score chase it in time_follow(), which returns early unless v->state is
 * ORCH_GST_PLAYING — parked at the middle of the piece with the view scrolled
 * to the start would paint no yellow bar and no notes from the middle of the
 * song.  This link has no audio backend (virtio_sound_open() is a stub above
 * and virtio_sound_is_ready() is false), which is why gst_rt_set_state() would
 * refuse PLAYING here; the state is therefore written directly, inside the same
 * seqlock pair the desk uses for every other mutation of the view.  Nothing
 * starts rendering — only the position moves, which is the whole picture.
 *
 * Piano and Score show one part at a time — the selected one.  At the exact
 * midpoint of this arrangement every chime voice is holding a single long note,
 * so those two windows are captured with the voice that is actually moving
 * there: track 13, "Crystal step", the waddle bass (E2–B2–E3), one of the four
 * signature shims of the piece.  The other windows stay on the session default,
 * track 0, because the Arrange window centres its lane list on the selected
 * track and the eight "Crystal chime" canon lanes are the picture to show.
 * The part each shot uses is the third column of the table in
 * capture_orchestra_desk(), which is where the selection is applied. */
static int orchestra_load_carol(void) {
    OrchView *v;
    uint32_t half;

    if (!open_orchestra_window()) return 0;   /* boots gst_rt, builds the menu, opens the stage */
    if (!orch_cmd_open_at(1)) {
        fprintf(stderr, "[ORCHESTRA] bells.orch refused (cwd is not the repo root?)\n");
        return 0;
    }
    v = gst_view();
    if (!v) return 0;
    orchestra_ui_tick();                      /* publishes loop_tick = span of the last note */
    half = v->loop_tick / 2u;
    gst_seq_begin();
    v->npos = gst_sample_of_tick(half, v->sample_rate, v->tempo, v->den);
    v->state = ORCH_GST_PLAYING;
    gst_seq_end();
    /* The stage's status line is a snapshot of the view, so it has to be
     * republished after the state write or the transport button and the text
     * under it disagree.  orch_publish_idle() is the desk's own formatter. */
    orch_publish_idle();
    orch_inval_all();
    printf("[ORCHESTRA] Carol of the Bells: %u tracks, %u events, playhead tick %u of %u\n",
           (unsigned)v->track_count, (unsigned)v->event_count, half, (unsigned)v->loop_tick);
    return 1;
}

static void capture_orchestra_desk(GDEV *dev) {
    typedef WND *(*OrchOpener)(void);

    /* The window, the file it lands in, and the part the desk is left on for
     * that shot.  Piano and Score paint the selected part and nothing else, so
     * they take track 13 — the waddle bass, the voice still moving at the
     * midpoint of the piece.  The rest keep track 0: it is the session default,
     * and row_reveal() in orchestra_arrange.c only re-centres the lane list when
     * the selection leaves the visible window, so track 0 is what keeps the
     * eight "Crystal chime" canon lanes on screen. */
    static const struct {
        OrchOpener open;
        const char *name;
        int part;
    } shot[] = {
        { orch_open_stage,   "Orchestra_Application",  0 },
        { orch_open_arrange, "Orchestra_Arrange",      0 },
        { orch_open_piano,   "Orchestra_Piano",       13 },
        { orch_open_score,   "Orchestra_Score",       13 },
        { orch_open_mix,     "Orchestra_Mix",          0 },
        { orch_open_insert,  "Orchestra_Insert",       0 }
    };
    enum { ORCH_SHOTS = 6 };
    OrchView *v;

    if (!orchestra_load_carol()) return;
    v = gst_view();
    if (!v) return;

    for (int i = 0; i < ORCH_SHOTS; i++) {
        char path[96];
        WND *w;

        /* One window per canvas.  dump_window_rect() crops the framebuffer, and
         * the desk windows cascade over each other, so two of them in the same
         * shot would overwrite.  The stage is the exception: it is already open
         * and registered by orchestra_load_carol(), and reset_isolation_state()
         * clears the registry without running on_destroy, which would leave a
         * later orch_open_stage() returning that stale, unregistered window
         * instead of building a paintable one.  So each module is opened exactly
         * once for the whole run, and only the windows after the stage get a
         * fresh canvas.  The About box is captured with the other About dialogs. */
        if (i > 0) reset_isolation_state(dev);
        if (v->selected != shot[i].part) {
            orch_desk_sel(shot[i].part);
            orch_inval_all();
        }
        w = shot[i].open();
        if (!w) {
            fprintf(stderr, "[ORCHESTRA] %s did not open\n", shot[i].name);
            continue;
        }
        redraw_all_windows();
        snprintf(path, sizeof(path), "/tmp/btron_raw_screens/%s.raw", shot[i].name);
        printf("[ORCHESTRA] %s: part %d\n", shot[i].name, (int)v->selected);
        dump_window_rect(dev, w, path);
    }
}

/* Cartridges the B-MSX gallery shows. The first row also writes the bare
 * application shot and the opened file menu. Frames are the title card or
 * the first readable scene; one paint after fast-forward does not move them.
 * Kagee is the separate 96x64 player. Paint decodes one video frame on every
 * even tick, so 1601 paints lands on decoded frame 801. */
static void capture_msx_windows(GDEV *dev) {
    static const struct {
        const char *rom;
        const char *out_path;
        int frames;
    } s_msx_games[] = {
        { "BULLRUSH.ROM", "/tmp/btron_raw_screens/MSX_Bullrush.raw", 450 },
        { "NEON_REVENANT-HC-Geo3D-256-v0.2.0.rom", "/tmp/btron_raw_screens/MSX_NeonRevenant.raw", 450 },
        { "NIGHT_RAVEN_GEO3D.ROM", "/tmp/btron_raw_screens/MSX_NightRaven.raw", 450 },
        { "xracing_v1.0.4.rom", "/tmp/btron_raw_screens/MSX_XRacing.raw", 800 },
        { "Bad Apple (MDPC)[ASCII16].rom", "/tmp/btron_raw_screens/MSX_BadApple.raw", 800 },
        { "Dynamite Go! Go! - Demo (2021) (Rutubo Gameworks).ROM", "/tmp/btron_raw_screens/MSX_DynamiteGoGo.raw", 900 },
        { "YAZZIER.ROM", "/tmp/btron_raw_screens/MSX_Yazzier.raw", 600 },
        { "c0man.rom", "/tmp/btron_raw_screens/MSX_C0man.raw", 450 }
    };

    for (size_t gi = 0; gi < sizeof(s_msx_games)/sizeof(s_msx_games[0]); gi++) {
        reset_isolation_state(dev);
        WND *gw = open_msx_window_with_rom(s_msx_games[gi].rom);
        if (gw) {
            msx_app_fast_forward(s_msx_games[gi].frames);
            inval_wnd(gw);
            redraw_all_windows();
            dump_window_rect(dev, gw, s_msx_games[gi].out_path);

            if (gi == 0) {
                dump_window_rect(dev, gw, "/tmp/btron_raw_screens/B_MSX_Application.raw");
                simulate_menu_click(gw, 0); /* ファイル(F): the ROM cascade */
                redraw_all_windows();
                dump_window_rect(dev, gw, "/tmp/btron_raw_screens/B_MSX_Menu_Opened.raw");
            }
            cls_wnd(gw);
        } else {
            fprintf(stderr, "Error opening B-MSX cartridge: %s\n", s_msx_games[gi].rom);
        }
    }

    reset_isolation_state(dev);
    WND *kw = open_kagee_window();
    if (kw) {
        for (int i = 0; i < 1601; i++) {
            inval_wnd(kw);
            redraw_all_windows();
        }
        dump_window_rect(dev, kw, "/tmp/btron_raw_screens/Kagee_BadApple.raw");
        cls_wnd(kw);
    } else {
        fprintf(stderr, "Error opening Kagee window\n");
    }
}

int main(int argc, char **argv) {
    int msx_only = 0;
    for (int i = 1; i < argc; i++) {
        if (argv[i] && strcmp(argv[i], "--msx") == 0) msx_only = 1;
    }

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

    if (msx_only) {
        capture_msx_windows(dev);
        cls_dev(dev);
        printf("==========================================================\n");
        printf(" Captured B-MSX cartridges and the Kagee window.\n");
        printf("==========================================================\n");
        return 0;
    }

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
            cls_wnd(w_term);
        }

        /* Commander suite window geometry: 1120 px wide (140 text columns at 8 px,
         * fits the 1280x800 canvas with a 70 px margin) and 475 px tall
         * (previous 396 px x 1.2). */
        const H CMD_X = 70, CMD_Y = 150, CMD_W = 1120, CMD_H = 475;
        const UW CMD_ATTR = WND_ATTR_TITLE | WND_ATTR_CLOSE | WND_ATTR_BORDER | WND_ATTR_RESIZE;

        /* Sokhatsky Commander (SC) with the F10 Quit dialog opened */
        reset_isolation_state(dev);
        WND *w_sc = open_gterm_window_rect(CMD_X, CMD_Y, CMD_W, CMD_H, CMD_ATTR);
        if (w_sc) {
            gterm_exec(w_sc, "sc");
            send_key(w_sc, BTRON_KEY_F10);
            dump_window_rect(dev, w_sc, "/tmp/btron_raw_screens/Commander_SC.raw");
            cls_wnd(w_sc);
        }

        /* Terminal Vision (TV) with the F2 Find dialog opened */
        reset_isolation_state(dev);
        WND *w_tv = open_gterm_window_rect(CMD_X, CMD_Y, CMD_W, CMD_H, CMD_ATTR);
        if (w_tv) {
            gterm_exec(w_tv, "tv /SYS/Heart_Sutra_Tibetan.txt");
            send_key(w_tv, BTRON_KEY_F2);
            dump_window_rect(dev, w_tv, "/tmp/btron_raw_screens/Commander_TV.raw");
            cls_wnd(w_tv);
        }

        /* Binary Editor (BE, launched from SC via F4).  Each shot types a small
         * machine-code sample into the first regular file in /SYS opened in BE,
         * selects the architecture/bitness through the F2 dialog and either leaves
         * the dialog open or switches to disassembly (F4). */
        static const unsigned char x64_code[] = {
            0x55, 0x48,0x89,0xE5, 0x48,0x83,0xEC,0x10, 0x89,0x7D,0xFC, 0x8B,0x45,0xFC,
            0x83,0xC0,0x01, 0x89,0x45,0xF8, 0x48,0x8D,0x3D,0x10,0x00,0x00,0x00,
            0xE8,0x00,0x00,0x00,0x00, 0x31,0xC0, 0x48,0x01,0xD8, 0x0F,0x05, 0x90,
            0xB8,0x2A,0x00,0x00,0x00, 0x48,0x89,0xC7, 0xC9, 0xC3
        };
        static const unsigned char a32_code[] = {   /* AArch32 (A32), little endian */
            0x00,0x48,0x2D,0xE9, 0x04,0xB0,0x8D,0xE2, 0x2A,0x00,0xA0,0xE3, 0x01,0x00,0x80,0xE2,
            0x00,0x00,0x00,0xEB, 0x00,0x30,0xA0,0xE1, 0x00,0xF0,0x20,0xE3, 0x04,0xD0,0x4B,0xE2,
            0x00,0x88,0xBD,0xE8, 0x1E,0xFF,0x2F,0xE1
        };
        static const unsigned char rv_code[] = {
            0x13,0x01,0x01,0xFF, 0x23,0x34,0x11,0x00, 0x13,0x05,0xA0,0x02, 0x33,0x05,0xB5,0x00,
            0xEF,0x00,0x00,0x00, 0x83,0x30,0x81,0x00, 0x13,0x01,0x01,0x01, 0x67,0x80,0x00,0x00
        };
        capture_be(dev, CMD_X, CMD_Y, CMD_W, CMD_H, CMD_ATTR, x64_code, sizeof(x64_code), 3,
                   '1', '3', FALSE, "/tmp/btron_raw_screens/Commander_BE.raw");
        capture_be(dev, CMD_X, CMD_Y, CMD_W, CMD_H, CMD_ATTR, x64_code, sizeof(x64_code), 3,
                   '1', '4', TRUE,  "/tmp/btron_raw_screens/Commander_BE_X64.raw");
        capture_be(dev, CMD_X, CMD_Y, CMD_W, CMD_H, CMD_ATTR, a32_code, sizeof(a32_code), 4,
                   '2', '3', TRUE,  "/tmp/btron_raw_screens/Commander_BE_ARM.raw");
        capture_be(dev, CMD_X, CMD_Y, CMD_W, CMD_H, CMD_ATTR, rv_code, sizeof(rv_code), 4,
                   '3', '4', TRUE,  "/tmp/btron_raw_screens/Commander_BE_RISCV.raw");

        /* Cassette Application Window */
        reset_isolation_state(dev);
        WND *w_cas = open_audio_player_window();
        if (w_cas) {
            redraw_all_windows();
            dump_window_rect(dev, w_cas, "/tmp/btron_raw_screens/Cassette_Application.raw");
        }

        /* Orchestra stage desk (Live DAW / MIDI server): six windows captured one
         * at a time, each on the album's first composition with the playhead
         * parked mid-piece. */
        reset_isolation_state(dev);
        capture_orchestra_desk(dev);

        /* Cross Media Bar (XMB) OpenGL Application Window */
        reset_isolation_state(dev);
        WND *w_xmb = open_xmb_window();
        if (w_xmb) {
            for (int f = 0; f < 30; f++) {
                w_xmb->img_valid = FALSE;
                redraw_all_windows();
            }
            dump_window_rect(dev, w_xmb, "/tmp/btron_raw_screens/XMB_Application.raw");
            cls_wnd(w_xmb);
        }

        /* Lil Cu 64 Demoscene Window (Hopf Field & 3D Mascot) */
        reset_isolation_state(dev);
        WND *w_demo = open_lilcu64_demo_window();
        if (w_demo) {
            redraw_all_windows();
            dump_window_rect(dev, w_demo, "/tmp/btron_raw_screens/Demoscene_Application.raw");
            cls_wnd(w_demo);
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

        /* DriveSetup / Volumes Application Window (Isolated) */
        reset_isolation_state(dev);
        WND *w_ds = open_drivesetup_window();
        if (w_ds) {
            redraw_all_windows();
            dump_window_rect(dev, w_ds, "/tmp/btron_raw_screens/DriveSetup_Application.raw");
        }

        /* DriveSetup About Box (Isolated) */
        reset_isolation_state(dev);
        WND *w_abt_ds = open_drivesetup_about_window();
        if (w_abt_ds) {
            redraw_all_windows();
            dump_window_rect(dev, w_abt_ds, "/tmp/btron_raw_screens/DriveSetup_About.raw");
        }

        /* Clarity DTP Application Window (Isolated) */
        reset_isolation_state(dev);
        WND *w_clarity = open_clarity_window();
        if (w_clarity) {
            redraw_all_windows();
            dump_window_rect(dev, w_clarity, "/tmp/btron_raw_screens/Clarity_Application.raw");
        }

        /* Paint Image Viewer Application Window (Isolated, live decoded image) */
        reset_isolation_state(dev);
        WND *w_paint = open_paint_window();
        if (w_paint) {
            redraw_all_windows();
            dump_window_rect(dev, w_paint, "/tmp/btron_raw_screens/Paint_Application.raw");
        }

        /* Paint About Box (Isolated) */
        reset_isolation_state(dev);
        WND *w_abt_paint = open_paint_about_window();
        if (w_abt_paint) {
            redraw_all_windows();
            dump_window_rect(dev, w_abt_paint, "/tmp/btron_raw_screens/Paint_About.raw");
        }

        /* Review PDF Application Window (Isolated, live vector PDF + Marginalia) */
        reset_isolation_state(dev);
        WND *w_review = open_review_window();
        if (w_review) {
            redraw_all_windows();
            dump_window_rect(dev, w_review, "/tmp/btron_raw_screens/Review_Application.raw");
        }

        /* Review About Box (Isolated) */
        reset_isolation_state(dev);
        WND *w_abt_rev = open_review_about_window();
        if (w_abt_rev) {
            redraw_all_windows();
            dump_window_rect(dev, w_abt_rev, "/tmp/btron_raw_screens/Review_About.raw");
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

        /* DriveSetup Disk Menu Opened with Partition Actions */
        reset_isolation_state(dev);
        WND *w_ds_menu = open_drivesetup_window();
        if (w_ds_menu) {
            redraw_all_windows();
            /* Header 1 (ディスク(D)) starts at x=110, width=100. Center is ~160 */
            EVT evt;
            memset(&evt, 0, sizeof(EVT));
            evt.type = EV_BUT_DOWN;
            evt.pos.x = w_ds_menu->client.left + 160;
            evt.pos.y = w_ds_menu->client.top + 12;
            w_ds_menu->event_handler(w_ds_menu, &evt);
            redraw_all_windows();
            dump_window_rect(dev, w_ds_menu, "/tmp/btron_raw_screens/DriveSetup_Menu_Opened.raw");
        }
        /* Clarity Arrange Menu Opened */
        reset_isolation_state(dev);
        WND *w_clr_m = open_clarity_window();
        if (w_clr_m) {
            redraw_all_windows();
            simulate_menu_click(w_clr_m, 4); /* Arrange menu (配置(A)) */
            redraw_all_windows();
            dump_window_rect(dev, w_clr_m, "/tmp/btron_raw_screens/Clarity_Menu_Opened.raw");
        }

        /* Paint File Menu Opened (ファイル: 開く cascade) with Live Image */
        reset_isolation_state(dev);
        WND *w_paint_m = open_paint_window();
        if (w_paint_m) {
            redraw_all_windows();
            simulate_menu_click(w_paint_m, 0); /* File Menu (ファイル) */
            redraw_all_windows();
            dump_window_rect(dev, w_paint_m, "/tmp/btron_raw_screens/Paint_Menu_Opened.raw");
        }

        /* Review File Menu Opened with Live Document Content */
        reset_isolation_state(dev);
        WND *w_rev_m = open_review_window();
        if (w_rev_m) {
            redraw_all_windows();
            simulate_menu_click(w_rev_m, 0); /* File Menu (ファイル) */
            redraw_all_windows();
            dump_window_rect(dev, w_rev_m, "/tmp/btron_raw_screens/Review_Menu_Opened.raw");
        }

        capture_msx_windows(dev);
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

    /* 7. Cho-Kanji (PMC 3D-Bevel) styled chrome of representative windows.
     *    Flipping g_wm_style re-chromes ANY window via the PMC branch in
     *    draw_retro_window_frame() â no dedicated chokanji app modules needed. */
    {
        pmc_set_style(WM_STYLE_CHOKANJI);

        reset_isolation_state(dev);
        WND *ck_paint = open_paint_window();
        if (ck_paint) {
            redraw_all_windows();
            dump_window_rect(dev, ck_paint, "/tmp/btron_raw_screens/Paint_Chokanji.raw");
        }

        reset_isolation_state(dev);
        WND *ck_cab = open_vobj_manager_window();
        if (ck_cab) {
            redraw_all_windows();
            dump_window_rect(dev, ck_cab, "/tmp/btron_raw_screens/Cabinet_Chokanji.raw");
        }

        reset_isolation_state(dev);
        WND *ck_ed = open_t_editor_window();
        if (ck_ed) {
            redraw_all_windows();
            dump_window_rect(dev, ck_ed, "/tmp/btron_raw_screens/Editor_Chokanji.raw");
        }

        reset_isolation_state(dev);
        WND *ck_term = open_gterm_window();
        if (ck_term) {
            GTermState *st = (GTermState*)(uintptr_t)ck_term->user_data;
            if (st) {
                gterm_append_line(st, "btron:/> uname -a", COLOR_WHITE);
                gterm_append_line(st, "B-System 3.20 (Cho-Kanji PMC)", COLOR_GREEN);
            }
            redraw_all_windows();
            dump_window_rect(dev, ck_term, "/tmp/btron_raw_screens/Terminal_Chokanji.raw");
        }

        reset_isolation_state(dev);
        WND *ck_appearance = open_appearance_settings_window();
        if (ck_appearance) {
            redraw_all_windows();
            dump_window_rect(dev, ck_appearance, "/tmp/btron_raw_screens/Appearance_Chokanji.raw");
        }

        reset_isolation_state(dev);
        WND *ck_about = open_about_window();
        if (ck_about) {
            redraw_all_windows();
            dump_window_rect(dev, ck_about, "/tmp/btron_raw_screens/About_Chokanji.raw");
        }

        /* Paint File menu opened, in Cho-Kanji chrome */
        reset_isolation_state(dev);
        WND *ck_paint_m = open_paint_window();
        if (ck_paint_m) {
            redraw_all_windows();
            simulate_menu_click(ck_paint_m, 0); /* File menu */
            redraw_all_windows();
            dump_window_rect(dev, ck_paint_m, "/tmp/btron_raw_screens/Paint_Menu_Opened_Chokanji.raw");
        }

        pmc_set_style(WM_STYLE_BEOS);
    }

    cls_dev(dev);
    printf("==========================================================\n");
    printf(" Successfully captured all real BTRON windows & menus!\n");
    printf("==========================================================\n");
    return 0;
}
