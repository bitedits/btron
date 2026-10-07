/*
 * B-System (BTRON 3.20) B-MSX — MSX1 cartridge emulator window.
 * src/apps/msx_app.c — host shell for the tronMSX core (include/emulators/
 * tronmsx.h).  The machine is stepped, rendered and fed input from the window
 * callbacks only, i.e. always on the thread that owns the core, so the
 * per-unit statics in src/emulators/msx need no locking.
 */

#include <btron/wnd.h>
#include <btron/dp.h>
#include <btron/event.h>
#include <btron/error.h>
#include <btron/core.h>
#include <btron/itron.h>
#include <btron/app_menu.h>
#include <btron/troncode.h>
#include <emulators/tronmsx.h>
#include "../emulators/msx/msx_kbd.h"
#include <stdint.h>

#include <btron/libc_shim.h>
#if BTRON_HOSTED
#include <dirent.h>
#endif

#define MSX_SCR_W      256u
#define MSX_SCR_H      212u
#define MSX_MENU_H     APP_MENU_BAR_HEIGHT
#define MSX_STATUS_H   22
#define MSX_PAD        4
#define MSX_MAX_ROMS   32
#define MSX_ROW_NAME   64      /* app_menu cascade rows are [][64] */
#define MSX_PATH_MAX   160
#define MSX_ROM_DIR    "assets/msx"

typedef enum {
    XCMD_NONE = 0,
    XCMD_FILE_ROM_CASCADE,
    XCMD_FILE_RESET,
    XCMD_FILE_QUIT,
    XCMD_EMU_PAUSE,
    XCMD_EMU_RESUME,
    XCMD_VIEW_FIT,
    XCMD_VIEW_1X,
    XCMD_VIEW_2X,
    XCMD_VIEW_3X,
    XCMD_VIEW_4X,
    XCMD_HELP_ABOUT
} MSX_CMD;

typedef struct {
    APP_MENU_BAR menu_bar;
    char roms[MSX_MAX_ROMS][MSX_ROW_NAME];
    int  rom_count;
    char path[MSX_PATH_MAX];
    char notice[160];          /* load failure / hint, shown over the screen */
    int  scale;                /* 0 = fit the screen to the window */
    BOOL paused;
    BOOL running;              /* a core is live and holds a cartridge */
    uint8_t joy;               /* joystick A level, rebuilt from key events */
    uint64_t frames;
} MsxViewer;

static MsxViewer   s_msx;
static uint16_t    s_fb[MSX_SCR_W * MSX_SCR_H];   /* RGB565 written by the core */
static WND        *s_wnd;
static ID          s_tsk;
static int         s_frame_ms = 16;
/* Set by the 60 Hz tick task, cleared by the paint callback.  One byte, one
 * writer, one reader: a lost or duplicated tick costs one frame of speed, and
 * without it a repaint caused by desktop damage would fast-forward the machine. */
static volatile BOOL s_frame_due;

/* ── Cartridge catalogue and loading ─────────────────────────────────────── */

#if BTRON_HOSTED
static BOOL msx_is_rom_name(const char *name)
{
    size_t n = strlen(name);
    if (n < 5 || name[n - 4] != '.') return FALSE;
    return (name[n - 3] == 'r' || name[n - 3] == 'R') &&
           (name[n - 2] == 'o' || name[n - 2] == 'O') &&
           (name[n - 1] == 'm' || name[n - 1] == 'M');
}

static void msx_scan_roms(MsxViewer *st)
{
    DIR *d;
    struct dirent *de;

    st->rom_count = 0;
    d = opendir(MSX_ROM_DIR);
    if (!d) return;
    while ((de = readdir(d)) != NULL && st->rom_count < MSX_MAX_ROMS) {
        if (de->d_name[0] == '.') continue;
        if (!msx_is_rom_name(de->d_name)) continue;
        snprintf(st->roms[st->rom_count], MSX_ROW_NAME, "%s", de->d_name);
        st->rom_count++;
    }
    closedir(d);

    for (int i = 1; i < st->rom_count; i++) {
        char key[MSX_ROW_NAME];
        int j = i - 1;
        snprintf(key, sizeof(key), "%s", st->roms[i]);
        while (j >= 0 && strcmp(st->roms[j], key) > 0) {
            snprintf(st->roms[j + 1], MSX_ROW_NAME, "%s", st->roms[j]);
            j--;
        }
        snprintf(st->roms[j + 1], MSX_ROW_NAME, "%s", key);
    }
}

