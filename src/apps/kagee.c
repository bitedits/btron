#include <btron/apps.h>
#include <btron/wnd.h>
#include <btron/dp.h>
#include <btron/troncode.h>
#include <device/virtio.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define KAGEE_W 640
#define KAGEE_H 430
#define KAGEE_VIDEO_W 96
#define KAGEE_VIDEO_H 64
#define KAGEE_FRAMES 6571
#define KAGEE_SOURCE_RATE 7200
#define KAGEE_AUDIO_RATE 44100
#define KAGEE_AUDIO_OUT_FRAMES 1470
#define KAGEE_AUDIO_BYTES 120

typedef struct KageeNode {
    int value;
    struct KageeNode *zero;
    struct KageeNode *one;
} KageeNode;

static WND *g_kagee_wnd;
static FILE *g_video;
static FILE *g_audio;
static KageeNode g_nodes[511];
static int g_node_count;
static uint32_t g_bitbuf;
static int g_bit_count;
static long g_video_payload;
static uint32_t g_video_start_buf;
static int g_video_start_bits;
static unsigned char g_fb[1024];
static uint32_t g_frame;
static uint32_t g_paint_ticks;
static int g_debug_logged;
static int g_audio_write_result;
static int16_t g_audio_pcm[KAGEE_AUDIO_OUT_FRAMES * 2];

static int kagee_bit(void) {
    if (g_bit_count == 0) {
        int a = fgetc(g_video), b = fgetc(g_video);
        if (a == EOF || b == EOF) return 0;
        g_bitbuf = ((uint32_t)(unsigned)a << 8) | (unsigned)b;
        g_bit_count = 16;
    }
    g_bit_count--;
    return (int)((g_bitbuf >> g_bit_count) & 1u);
}

static void kagee_read_tree(KageeNode *n) {
    if (!kagee_bit()) {
        n->value = fgetc(g_video);
        n->zero = n->one = NULL;
        return;
    }
    n->value = 0;
    n->zero = &g_nodes[++g_node_count];
    kagee_read_tree(n->zero);
    n->one = &g_nodes[++g_node_count];
    kagee_read_tree(n->one);
}

static unsigned char kagee_huff(void) {
    KageeNode *n = &g_nodes[0];
    while (n->zero) n = kagee_bit() ? n->one : n->zero;
    return (unsigned char)n->value;
}

static int kagee_open_files(void) {
    printf("[KAGEE-02] opening assets\n");
    g_video = fopen("assets/kagee/BADAPPLE.vid", "rb");
    if (!g_video) g_video = fopen("./assets/kagee/BADAPPLE.vid", "rb");
    g_audio = fopen("assets/kagee/BADAPPLE.aud", "rb");
    if (!g_audio) g_audio = fopen("./assets/kagee/BADAPPLE.aud", "rb");
    if (!g_video || !g_audio) { printf("[KAGEE-03] asset open failed: video=%p audio=%p\n", (void*)g_video, (void*)g_audio); return 0; }
    printf("[KAGEE-04] assets opened\n");
    g_node_count = 0;
    g_bitbuf = 0;
    g_bit_count = 0;
    kagee_read_tree(&g_nodes[0]);
    g_video_payload = ftell(g_video);
    g_video_start_buf = g_bitbuf;
    g_video_start_bits = g_bit_count;
    printf("[KAGEE-05] Huffman tree ready: nodes=%d payload=%ld remainder_bits=%d\n", g_node_count + 1, g_video_payload, g_video_start_bits);
    return g_video_payload >= 0;
}

static void kagee_restart(void) {
    if (!g_video || !g_audio) return;
    fseek(g_video, g_video_payload, SEEK_SET);
    fseek(g_audio, 0, SEEK_SET);
    g_bitbuf = g_video_start_buf;
    g_bit_count = g_video_start_bits;
    g_frame = 0;
    g_debug_logged = 0;
    g_audio_write_result = -1;
}

static void kagee_decode_frame(void) {
    int x, by, y;
    memset(g_fb, 0xff, sizeof(g_fb));
    for (x = 2; x < 14; x++) {
        int mask = kagee_huff();
        for (by = 0; by < 8; by++) {
            if (mask & (1 << by)) {
                unsigned char *p = g_fb + (by << 7) + x;
                for (y = 0; y < 128; y += 16) { *p = (unsigned char)(*p ^ kagee_huff()); p += 16; }
            }
        }
    }
}

