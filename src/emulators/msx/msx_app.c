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
#include <device/virtio.h>
#include "msx_kbd.h"
#include "msx_int.h"
#include "../../clu/vfs.h"          /* the /SYS catalogue: src/clu/vfs.c */
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

typedef enum {
    XCMD_NONE = 0,
    XCMD_FILE_ROM_CASCADE,
    XCMD_FILE_RESET,
    XCMD_FILE_QUIT,
    XCMD_EMU_PAUSE,
    XCMD_EMU_RESUME,
    XCMD_EMU_FAST_FORWARD,
    XCMD_VIEW_FIT,
    XCMD_VIEW_1X,
    XCMD_VIEW_2X,
    XCMD_VIEW_3X,
    XCMD_VIEW_4X,
    XCMD_INPUT_KBD_MODE,
    XCMD_INPUT_JOY_MODE,
    XCMD_INPUT_DUAL_MODE,
    XCMD_HELP_ABOUT
} MSX_CMD;

typedef enum {
    MSX_INPUT_KBD_PRIORITY = 0, /* Arrows & keys drive MSX keyboard matrix (authentic) */
    MSX_INPUT_JOY_PRIORITY = 1, /* Arrows & Z/X drive MSX Joypad port 1 only */
    MSX_INPUT_DUAL         = 2  /* Arrows & Z/X drive both simultaneously */
} MsxInputMode;

typedef struct {
    APP_MENU_BAR menu_bar;
    char roms[MSX_MAX_ROMS][MSX_ROW_NAME];
    /* Which Real Body each row names.  On a host this is VFS_NO_FID, where the
     * folder entry is the whole identity; in the image it is the fid the volume
     * listing returned, without which a second body of the same name would open. */
    uint32_t rom_fids[MSX_MAX_ROMS];
    int  rom_count;
    char path[MSX_PATH_MAX];
    char notice[160];          /* load failure / hint, shown over the screen */
    int  scale;                /* 0 = fit the screen to the window */
    BOOL paused;
    BOOL running;              /* a core is live and holds a cartridge */
    MsxInputMode input_mode;
    uint8_t joy;               /* combined joystick A level: joy_keys | joy_mouse */
    uint8_t joy_keys;          /* joystick bits held by keyboard */
    uint8_t joy_mouse;         /* joystick bits held by mouse */
    uint8_t mouse_decay;       /* frame decay timer for mouse impulses */
    uint8_t mouse_buttons;     /* proportional mouse buttons: bit0=L, bit1=R */
    uint64_t frames;
    int  emu_header;           /* エミュレータ(E), or -1 before the bar exists */
    int  machine_item;         /* status row that names the running machine */
    int  input_header;         /* 入力(I), or -1 before the bar exists */
    int  input_kbd_item;
    int  input_joy_item;
    int  input_dual_item;
} MsxViewer;

static MsxViewer   s_msx;
static uint16_t    s_fb[MSX_SCR_W * MSX_SCR_H];   /* RGB565 written by the core */
static WND        *s_wnd;
static ID          s_tsk;
/* Counter set by the 60 Hz tick task, decremented by the paint callback.
 * Paces the machine accurately even when desktop damage, event processing,
 * or host VSync jitter delays a paint call. Catch-up steps both CPU and audio
 * so the audio buffer never starves. */
static volatile int s_frames_due;

/* One emulated frame of S16, pumped like lilcu64_synth_pump_virtio: write
 * while the device is under ~200 ms, otherwise count a drop. The device is
 * process-wide, so this window does not close it and does not reopen it
 * when another producer already holds 44100 Hz stereo. */
#define MSX_AUDIO_TARGET_BYTES 35280u
static int16_t  s_pcm[735u * 2u];
static uint32_t s_audio_drops;
static int      s_audio_open_tried;
/* Speaker capacitor. The chip mix is DC-coupled: a PSG level is unipolar and
 * shifts the bipolar FM toward the positive rail. The jack on the machine is
 * not. Mean is Q8. Shift 9 is about 14 Hz at 44100 Hz, so a held level leaves
 * within a few frames and a 40 Hz tone stays. */
static int32_t  s_hp[2];

static void msx_host_ac_reset(void)
{
    s_hp[0] = 0;
    s_hp[1] = 0;
}

static void msx_host_ac(int16_t *pcm, uint32_t frames)
{
    uint32_t i;
    for (i = 0u; i < frames; i++) {
        int ch;
        for (ch = 0; ch < 2; ch++) {
            int32_t x = pcm[i * 2u + (uint32_t)ch];
            int32_t mean = s_hp[ch];
            int32_t y;
            mean += ((x << 8) - mean) >> 9;
            s_hp[ch] = mean;
            y = x - (mean >> 8);
            if (y > 32767) y = 32767;
            if (y < -32768) y = -32768;
            pcm[i * 2u + (uint32_t)ch] = (int16_t)y;
        }
    }
}