/* Read one cartridge image and hand it to the core. */
static BOOL msx_load(MsxViewer *st, const char *name)
{
    uint8_t *buf;
    FILE *f;
    long size;
    size_t got;
    int rc;

    st->notice[0] = '\0';
    if (!name || !name[0]) return FALSE;

    snprintf(st->path, sizeof(st->path), MSX_ROM_DIR "/%s", name);
    f = fopen(st->path, "rb");
    if (!f) {
        snprintf(st->notice, sizeof(st->notice),
                 "ROM を開けません (cannot open %s)", st->path);
        return FALSE;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0) {
        fclose(f);
        snprintf(st->notice, sizeof(st->notice), "ROM が空です (empty image)");
        return FALSE;
    }
    buf = (uint8_t *)malloc((size_t)size);
    if (!buf) {
        fclose(f);
        snprintf(st->notice, sizeof(st->notice),
                 "ホストメモリ不足 (host heap exhausted)");
        return FALSE;
    }
    got = fread(buf, 1, (size_t)size, f);
    fclose(f);

    rc = msx_load_rom(buf, (uint32_t)got, 0);
    free(buf);

    if (rc != MSX_OK) {
        if (rc == MSX_ERR_SIZE)
            snprintf(st->notice, sizeof(st->notice),
                     "%s: %ld バイトは上限 32768 を超えます "
                     "(banked megaROM mapper not implemented yet)", name, size);
        else
            snprintf(st->notice, sizeof(st->notice),
                     "%s: msx_load_rom -> error %d", name, rc);
        return FALSE;
    }

    msx_reset();
    st->paused  = FALSE;
    st->running = TRUE;
    st->joy     = 0u;
    st->frames  = 0;
    msx_joy_set(0u);
    return TRUE;
}
#else
static void msx_scan_roms(MsxViewer *st) { st->rom_count = 0; }
static BOOL msx_load(MsxViewer *st, const char *name)
{
    (void)name;
    snprintf(st->notice, sizeof(st->notice),
             "ROM 読み込みはホストビルド専用 (hosted build only)");
    return FALSE;
}
#endif