static void kagee_decode_audio(void) {
    unsigned char packed[KAGEE_AUDIO_BYTES];
    int16_t source[KAGEE_AUDIO_BYTES * 2];
    size_t i;
    if (fread(packed, 1, sizeof(packed), g_audio) != sizeof(packed)) {
        fseek(g_audio, 0, SEEK_SET);
        if (fread(packed, 1, sizeof(packed), g_audio) != sizeof(packed)) return;
    }
    for (i = 0; i < sizeof(packed); i++) {
        source[i * 2] = (int16_t)(((int)(packed[i] >> 4) - 8) * 4096);
        source[i * 2 + 1] = (int16_t)(((int)(packed[i] & 15) - 8) * 4096);
    }
    for (i = 0; i < KAGEE_AUDIO_OUT_FRAMES; i++) {
        size_t pos = i * (KAGEE_AUDIO_BYTES * 2u - 1u) / KAGEE_AUDIO_OUT_FRAMES;
        size_t next = pos + 1u;
        size_t frac = (i * (KAGEE_AUDIO_BYTES * 2u - 1u)) % KAGEE_AUDIO_OUT_FRAMES;
        int sample = source[pos] + (int)((source[next] - source[pos]) * frac / KAGEE_AUDIO_OUT_FRAMES);
        g_audio_pcm[i * 2] = (int16_t)sample;
        g_audio_pcm[i * 2 + 1] = (int16_t)sample;
    }
    g_audio_write_result = virtio_sound_write(g_audio_pcm, KAGEE_AUDIO_OUT_FRAMES);
    printf("[KAGEE-07] PCM queue result=%d (16-bit 44.1kHz)\n", g_audio_write_result);
}static void kagee_paint(WND *wnd, GDEV *dev) {
    RECT r;
    int sx, sy, on_count = 0;
    (void)wnd;
    if (!dev) return;
    r.left = 0; r.top = 0; r.right = dev->width; r.bottom = dev->height;
    fill_rec(dev, &r, 0xff080a10);
    drw_tc_string(dev, 20, 16, "KAGEE // BAD APPLE!", COLOR_WHITE, 0);
    drw_tc_string(dev, 20, 36, "96x64 Huffman / 4-bit PCM 7200Hz", COLOR_CYAN, 0);
    if ((g_paint_ticks++ & 1u) == 0u) {
        kagee_decode_frame();
        printf("[KAGEE-06] frame decode complete\n");
        kagee_decode_audio();
    }
    for (sy = 0; sy < KAGEE_VIDEO_H; sy++) {
        for (sx = 0; sx < KAGEE_VIDEO_W; sx++) {
            int idx = (sy % 8) * 128 + (sy / 8) * 16 + 2 + (sx / 8);
            int on = (g_fb[idx] & (1 << (7 - (sx & 7)))) == 0;
            if (on) {
                on_count++;
                r.left = 128 + sx * 4; r.top = 78 + sy * 4;
                r.right = r.left + 4; r.bottom = r.top + 4;
                fill_rec(dev, &r, COLOR_WHITE);
            }
        }
    }
    if (!g_debug_logged) {
        printf("[KAGEE] first frame: white_pixels=%d audio_samples=%d queued_frames=%d first_pcm=%d\n", on_count, KAGEE_AUDIO_OUT_FRAMES, g_audio_write_result, (int)g_audio_pcm[0]);
        g_debug_logged = 1;
    }
    drw_tc_string(dev, 170, 355, "♪ AC100V fx-9860GII decoder  |  ESC closes", COLOR_GOLD, 0);
    g_frame++;
    if (g_frame >= KAGEE_FRAMES) kagee_restart();
}

static void kagee_event(WND *wnd, const EVT *evt) {
    if (!wnd || !evt) return;
    if (evt->type == EV_KEY_DOWN &&
        (evt->key == BTRON_KEY_ESCAPE || evt->key == 'q' || evt->key == 'Q'))
        cls_wnd(wnd);
}

static void kagee_destroy(WND *wnd) {
    (void)wnd;
    virtio_sound_close();
    if (g_video) fclose(g_video);
    if (g_audio) fclose(g_audio);
    g_video = g_audio = NULL;
    g_kagee_wnd = NULL;
}

WND *open_kagee_window(void) {
    if (g_kagee_wnd) {
        top_wnd(g_kagee_wnd);
        return g_kagee_wnd;
    }
    if (!kagee_open_files()) return NULL;
    printf("[KAGEE-07a] opening audio 44100Hz stereo S16\n");
    if (virtio_sound_open(KAGEE_AUDIO_RATE, 2) != 0) {
        printf("[KAGEE-07b] audio open failed\n");
        fclose(g_video); fclose(g_audio);
        g_video = g_audio = NULL;
        return NULL;
    }
    g_frame = 0;
    g_debug_logged = 0;
    g_audio_write_result = -1;
    g_paint_ticks = 0;
    printf("[KAGEE-08] audio ready; creating window\n");
    g_kagee_wnd = opn_wnd("Kagee - Bad Apple!", 250, 120, KAGEE_W, KAGEE_H,
                           WND_ATTR_TITLE | WND_ATTR_CLOSE | WND_ATTR_BORDER);
    if (!g_kagee_wnd) {
        printf("[KAGEE-09] window creation failed\n");
        virtio_sound_close();
        fclose(g_video); fclose(g_audio);
        g_video = g_audio = NULL;
        return NULL;
    }
    printf("[KAGEE-10] window created; player armed\n");
    g_kagee_wnd->paint = kagee_paint;
    g_kagee_wnd->event_handler = kagee_event;
    g_kagee_wnd->destroy = kagee_destroy;
    return g_kagee_wnd;
}