static void msx_host_pump_audio(int queue)
{
    uint32_t n = 0u;

    msx_psg_submit_pcm(s_pcm, &n);
    if (n) msx_host_ac(s_pcm, n);
    if (!queue || !n) return;
    if (!virtio_sound_is_ready()) {
        if (!s_audio_open_tried) {
            s_audio_open_tried = 1;
            virtio_sound_open(44100, 2);
        }
        if (!virtio_sound_is_ready()) {
            s_audio_drops++;
            return;
        }
    }
    if (virtio_sound_get_queued_bytes() >= MSX_AUDIO_TARGET_BYTES) {
        s_audio_drops++;
        return;
    }
    if (virtio_sound_write(s_pcm, n) < 0) s_audio_drops++;
    if (s_audio_drops == 0xFFFFFFFFu) s_audio_drops = 0u;
}

/* MegaROM homebrew in this drawer boots on turboR. Two file-hunter carts
 * name their own machine. Bad Apple is a 2 MiB ASCII16 image, so the size
 * rule would pick turboR. The file-hunter page leaves the machine blank and
 * WebMSX then picks MSX2+, whose preset inserts MSX-MUSIC. MGSDRV only
 * starts the YM2413 track after it reads APRLOPLL at $4018 in that ROM.
 * C-BIOS cbios_music.rom is that signature, mapped on MSX2+ and turboR.
 * Dynamite Go! Go! is the 32 KiB MSX2J demo, which the size rule would
 * boot as MSX1. A disk image is chosen before the size rule: a 360 KiB
 * or 720 KiB file would otherwise become a turboR cartridge with no
 * disk ROM. Bad Apple's disk wants the V9958, so it takes MSX2+ FDD.
 * Every other disk takes MSX2 FDD. */
static BOOL msx_is_dsk_name(const char *name)
{
    size_t n;
    char a, b, c;
    if (!name) return FALSE;
    n = strlen(name);
    if (n < 5 || name[n - 4] != '.') return FALSE;
    a = name[n - 3];
    b = name[n - 2];
    c = name[n - 1];
    if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
    if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
    if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    return a == 'd' && b == 's' && c == 'k';
}

static uint16_t msx_machine_for(const char *name, size_t size)
{
    if (name && (strstr(name, "HC-95") || strstr(name, "HC95") || strstr(name, "hc95")))
        return msx_is_dsk_name(name) ? BMSX_MACHINE_HC95_CBIOS_FDD : BMSX_MACHINE_HC95_CBIOS;
    if (name && (strstr(name, "HC-90") || strstr(name, "HC90") || strstr(name, "hc90")))
        return BMSX_MACHINE_HC90_CBIOS;

    if (msx_is_dsk_name(name)) {
        if (strstr(name, "Bad Apple")) return BMSX_MACHINE_MSX2P_CBIOS_FDD;
        return BMSX_MACHINE_MSX2_CBIOS_FDD;
    }
    if (name && strstr(name, "Bad Apple"))
        return BMSX_MACHINE_MSX2P_CBIOS;
    if (name && strstr(name, "Dynamite"))
        return BMSX_MACHINE_MSX2_CBIOS_JP;
    if (size > 65536u ||
        (name && (strstr(name, "BULLRUSH") || strstr(name, "NEON") ||
                  strstr(name, "NIGHT") || strstr(name, "btron") ||
                  strstr(name, "BTRON") || strstr(name, "bmsx") ||
                  strstr(name, "BMSX"))))
        return BMSX_MACHINE_MSXTR_CBIOS;
    return BMSX_MACHINE_MSX;
}

/* ── Cartridge and disk catalogue ────────────────────────────────────────── */

/* A row is a cartridge (.rom) or a raw disk image (.dsk), either case. */
static BOOL msx_is_media_name(const char *name)
{
    size_t n;
    char a, b, c;

    if (!name) return FALSE;
    n = strlen(name);
    if (n < 5 || name[n - 4] != '.') return FALSE;
    a = name[n - 3];
    b = name[n - 2];
    c = name[n - 1];
    if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
    if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
    if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    return (a == 'r' && b == 'o' && c == 'm') ||
           (a == 'd' && b == 's' && c == 'k');
}

/* Rows are sorted so the cascade and the XMB Games band land on the same row
 * from one run to the next, whichever side of the hosted boundary listed them. */
static void msx_sort_rows(MsxViewer *st)
{
    for (int i = 1; i < st->rom_count; i++) {
        char key[MSX_ROW_NAME];
        uint32_t key_fid = st->rom_fids[i];
        int j = i - 1;

        snprintf(key, sizeof(key), "%s", st->roms[i]);
        while (j >= 0 && strcmp(st->roms[j], key) > 0) {
            snprintf(st->roms[j + 1], MSX_ROW_NAME, "%s", st->roms[j]);
            st->rom_fids[j + 1] = st->rom_fids[j];
            j--;
        }
        snprintf(st->roms[j + 1], MSX_ROW_NAME, "%s", key);
        st->rom_fids[j + 1] = key_fid;
    }
}