static const char *msx_basename(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

/* ── Menu bar ────────────────────────────────────────────────────────────── */

static void msx_init_menu_bar(MsxViewer *st)
{
    app_menu_init(&st->menu_bar, APP_MENU_STYLE_CLASSIC_3D);

    int h0 = app_menu_add_header(&st->menu_bar, "ファイル(F)", 104);
    app_menu_add_submenu_item(&st->menu_bar, h0, "ROM カセット ▶",
                              XCMD_FILE_ROM_CASCADE, 1);
    app_menu_add_separator(&st->menu_bar, h0);
    app_menu_add_item(&st->menu_bar, h0, "リセット (Reset)",
                      "Ctrl+R", XCMD_FILE_RESET, TRUE);
    app_menu_add_item(&st->menu_bar, h0, "終了 (Quit Window)",
                      "Ctrl+Q", XCMD_FILE_QUIT, TRUE);

    int h1 = app_menu_add_header(&st->menu_bar, "エミュレータ(E)", 132);
    app_menu_add_item(&st->menu_bar, h1, "一時停止 (Pause)",
                      "Ctrl+P", XCMD_EMU_PAUSE, TRUE);
    app_menu_add_item(&st->menu_bar, h1, "再開 (Resume)",
                      "Ctrl+P", XCMD_EMU_RESUME, TRUE);
    app_menu_add_item(&st->menu_bar, h1, "機械: MSX1 / TMS9918A",
                      "", XCMD_NONE, FALSE);

    int h2 = app_menu_add_header(&st->menu_bar, "表示(V)", 72);
    app_menu_add_item(&st->menu_bar, h2, "自動拡大 (Fit)", "", XCMD_VIEW_FIT, TRUE);
    app_menu_add_item(&st->menu_bar, h2, "1x 256x212", "", XCMD_VIEW_1X, TRUE);
    app_menu_add_item(&st->menu_bar, h2, "2x 512x424", "", XCMD_VIEW_2X, TRUE);
    app_menu_add_item(&st->menu_bar, h2, "3x 768x636", "", XCMD_VIEW_3X, TRUE);
    app_menu_add_item(&st->menu_bar, h2, "4x 1024x848", "", XCMD_VIEW_4X, TRUE);

    int h3 = app_menu_add_header(&st->menu_bar, "ヘルプ(H)", 88);
    app_menu_add_item(&st->menu_bar, h3, "B-MSX について (About)",
                      "", XCMD_HELP_ABOUT, TRUE);
}

static void msx_sync_menu_state(MsxViewer *st)
{
    char buf[96];

    if (st->running)
        snprintf(buf, sizeof(buf), "%s | %s",
                 msx_basename(st->path), st->paused ? "停止" : "実行");
    else
        snprintf(buf, sizeof(buf), "ROM 無し (%d 本)", st->rom_count);
    app_menu_set_right_text(&st->menu_bar, buf);
}

/* ── Layout and screen blit ──────────────────────────────────────────────── */

typedef struct {
    int scale, x, y, w, h;    /* integer scale and the screen area in client coords */
} MsxView;

static void msx_compute_view(const GDEV *dev, MsxView *v)
{
    int aw = dev->width - 2 * MSX_PAD;
    int ah = dev->height - MSX_MENU_H - MSX_STATUS_H - MSX_PAD;
    int s;

    if (aw < (int)MSX_SCR_W) aw = (int)MSX_SCR_W;
    if (ah < (int)MSX_SCR_H) ah = (int)MSX_SCR_H;

    s = aw / (int)MSX_SCR_W;
    if (ah / (int)MSX_SCR_H < s) s = ah / (int)MSX_SCR_H;
    if (s > 4) s = 4;
    if (s_msx.scale > 0 && s_msx.scale < s) s = s_msx.scale;
    if (s < 1) s = 1;

    v->scale = s;
    v->w = aw;
    v->h = ah;
    v->x = MSX_PAD + (aw - (int)MSX_SCR_W * s) / 2;
    v->y = MSX_MENU_H + (ah - (int)MSX_SCR_H * s) / 2;
}

static void msx_draw_screen(const MsxViewer *st, GDEV *dev, const MsxView *v)
{
    RECT area = { MSX_PAD, MSX_MENU_H, dev->width - MSX_PAD,
                  (H)(MSX_MENU_H + v->h) };
    int dx1 = v->x + (int)MSX_SCR_W * v->scale;
    int dy1 = v->y + (int)MSX_SCR_H * v->scale;

    fill_rec(dev, &area, COLOR_BLACK);
    if (!st->running) return;

    if (dx1 > dev->width) dx1 = dev->width;
    if (dy1 > dev->height - MSX_STATUS_H) dy1 = dev->height - MSX_STATUS_H;

    for (int dy = v->y; dy < dy1; dy++) {
        unsigned sy = (unsigned)(dy - v->y) / (unsigned)v->scale;
        const uint16_t *src;
        COLOR *dst;
        if (sy >= MSX_SCR_H) sy = MSX_SCR_H - 1u;
        src = s_fb + (size_t)sy * MSX_SCR_W;
        dst = dev->pixels + (size_t)dy * dev->width;
        for (int dx = v->x; dx < dx1; dx++) {
            unsigned c;
            unsigned r, g, b;
            unsigned sx = (unsigned)(dx - v->x) / (unsigned)v->scale;
            if (sx >= MSX_SCR_W) sx = MSX_SCR_W - 1u;
            c = src[sx];
            r = (c >> 11) & 0x1Fu;
            g = (c >> 5) & 0x3Fu;
            b = c & 0x1Fu;
            dst[dx] = 0xFF000000u |
                      ((COLOR)((r << 3) | (r >> 2)) << 16) |
                      ((COLOR)((g << 2) | (g >> 4)) << 8) |
                      (COLOR)((b << 3) | (b >> 2));
        }
    }
}

static void msx_draw_notice(const MsxViewer *st, GDEV *dev, const MsxView *v)
{
    const char *l0 = st->notice;
    const char *l1 = "";

    if (!l0[0]) {
        l0 = "ファイル(F) ▶ ROM カセット でカートリッジを選んでください";
        l1 = "(select a cartridge under File > ROM Cassette)";
    }
    drw_tc_string(dev, (H)(MSX_PAD + 16), (H)(v->y + v->h / 2 - 18), l0,
                  COLOR_CYAN, 0x00000000);
    if (l1[0])
        drw_tc_string(dev, (H)(MSX_PAD + 16), (H)(v->y + v->h / 2), l1,
                      COLOR_LTGRAY, 0x00000000);
}

static void msx_draw_status(const MsxViewer *st, GDEV *dev)
{
    RECT bar = { 0, (H)(dev->height - MSX_STATUS_H), dev->width, dev->height };
    char msg[256];

    fill_rec(dev, &bar, COLOR_LTGRAY);
    drw_lin(dev, 0, (H)(dev->height - MSX_STATUS_H), dev->width,
            (H)(dev->height - MSX_STATUS_H));

    if (st->running)
        snprintf(msg, sizeof(msg),
                 "B-MSX | %s | T=%llu T-states | %s | 矢印=ジョイスティックA"
                 "  Z=ボタンA  X=ボタンB",
                 msx_basename(st->path),
                 (unsigned long long)msx_total_cycles(),
                 st->paused ? "一時停止" : "実行中");
    else
        snprintf(msg, sizeof(msg), "%s | ROM 未装填 (%d 本発見)",
                 msx_version_string(), st->rom_count);
    drw_tc_string(dev, 8, (H)(dev->height - 17), msg, COLOR_BLACK, 0x00000000);
}

/* ── Paint ───────────────────────────────────────────────────────────────── */

static void msx_view(WND *wnd, GDEV *dev)
{
    MsxViewer *st = &s_msx;
    MsxView v;

    if (!wnd || !dev) return;

    RECT full = { 0, 0, dev->width, dev->height };
    fill_rec(dev, &full, COLOR_DKGRAY);

    if (st->menu_bar.header_count == 0) msx_init_menu_bar(st);

    /* The tick task is the pacer: with one running, a repaint advances the machine
     * only when a frame is actually due, so damage from elsewhere on the desktop
     * never fast-forwards the emulator.  Where no task was created - a target
     * whose cre_tsk() fails, or the headless capture harness - every repaint is a
     * frame, because nothing else would ever ask for one. */
    if (st->running && !st->paused && (s_frame_due || s_tsk <= 0)) {
        s_frame_due = FALSE;
        msx_run_frame();
        msx_blit_rgb565(s_fb);
        st->frames++;
    }

    msx_compute_view(dev, &v);
    msx_draw_screen(st, dev, &v);
    if (!st->running || st->notice[0]) msx_draw_notice(st, dev, &v);

    msx_sync_menu_state(st);
    app_menu_paint_bar(&st->menu_bar, dev);
    if (st->menu_bar.active_menu >= 0) {
        app_menu_paint_dropdown(&st->menu_bar, dev);
        if (st->menu_bar.active_menu == 0 && st->menu_bar.active_submenu >= 0)
            app_menu_paint_cascading_strings(&st->menu_bar, dev, st->roms,
                                             st->rom_count);
    }

    msx_draw_status(st, dev);
}

/* ── Commands ────────────────────────────────────────────────────────────── */

static void msx_apply_cmd(WND *wnd, MsxViewer *st, int cmd, int sub_idx)
{
    switch (cmd) {
        case XCMD_FILE_ROM_CASCADE:
#if BTRON_HOSTED
            if (sub_idx >= 0 && sub_idx < st->rom_count) msx_load(st, st->roms[sub_idx]);
#else
            (void)sub_idx;
#endif
            break;
        case XCMD_FILE_RESET:
            if (st->running) {
                msx_reset();
                st->frames = 0;
                st->joy = 0u;
                msx_joy_set(0u);
            }
            break;
        case XCMD_FILE_QUIT:
            if (wnd) cls_wnd(wnd);
            break;
        case XCMD_EMU_PAUSE:
            if (st->running && !st->paused) {
                msx_pause(TRUE);
                st->paused = TRUE;
            }
            break;
        case XCMD_EMU_RESUME:
            if (st->running && st->paused) {
                msx_pause(FALSE);
                st->paused = FALSE;
                s_frame_due = TRUE;
            }
            break;
        case XCMD_VIEW_FIT: st->scale = 0; break;
        case XCMD_VIEW_1X:  st->scale = 1; break;
        case XCMD_VIEW_2X:  st->scale = 2; break;
        case XCMD_VIEW_3X:  st->scale = 3; break;
        case XCMD_VIEW_4X:  st->scale = 4; break;
        case XCMD_HELP_ABOUT:
            app_menu_create_about_dialog("B-MSX", "エムエスエックス",
                                         "tronMSX clean-room MSX1 emulator core",
                                         msx_version_string(), 260, 180);
            break;
        default:
            return;
    }
    if (wnd) inval_wnd(wnd);
}

/* ── Input: host keys -> MSX joystick level and keyboard matrix ──────────── */

typedef struct {
    UW      key;
    uint8_t joy;          /* joystick A bit, or 0 for a matrix cell */
    uint8_t row, col;
} MsxKeyMap;

/* SDL keysyms: LSHIFT 0x400000E2, RSHIFT 0x400000E6, LCTRL 0x400000E0,
 * LALT 0x400000E4. */
static const MsxKeyMap s_keymap[] = {
    { BTRON_KEY_UP,        MSX_JOY_UP,    0u, 0u },
    { BTRON_KEY_DOWN,      MSX_JOY_DOWN,  0u, 0u },
    { BTRON_KEY_LEFT,      MSX_JOY_LEFT,  0u, 0u },
    { BTRON_KEY_RIGHT,     MSX_JOY_RIGHT, 0u, 0u },
    { 'z',                 MSX_JOY_TRGA,  0u, 0u },
    { 'x',                 MSX_JOY_TRGB,  0u, 0u },
    { BTRON_KEY_RETURN,    0u, MSX_KEY_ENTER_R,  MSX_KEY_ENTER_C },
    { BTRON_KEY_BACKSPACE, 0u, MSX_KEY_BS_R,     MSX_KEY_BS_C },
    { BTRON_KEY_TAB,       0u, MSX_KEY_TAB_R,    MSX_KEY_TAB_C },
    { BTRON_KEY_ESCAPE,    0u, MSX_KEY_ESC_R,    MSX_KEY_ESC_C },
    { BTRON_KEY_DELETE,    0u, MSX_KEY_DEL_R,    MSX_KEY_DEL_C },
    { BTRON_KEY_HOME,      0u, MSX_KEY_HOME_R,   MSX_KEY_HOME_C },
    { BTRON_KEY_F1,        0u, MSX_KEY_F1_R,     MSX_KEY_F1_C },
    { BTRON_KEY_F3,        0u, MSX_KEY_F3_R,     MSX_KEY_F3_C },
    { BTRON_KEY_F4,        0u, MSX_KEY_F4_R,     MSX_KEY_F4_C },
    { BTRON_KEY_F5,        0u, MSX_KEY_F5_R,     MSX_KEY_F5_C },
    { 0x400000E2u,         0u, MSX_KEY_SHIFT_R,  MSX_KEY_SHIFT_C },
    { 0x400000E6u,         0u, MSX_KEY_SHIFT_R,  MSX_KEY_SHIFT_C },
    { 0x400000E0u,         0u, MSX_KEY_CTRL_R,   MSX_KEY_CTRL_C },
    { 0x400000E4u,         0u, MSX_KEY_CODE_R,   MSX_KEY_CODE_C },
};

static void msx_key(MsxViewer *st, UW key, BOOL down)
{
    uint8_t row, col;

    /* F2 belongs to the menu bar (app_menu_handle_key), so the MSX F2 cell is
     * not reachable and stays out of the table. */
    for (size_t i = 0; i < sizeof(s_keymap) / sizeof(s_keymap[0]); i++) {
        if (s_keymap[i].key != key) continue;
        if (s_keymap[i].joy) {
            if (down) st->joy |= s_keymap[i].joy;
            else      st->joy &= (uint8_t)~s_keymap[i].joy;
            msx_joy_set(st->joy);
        } else if (down) {
            msx_kbd_down(s_keymap[i].row, s_keymap[i].col);
        } else {
            msx_kbd_up(s_keymap[i].row, s_keymap[i].col);
        }
        return;
    }

    if (msx_kbd_position((char)key, &row, &col)) {
        if (down) msx_kbd_down(row, col);
        else      msx_kbd_up(row, col);
    }
}

/* ── Events ──────────────────────────────────────────────────────────────── */

static void msx_handle_event(WND *wnd, const EVT *evt)
{
    MsxViewer *st = &s_msx;
    H rel_x, rel_y;

    if (!wnd || !evt) return;
    rel_x = (H)(evt->pos.x - (wnd->bounds.left + 4));
    rel_y = (H)(evt->pos.y - (wnd->bounds.top + 26));

    if (evt->type == EV_MOUSE_MOVE) {
        if (app_menu_handle_mouse_move(&st->menu_bar, rel_x, rel_y)) inval_wnd(wnd);
        return;
    }

    if (evt->type == EV_BUT_DOWN) {
        int cmd = 0, sub = -1;
        if (app_menu_handle_mouse_down(&st->menu_bar, rel_x, rel_y, &cmd, &sub)) {
            msx_apply_cmd(wnd, st, cmd, sub);
            inval_wnd(wnd);
        }
        return;
    }

    if (evt->type == EV_KEY_DOWN) {
        int cmd = 0;
        if (app_menu_handle_key(&st->menu_bar, evt->key,
                                (uint16_t)(uintptr_t)evt->data, &cmd)) {
            msx_apply_cmd(wnd, st, cmd, -1);
            inval_wnd(wnd);
            return;
        }
        msx_key(st, evt->key, TRUE);
        return;
    }

    if (evt->type == EV_KEY_UP) {
        msx_key(st, evt->key, FALSE);
    }
}

/* ── Frame tick ──────────────────────────────────────────────────────────── */

static void msx_task(VW exinf)
{
    (void)exinf;
    while (s_wnd) {
        s_frame_due = TRUE;
        inval_wnd(s_wnd);
        dly_tsk(s_frame_ms);
    }
    s_tsk = 0;
}

/* ── Lifecycle ───────────────────────────────────────────────────────────── */

static BOOL msx_menu_open(WND *wnd)
{
    return (wnd && s_msx.menu_bar.active_menu >= 0) ? TRUE : FALSE;
}

static void destroy_msx_wnd(WND *wnd)
{
    (void)wnd;
    if (s_msx.running) msx_shutdown();
    s_msx.running = FALSE;
    s_msx.paused = FALSE;
    s_frame_due = FALSE;
    s_wnd = NULL;
}

static WND *msx_open(const char *rom_name)
{
    MsxViewer *st = &s_msx;
    T_CTSK ctsk;
    int win_w = MSX_SCR_W * 2 + 2 * MSX_PAD + 8;
    int win_h = MSX_MENU_H + MSX_SCR_H * 2 + MSX_STATUS_H + MSX_PAD + 36;
    WND *wnd;

    if (s_wnd) {
        if (rom_name) msx_load(st, rom_name);
        top_wnd(s_wnd);
        inval_wnd(s_wnd);
        return s_wnd;
    }

    memset(st, 0, sizeof(*st));
    st->menu_bar.active_menu = -1;
    st->menu_bar.hover_menu = -1;
    st->menu_bar.hover_item = -1;
    st->menu_bar.active_submenu = -1;
    st->menu_bar.hover_subitem = -1;
    msx_scan_roms(st);

    if (msx_init(NULL) != MSX_OK) {
        snprintf(st->notice, sizeof(st->notice),
                 "C-BIOS が読み込めません (BIOS missing under "
                 "third_party/openMSX/Contrib/cbios)");
    } else if (rom_name) {
        msx_load(st, rom_name);
    }
    msx_init_menu_bar(st);

    wnd = opn_wnd("B-MSX", 60, 60, (H)win_w, (H)win_h,
                  WND_ATTR_TITLE | WND_ATTR_CLOSE | WND_ATTR_RESIZE |
                  WND_ATTR_BORDER);
    if (!wnd) {
        if (st->running) msx_shutdown();
        return NULL;
    }
    s_wnd = wnd;
    wnd->paint         = msx_view;
    wnd->event_handler = msx_handle_event;
    wnd->destroy       = destroy_msx_wnd;
    wnd->menu_open     = msx_menu_open;

    ctsk.exinf   = 0;
    ctsk.tskatr  = TA_HLNG;
    ctsk.task    = msx_task;
    ctsk.itskpri = 10;
    ctsk.stksz   = 16384;
    s_tsk = cre_tsk(&ctsk);
    if (s_tsk > 0) sta_tsk(s_tsk, 0);

    return wnd;
}

WND *open_msx_window(void)
{
    return msx_open(NULL);
}

/* `path` may be a full path or a bare file name; it is resolved against
 * assets/msx, the same catalogue the ROM cascade and the XMB Games band list. */
WND *open_msx_window_with_rom(const char *path)
{
    const char *name = path;
    if (name) {
        const char *slash = strrchr(name, '/');
        if (slash) name = slash + 1;
    }
    return msx_open(name);
}
