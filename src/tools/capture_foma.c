/*
 * B-System (BTRON 3.20) Headless Screenshot Capturer for FOMA Mobile UI
 * src/tools/capture_foma.c
 *
 * Captures pixel-perfect 480x640 VGA vertical screen frames directly from C99
 * headless GDEV framebuffer into raw ARGB format for PNG generation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <btron/types.h>
#include <btron/dp.h>
#include <btron/mobile_ui.h>

static void dump_foma_screen(GDEV *dev, const char *label, const char *raw_path) {
    if (!dev) return;

    /* Render current active screen onto headless framebuffer */
    foma_render_desktop(dev, NULL);

    FILE *fp = fopen(raw_path, "wb");
    if (!fp) {
        fprintf(stderr, "Error: failed to open output file %s\n", raw_path);
        return;
    }

    int w = dev->width;
    int h = dev->height;
    fwrite(&w, sizeof(int), 1, fp);
    fwrite(&h, sizeof(int), 1, fp);
    fwrite(dev->pixels, sizeof(COLOR), w * h, fp);
    fclose(fp);

    printf("  [CAPTURED] %-30s -> %s (%dx%d px)\n", label, raw_path, w, h);
}

int main(void) {
    printf("===============================================================\n");
    printf(" Generating Isolated µBTRON-FOMA Mobile UI Screenshots (480x640)\n");
    printf("===============================================================\n");

#ifdef __APPLE__
    mkdir("/tmp/foma_raw_screens", 0777);
#else
    mkdir("/tmp/foma_raw_screens", 0777);
#endif

    GDEV *dev = opn_dev(FOMA_SCREEN_W, FOMA_SCREEN_H);
    if (!dev) {
        fprintf(stderr, "Fatal: failed to allocate GDEV for FOMA capture.\n");
        return 1;
    }

    /* 1. Main (Home Cabinet / 起動キャビネット) */
    foma_ui_init();
    foma_show_home_cabinet();
    dump_foma_screen(dev, "1. Main (Home Cabinet)", "/tmp/foma_raw_screens/foma_main.raw");

    /* 2. Contacts Explorer (連絡先キャビネット) */
    foma_ui_init();
    foma_show_contacts();
    dump_foma_screen(dev, "2. Contacts Explorer", "/tmp/foma_raw_screens/foma_contacts.raw");

    /* 3. Memos Explorer (メモ / Memos) */
    foma_ui_init();
    foma_show_memos();
    dump_foma_screen(dev, "3. Memos Explorer", "/tmp/foma_raw_screens/foma_memo.raw");

    /* 4. T-Editor Text (基本エディタ) */
    foma_ui_init();
    foma_show_editor_sample();
    dump_foma_screen(dev, "4. T-Editor Viewer", "/tmp/foma_raw_screens/foma_editor.raw");

    /* 5. Control Panel (コントロールパネル) */
    foma_ui_init();
    foma_show_control_panel();
    dump_foma_screen(dev, "5. Control Panel", "/tmp/foma_raw_screens/foma_control_panel.raw");

    /* 6. Other Apps (アプリ / Applications Launcher) */
    foma_ui_init();
    foma_show_apps_launcher();
    dump_foma_screen(dev, "6. Apps Launcher", "/tmp/foma_raw_screens/foma_other_apps.raw");

    /* Bonus: Contact Detail with [仮身] links */
    foma_ui_init();
    foma_show_contact_detail_sample();
    dump_foma_screen(dev, "Bonus: Contact Detail [仮身]", "/tmp/foma_raw_screens/foma_contact_detail.raw");

    /* Bonus: Device Info (端末情報) */
    foma_ui_init();
    foma_show_device_info();
    dump_foma_screen(dev, "Bonus: Device Info", "/tmp/foma_raw_screens/foma_device_info.raw");

    /* Bonus: System Menu (システムメニュー) */
    foma_ui_init();
    foma_show_system_menu();
    dump_foma_screen(dev, "Bonus: System Menu", "/tmp/foma_raw_screens/foma_system_menu.raw");

    /* Bonus: Calculator (計算機) */
    foma_ui_init();
    foma_show_calculator();
    dump_foma_screen(dev, "Bonus: Calculator", "/tmp/foma_raw_screens/foma_calculator.raw");

    cls_dev(dev);
    printf("===============================================================\n");
    printf(" FOMA Screen captures dumped successfully to /tmp/foma_raw_screens/\n");
    printf("===============================================================\n");

    return 0;
}