#if BTRON_HOSTED
static void msx_scan_roms(MsxViewer *st)
{
    DIR *d;
    struct dirent *de;

    st->rom_count = 0;
    d = opendir(MSX_ROM_CATALOGUE);
    if (!d) return;
    while ((de = readdir(d)) != NULL && st->rom_count < MSX_MAX_ROMS) {
        if (de->d_name[0] == '.') continue;
        if (!msx_is_media_name(de->d_name)) continue;
        snprintf(st->roms[st->rom_count], MSX_ROW_NAME, "%s", de->d_name);
        st->rom_fids[st->rom_count] = VFS_NO_FID;
        st->rom_count++;
    }
    closedir(d);
    msx_sort_rows(st);
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

    snprintf(st->path, sizeof(st->path), MSX_ROM_CATALOGUE "/%s", name);
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

    msx_cfg_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.frame_hz = 60;
    cfg.machine = msx_machine_for(name, (size_t)size);
    msx_init(&cfg);

    if (msx_is_dsk_name(name)) rc = slots_set_dsk(buf, (uint32_t)got);
    else rc = msx_load_rom(buf, (uint32_t)got, 0);
    free(buf);

    if (rc != MSX_OK) {
        if (rc == MSX_ERR_SIZE && msx_is_dsk_name(name))
            snprintf(st->notice, sizeof(st->notice),
                     "%s: %ld バイトはディスクイメージではありません", name, size);
        else if (rc == MSX_ERR_SIZE)
            snprintf(st->notice, sizeof(st->notice),
                     "%s: %ld バイトは上限 %u を超えます",
                     name, size, (unsigned)MSX_ROM_SIZE);
        else
            snprintf(st->notice, sizeof(st->notice),
                     "%s: load -> error %d", name, rc);
        return FALSE;
    }

    msx_reset();
    msx_host_ac_reset();
    st->paused  = FALSE;
    st->running = TRUE;
    st->joy     = 0u;
    st->joy_keys = 0u;
    st->joy_mouse = 0u;
    st->mouse_decay = 0u;
    st->frames  = 0;
    msx_joy_set(0u);
    return TRUE;
}
#else
/* The catalogue is a drawer of the /SYS volume the image carries, so the same
 * rows appear on a machine with no file system below the program at all. */
static void msx_scan_roms(MsxViewer *st)
{
    /* Static, not on the caller's stack: this runs while opening the window, on
     * a task stack shared with the desktop, and 32 rows of VfsEntry are 4.6 KiB. */
    static VfsEntry entries[MSX_MAX_ROMS];
    int n = vfs_list_dir(MSX_ROM_CATALOGUE, entries, MSX_MAX_ROMS);

    st->rom_count = 0;
    for (int i = 0; i < n && st->rom_count < MSX_MAX_ROMS; i++) {
        if (entries[i].is_dir || !msx_is_media_name(entries[i].name)) continue;
        snprintf(st->roms[st->rom_count], MSX_ROW_NAME, "%s", entries[i].name);
        st->rom_fids[st->rom_count] = entries[i].fid;
        st->rom_count++;
    }
    msx_sort_rows(st);
}

/* No second copy: the cartridge is read straight into the core's own buffer
 * (msx_rom_buffer), which has one byte more than the core's cap -- enough to
 * tell a 32 KiB image from a bigger one, since the core can only run the first
 * page of a banked cart and must be told to refuse the rest. */

/* The Real Body this row named, anchor included: a drawer can hold two bodies
 * of one name, and only the fid says which one the cursor is on. */
static uint32_t msx_rom_fid(const MsxViewer *st, const char *name)
{
    for (int i = 0; i < st->rom_count; i++) {
        if (strcmp(st->roms[i], name) == 0) return st->rom_fids[i];
    }
    return VFS_NO_FID;
}

static BOOL msx_load(MsxViewer *st, const char *name)
{
    size_t got = 0;
    int rc;

    st->notice[0] = '\0';
    if (!name || !name[0]) return FALSE;

    uint32_t rom_cap = 0;
    uint8_t *rom_image = msx_rom_buffer(&rom_cap);

    vfs_child_path(st->path, sizeof(st->path), MSX_ROM_CATALOGUE, name,
                   msx_rom_fid(st, name));

    msx_cfg_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.frame_hz = 60;

    /* A disk is read after msx_init: that call clears the cartridge buffer,
     * which is the only place a freestanding image can land. slots_set_dsk
     * copies it out. A cartridge keeps the old order. */
    if (msx_is_dsk_name(name)) {
        cfg.machine = msx_machine_for(name, 0);
        msx_init(&cfg);
        if (vfs_read_blob(st->path, rom_image, rom_cap, &got) != 0 || got == 0) {
            snprintf(st->notice, sizeof(st->notice),
                     "ROM を開けません (cannot open %s)", st->path);
            return FALSE;
        }
        rc = slots_set_dsk(rom_image, (uint32_t)got);
    } else {
        if (vfs_read_blob(st->path, rom_image, rom_cap, &got) != 0 || got == 0) {
            snprintf(st->notice, sizeof(st->notice),
                     "ROM を開けません (cannot open %s)", st->path);
            return FALSE;
        }
        cfg.machine = msx_machine_for(name, got);
        msx_init(&cfg);
        rc = msx_load_rom(rom_image, (uint32_t)got, 0);
    }

    if (rc != MSX_OK) {
        if (rc == MSX_ERR_SIZE && msx_is_dsk_name(name))
            snprintf(st->notice, sizeof(st->notice),
                     "%s: %u バイトはディスクイメージではありません",
                     name, (unsigned)got);
        else if (rc == MSX_ERR_SIZE)
            snprintf(st->notice, sizeof(st->notice),
                     "%s: %u バイトは上限 %u を超えます",
                     name, (unsigned)got, (unsigned)MSX_ROM_SIZE);
        else
            snprintf(st->notice, sizeof(st->notice),
                     "%s: load -> error %d", name, rc);
        return FALSE;
    }

    msx_reset();
    msx_host_ac_reset();
    st->paused  = FALSE;
    st->running = TRUE;
    st->joy     = 0u;
    st->joy_keys = 0u;
    st->joy_mouse = 0u;
    st->mouse_decay = 0u;
    st->frames  = 0;
    msx_joy_set(0u);
    return TRUE;
}
#endif

static const char *msx_basename(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

/* ── Menu bar ────────────────────────────────────────────────────────────── */

/* The row names the machine msx_init actually selected. An empty window is
 * turboR: msx_init(NULL) resolves there. A cartridge load replaces it. */
static const char *msx_machine_label(uint16_t id)
{
    switch (id) {
    case BMSX_MACHINE_MSX:               return "機械: MSX1 / TMS9918A";
    case BMSX_MACHINE_MSX2_CBIOS:        return "機械: MSX2 / V9938";
    case BMSX_MACHINE_MSX2_CBIOS_BR:     return "機械: MSX2 BR / V9938";
    case BMSX_MACHINE_MSX2_CBIOS_EU:     return "機械: MSX2 EU / V9938";
    case BMSX_MACHINE_MSX2_CBIOS_JP:     return "機械: MSX2 JP / V9938";
    case BMSX_MACHINE_MSX2_CBIOS_FDD:    return "機械: MSX2 FDD / V9938";
    case BMSX_MACHINE_MSX2_CBIOS_FDD_BR: return "機械: MSX2 FDD BR / V9938";
    case BMSX_MACHINE_MSX2_CBIOS_FDD_EU: return "機械: MSX2 FDD EU / V9938";
    case BMSX_MACHINE_MSX2_CBIOS_FDD_JP: return "機械: MSX2 FDD JP / V9938";
    case BMSX_MACHINE_MSX2P_CBIOS:       return "機械: MSX2+ / V9958";
    case BMSX_MACHINE_MSX2P_CBIOS_BR:    return "機械: MSX2+ BR / V9958";
    case BMSX_MACHINE_MSX2P_CBIOS_EU:    return "機械: MSX2+ EU / V9958";
    case BMSX_MACHINE_MSX2P_CBIOS_JP:    return "機械: MSX2+ JP / V9958";
    case BMSX_MACHINE_MSX2P_CBIOS_FDD:   return "機械: MSX2+ FDD / V9958";
    case BMSX_MACHINE_MSX2P_CBIOS_FDD_BR:return "機械: MSX2+ FDD BR / V9958";
    case BMSX_MACHINE_MSX2P_CBIOS_FDD_EU:return "機械: MSX2+ FDD EU / V9958";
    case BMSX_MACHINE_MSX2P_CBIOS_FDD_JP:return "機械: MSX2+ FDD JP / V9958";
    case BMSX_MACHINE_MSXTR_CBIOS_FDD:   return "機械: turboR FDD / R800";
    case BMSX_MACHINE_MSXTR_CBIOS:       return "機械: turboR / R800";
    case BMSX_MACHINE_HC90_CBIOS:        return "機械: Hitachi HC-90 / HD64180";
    case BMSX_MACHINE_HC95_CBIOS:        return "機械: Hitachi HC-95 / HD64180";
    case BMSX_MACHINE_HC95_CBIOS_FDD:    return "機械: Hitachi HC-95 FDD / HD64180";
    default:                             return "機械: turboR / R800";
    }
}

static void msx_init_menu_bar(MsxViewer *st)
{
    app_menu_init(&st->menu_bar, APP_MENU_STYLE_CLASSIC_3D);
    st->emu_header = -1;
    st->machine_item = -1;
    st->input_header = -1;
    st->input_kbd_item = -1;
    st->input_joy_item = -1;
    st->input_dual_item = -1;

    int h0 = app_menu_add_header(&st->menu_bar, "ファイル(F)", 104);
    app_menu_add_submenu_item(&st->menu_bar, h0, "ROM カセット ▶",
                              XCMD_FILE_ROM_CASCADE, 1);
    app_menu_add_separator(&st->menu_bar, h0);
    app_menu_add_item(&st->menu_bar, h0, "リセット (Reset)",
                      "Ctrl+R", XCMD_FILE_RESET, TRUE);
    app_menu_add_item(&st->menu_bar, h0, "終了 (Quit Window)",
                      "Ctrl+Q", XCMD_FILE_QUIT, TRUE);

    int h1 = app_menu_add_header(&st->menu_bar, "エミュレータ(E)", 132);
    st->emu_header = h1;
    app_menu_add_item(&st->menu_bar, h1, "一時停止 (Pause)",
                      "Ctrl+P", XCMD_EMU_PAUSE, TRUE);
    app_menu_add_item(&st->menu_bar, h1, "再開 (Resume)",
                      "Ctrl+P", XCMD_EMU_RESUME, TRUE);
    app_menu_add_item(&st->menu_bar, h1, "早送り 300F (Fast Forward)",
                      "Tab", XCMD_EMU_FAST_FORWARD, TRUE);
    st->machine_item = app_menu_add_item(&st->menu_bar, h1,
                                         msx_machine_label(msx_get_machine()),
                                         "", XCMD_NONE, FALSE);

    int h2 = app_menu_add_header(&st->menu_bar, "表示(V)", 72);
    app_menu_add_item(&st->menu_bar, h2, "自動拡大 (Fit)", "", XCMD_VIEW_FIT, TRUE);
    app_menu_add_item(&st->menu_bar, h2, "1x 256x212", "", XCMD_VIEW_1X, TRUE);
    app_menu_add_item(&st->menu_bar, h2, "2x 512x424", "", XCMD_VIEW_2X, TRUE);
    app_menu_add_item(&st->menu_bar, h2, "3x 768x636", "", XCMD_VIEW_3X, TRUE);
    app_menu_add_item(&st->menu_bar, h2, "4x 1024x848", "", XCMD_VIEW_4X, TRUE);

    int h3 = app_menu_add_header(&st->menu_bar, "入力(I)", 88);
    st->input_header = h3;
    st->input_kbd_item = app_menu_add_item(&st->menu_bar, h3,
                                           "(*) キーボード優先 (Keyboard)",
                                           "", XCMD_INPUT_KBD_MODE, TRUE);
    st->input_joy_item = app_menu_add_item(&st->menu_bar, h3,
                                           "( ) ジョイスティック (Joypad 1)",
                                           "", XCMD_INPUT_JOY_MODE, TRUE);
    st->input_dual_item = app_menu_add_item(&st->menu_bar, h3,
                                            "( ) 両方 (Dual Matrix+Joy)",
                                            "", XCMD_INPUT_DUAL_MODE, TRUE);

    int h4 = app_menu_add_header(&st->menu_bar, "ヘルプ(H)", 88);
    app_menu_add_item(&st->menu_bar, h4, "B-MSX について (About)",
                      "", XCMD_HELP_ABOUT, TRUE);
}

static void msx_sync_menu_state(MsxViewer *st)
{
    char buf[96];

    if (st->emu_header >= 0 && st->machine_item >= 0 &&
        st->emu_header < st->menu_bar.header_count) {
        APP_MENU_HEADER *hdr = &st->menu_bar.headers[st->emu_header];
        if (st->machine_item < hdr->item_count) {
            snprintf(hdr->items[st->machine_item].label,
                     sizeof(hdr->items[st->machine_item].label), "%s",
                     msx_machine_label(msx_get_machine()));
        }
    }

    if (st->input_header >= 0 && st->input_header < st->menu_bar.header_count) {
        APP_MENU_HEADER *hdr = &st->menu_bar.headers[st->input_header];
        if (st->input_kbd_item >= 0 && st->input_kbd_item < hdr->item_count) {
            snprintf(hdr->items[st->input_kbd_item].label,
                     sizeof(hdr->items[st->input_kbd_item].label),
                     "%s キーボード優先 (Keyboard)",
                     st->input_mode == MSX_INPUT_KBD_PRIORITY ? "(*)" : "( )");
        }
        if (st->input_joy_item >= 0 && st->input_joy_item < hdr->item_count) {
            snprintf(hdr->items[st->input_joy_item].label,
                     sizeof(hdr->items[st->input_joy_item].label),
                     "%s ジョイスティック (Joypad 1)",
                     st->input_mode == MSX_INPUT_JOY_PRIORITY ? "(*)" : "( )");
        }
        if (st->input_dual_item >= 0 && st->input_dual_item < hdr->item_count) {
            snprintf(hdr->items[st->input_dual_item].label,
                     sizeof(hdr->items[st->input_dual_item].label),
                     "%s 両方 (Dual Matrix+Joy)",
                     st->input_mode == MSX_INPUT_DUAL ? "(*)" : "( )");
        }
    }

    if (st->running)
        snprintf(buf, sizeof(buf), "%s | %s",
                 msx_basename(st->path),
                 dsk_menu_active() ? "ディスク" : (st->paused ? "停止" : "実行"));
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

    if (st->running && dsk_menu_active())
        snprintf(msg, sizeof(msg),
                 "B-MSX | %s | ディスク | Up Down Enter",
                 msx_basename(st->path));
    else if (st->running) {
        const char *mode_str = (st->input_mode == MSX_INPUT_KBD_PRIORITY)
            ? "矢印/Z/X=キーボード (F2=面再開)"
            : (st->input_mode == MSX_INPUT_JOY_PRIORITY)
            ? "矢印=ジョイスティックA  Z=ボタンA  X=ボタンB"
            : "両方 (Dual Matrix+Joy)";
        snprintf(msg, sizeof(msg),
                 "B-MSX | %s | T=%llu T-states | %s | %s",
                 msx_basename(st->path),
                 (unsigned long long)msx_total_cycles(),
                 st->paused ? "一時停止" : "実行中",
                 mode_str);
    }
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

    /* The tick task is the pacer: with one running, paint advances the machine
     * only when frames are due, handling catch-up cleanly so neither audio nor video
     * stutters under host VSync or key event delays. Where no task was created,
     * every repaint is a frame. */
    int due = s_frames_due;
    if (s_tsk <= 0 && due < 1) due = 1;
    if (due > 3) due = 3; /* cap catch-up per paint to keep frame rate responsive */
    if (st->running && !st->paused && due > 0) {
        s_frames_due -= due;
        if (s_frames_due < 0) s_frames_due = 0;
        for (int step = 0; step < due; step++) {
            if (st->mouse_decay > 0) {
                st->mouse_decay--;
                if (st->mouse_decay == 0) {
                    st->joy_mouse &= (uint8_t)~(MSX_JOY_UP | MSX_JOY_DOWN | MSX_JOY_LEFT | MSX_JOY_RIGHT);
                    st->joy = st->joy_keys | st->joy_mouse;
                    msx_joy_set(st->joy);
                }
            }
            msx_run_frame();
            msx_host_pump_audio(1);
            st->frames++;
        }
        msx_blit_rgb565(s_fb);
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
            /* Both builds: the cascade's rows come from the same catalogue the
             * window was opened with, wherever that catalogue was listed from. */
            if (sub_idx >= 0 && sub_idx < st->rom_count) msx_load(st, st->roms[sub_idx]);
            break;
        case XCMD_FILE_RESET:
            if (st->running) {
                msx_reset();
                msx_host_ac_reset();
                st->frames = 0;
                st->joy = 0u;
                st->joy_keys = 0u;
                st->joy_mouse = 0u;
                st->mouse_decay = 0u;
                msx_joy_set(0u);
                s_frames_due = 0;
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
                s_frames_due = 1;
            }
            break;
        case XCMD_EMU_FAST_FORWARD:
            if (st->running && !st->paused) {
                for (int i = 0; i < 300; i++) {
                    msx_run_frame();
                    msx_host_pump_audio(0);
                }
                s_frames_due = 1;
            }
            break;
        case XCMD_VIEW_FIT: st->scale = 0; break;
        case XCMD_VIEW_1X:  st->scale = 1; break;
        case XCMD_VIEW_2X:  st->scale = 2; break;
        case XCMD_VIEW_3X:  st->scale = 3; break;
        case XCMD_VIEW_4X:  st->scale = 4; break;
        case XCMD_INPUT_KBD_MODE:
            st->input_mode = MSX_INPUT_KBD_PRIORITY;
            st->joy_keys = 0u;
            st->joy = st->joy_keys | st->joy_mouse;
            msx_joy_set(st->joy);
            break;
        case XCMD_INPUT_JOY_MODE:
            st->input_mode = MSX_INPUT_JOY_PRIORITY;
            kbd_reset();
            break;
        case XCMD_INPUT_DUAL_MODE:
            st->input_mode = MSX_INPUT_DUAL;
            break;
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
    { BTRON_KEY_UP,        MSX_JOY_UP,    MSX_KEY_UP_R,    MSX_KEY_UP_C },
    { BTRON_KEY_DOWN,      MSX_JOY_DOWN,  MSX_KEY_DOWN_R,  MSX_KEY_DOWN_C },
    { BTRON_KEY_LEFT,      MSX_JOY_LEFT,  MSX_KEY_LEFT_R,  MSX_KEY_LEFT_C },
    { BTRON_KEY_RIGHT,     MSX_JOY_RIGHT, MSX_KEY_RIGHT_R, MSX_KEY_RIGHT_C },
    { BTRON_KEY_SPACE,     0u,            MSX_KEY_SPACE_R, MSX_KEY_SPACE_C },
    { ' ',                 0u,            MSX_KEY_SPACE_R, MSX_KEY_SPACE_C },
    { 'z',                 MSX_JOY_TRGA,  5u, 7u },
    { 'Z',                 MSX_JOY_TRGA,  5u, 7u },
    { 'x',                 MSX_JOY_TRGB,  5u, 5u },
    { 'X',                 MSX_JOY_TRGB,  5u, 5u },
    { BTRON_KEY_RETURN,    0u, MSX_KEY_ENTER_R,  MSX_KEY_ENTER_C },
    { '\r',                0u, MSX_KEY_ENTER_R,  MSX_KEY_ENTER_C },
    { '\n',                0u, MSX_KEY_ENTER_R,  MSX_KEY_ENTER_C },
    { BTRON_KEY_KP_ENTER,  0u, MSX_KEY_ENTER_R,  MSX_KEY_ENTER_C },
    { BTRON_KEY_BACKSPACE, 0u, MSX_KEY_BS_R,     MSX_KEY_BS_C },
    { 0x08,                0u, MSX_KEY_BS_R,     MSX_KEY_BS_C },
    { BTRON_KEY_TAB,       0u, MSX_KEY_TAB_R,    MSX_KEY_TAB_C },
    { '\t',                0u, MSX_KEY_TAB_R,    MSX_KEY_TAB_C },
    { BTRON_KEY_ESCAPE,    0u, MSX_KEY_ESC_R,    MSX_KEY_ESC_C },
    { 27,                  0u, MSX_KEY_ESC_R,    MSX_KEY_ESC_C },
    { BTRON_KEY_DELETE,    0u, MSX_KEY_DEL_R,    MSX_KEY_DEL_C },
    { 127,                 0u, MSX_KEY_DEL_R,    MSX_KEY_DEL_C },
    { BTRON_KEY_HOME,      0u, MSX_KEY_HOME_R,   MSX_KEY_HOME_C },
    { BTRON_KEY_F1,        0u, MSX_KEY_F1_R,     MSX_KEY_F1_C },
    { BTRON_KEY_F2,        0u, MSX_KEY_F2_R,     MSX_KEY_F2_C },
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

    /* The disk panel owns Up, Down and Enter until a file is started.
     * The menu bar still sees its keys first, in msx_handle_event. */
    if (down && dsk_menu_active()) {
        if (key == BTRON_KEY_UP) { dsk_menu_key(0); return; }
        if (key == BTRON_KEY_DOWN) { dsk_menu_key(1); return; }
        if (key == BTRON_KEY_RETURN || key == '\r' || key == '\n') { dsk_menu_key(2); return; }
    }

    for (size_t i = 0; i < sizeof(s_keymap) / sizeof(s_keymap[0]); i++) {
        if (s_keymap[i].key != key) continue;

        /* In Joypad or Dual mode, keys with a joypad mapping drive Joypad Port 1 */
        if (s_keymap[i].joy && (st->input_mode == MSX_INPUT_JOY_PRIORITY ||
                                st->input_mode == MSX_INPUT_DUAL)) {
            if (down) st->joy_keys |= s_keymap[i].joy;
            else      st->joy_keys &= (uint8_t)~s_keymap[i].joy;
            st->joy = st->joy_keys | st->joy_mouse;
            msx_joy_set(st->joy);
        }

        /* In Keyboard or Dual mode, keys assert MSX keyboard matrix.
         * For keys without a joypad assignment (e.g. F1-F5, Enter, Esc), always assert matrix. */
        BOOL assert_matrix = (st->input_mode == MSX_INPUT_KBD_PRIORITY ||
                              st->input_mode == MSX_INPUT_DUAL ||
                              !s_keymap[i].joy);
        if (assert_matrix && s_keymap[i].row != 0xFFu) {
            if (down) msx_kbd_down(s_keymap[i].row, s_keymap[i].col);
            else      msx_kbd_up(s_keymap[i].row, s_keymap[i].col);
        }
        return;
    }

    if (msx_kbd_position((char)key, &row, &col)) {
        if (down) msx_kbd_down(row, col);
        else      msx_kbd_up(row, col);
    }
}

/* ── Events ──────────────────────────────────────────────────────────────── */

static void msx_update_host_mouse(WND *wnd, MsxViewer *st, H rel_x, H rel_y)
{
    MsxView v;
    int scr_w, scr_h, mx, my;

    if (!wnd || !wnd->dev || !st->running) return;
    msx_compute_view(wnd->dev, &v);
    scr_w = (int)MSX_SCR_W * v.scale;
    scr_h = (int)MSX_SCR_H * v.scale;
    if (scr_w <= 0 || scr_h <= 0) return;

    mx = ((int)rel_x - v.x) * 512 / scr_w;
    my = ((int)rel_y - v.y) * 212 / scr_h;
    if (mx < 0) mx = 0;
    if (mx > 511) mx = 511;
    if (my < 0) my = 0;
    if (my > 211) my = 211;

    msx_mouse_set((uint16_t)mx, (uint16_t)my, st->mouse_buttons);
}

static void msx_handle_event(WND *wnd, const EVT *evt)
{
    MsxViewer *st = &s_msx;
    H rel_x, rel_y;

    if (!wnd || !evt) return;
    rel_x = (H)(evt->pos.x - (wnd->bounds.left + 4));
    rel_y = (H)(evt->pos.y - (wnd->bounds.top + 26));

    if (evt->type == EV_MOUSE_MOVE) {
        if (app_menu_handle_mouse_move(&st->menu_bar, rel_x, rel_y)) {
            inval_wnd(wnd);
            return;
        }
        if (st->running && rel_y >= MSX_MENU_H) {
            msx_update_host_mouse(wnd, st, rel_x, rel_y);
        }
        return;
    }

    if (evt->type == EV_BUT_DOWN) {
        int cmd = 0, sub = -1;
        if (app_menu_handle_mouse_down(&st->menu_bar, rel_x, rel_y, &cmd, &sub)) {
            msx_apply_cmd(wnd, st, cmd, sub);
            inval_wnd(wnd);
            return;
        }
        if (st->menu_bar.active_menu >= 0) {
            app_menu_close(&st->menu_bar);
            inval_wnd(wnd);
            return;
        }
        /* Click inside running emulator forwards proportional mouse + Trigger A */
        if (st->running) {
            st->mouse_buttons |= 0x01u;
            msx_update_host_mouse(wnd, st, rel_x, rel_y);
            st->joy_mouse |= MSX_JOY_TRGA;
            st->joy = st->joy_keys | st->joy_mouse;
            msx_joy_set(st->joy);
        }
        return;
    }

    if (evt->type == EV_BUT_UP) {
        if (st->running) {
            st->mouse_buttons &= (uint8_t)~0x01u;
            msx_update_host_mouse(wnd, st, rel_x, rel_y);
            st->joy_mouse &= (uint8_t)~MSX_JOY_TRGA;
            st->joy = st->joy_keys | st->joy_mouse;
            msx_joy_set(st->joy);
        }
        return;
    }

    if (evt->type == EV_KEY_DOWN) {
        int cmd = 0;
        /* When running and dropdown is closed, route F2 directly to MSX matrix */
        if (st->running && st->menu_bar.active_menu < 0 &&
            (evt->key == BTRON_KEY_F2 || evt->key == 0x4000003Bu)) {
            msx_key(st, evt->key, TRUE);
            return;
        }
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
    SYSTIME next = 0;
    unsigned phase = 0u;
    static const int step[3] = { 17, 17, 16 };

    (void)exinf;
    get_tim(&next);
    while (s_wnd) {
        SYSTIME now = 0;
        int delay;

        if (s_frames_due < 5) s_frames_due++;
        inval_wnd(s_wnd);
        next += (SYSTIME)step[phase];
        phase = phase < 2u ? phase + 1u : 0u;
        get_tim(&now);
        if (now >= next) next = now; /* late: resync, the flag coalesces */
        delay = (int)(next - now);
        if (delay < 1) delay = 1;    /* dly_tsk(0) would spin */
        if (delay > 30) {            /* wall clock stepped backwards */
            next = now;
            delay = 1;
        }
        dly_tsk(delay);
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
    s_frames_due = 0;
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
                 "C-BIOS が読み込めません (BIOS missing under %s)",
                 MSX_BIOS_DEFAULT_DIR);
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
    ctsk.name    = "msx_task";    /* what `ps` shows; see T_CTSK.name */
    s_tsk = cre_tsk(&ctsk);
    if (s_tsk > 0) sta_tsk(s_tsk, 0);

    return wnd;
}

WND *open_msx_window(void)
{
    return msx_open(NULL);
}

/* `path` may be a full path or a bare file name; it is resolved against
 * MSX_ROM_CATALOGUE, the same catalogue the ROM cascade and the XMB Games band
 * list. */
WND *open_msx_window_with_rom(const char *path)
{
    const char *name = path;
    if (name) {
        const char *slash = strrchr(name, '/');
        if (slash) name = slash + 1;
    }
    return msx_open(name);
}

void msx_app_fast_forward(int frames)
{
    if (s_msx.running && !s_msx.paused) {
        for (int i = 0; i < frames; i++) {
            msx_run_frame();
            msx_host_pump_audio(0);
        }
        s_frames_due = 1;
        if (s_wnd) inval_wnd(s_wnd);
    }
}

