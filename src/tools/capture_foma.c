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

/* Minimal stubs for GTerm interactive commands in headless tool mode */
void btron_core_print_ver(void *out_fn, void *user_data, const char *arg) { (void)out_fn; (void)user_data; (void)arg; }
void sys_get_devconf(void *p) { (void)p; }
void sys_get_mem_stats(void *p) { (void)p; }
void sys_mouse_get_pos(int *x, int *y) { if (x) *x = 0; if (y) *y = 0; }
void sys_mouse_set_pos(int x, int y) { (void)x; (void)y; }
void sys_mouse_click(int b) { (void)b; }
void open_audio_player_window(void) {}
void open_tad_browser_window(void) {}
void launch_beos_chat(void) {}

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

    /* Tier 1 App: gterm Live Terminal (端末シェル) */
    foma_ui_init();
    foma_show_terminal();
    dump_foma_screen(dev, "Tier 1: gterm Console", "/tmp/foma_raw_screens/foma_terminal.raw");

    /* ── Opened Menus & Dialogs Showcase ── */

    /* 7. Opened Floating Context Popup Menu (浮動操作メニュー) over Home Cabinet */
    foma_ui_init();
    foma_show_home_cabinet();
    {
        const char *pop_items[] = {
            "1. 開く (Open)",
            "2. 詳細属性 (Properties)",
            "3. 実身検索 (Search)",
            "4. 仮身作成 (Create Fusen)",
            "5. 実身削除 (Delete)",
            "6. 電源管理 (Power)"
        };
        const char *pop_badges[] = {
            "選択", "属性", "TIP", "仮身", "警告", "88%"
        };
        foma_show_popup_menu("操作メニュー (1-6) / Actions", pop_items, pop_badges, 6, 1);
    }
    dump_foma_screen(dev, "7. Opened Menu: Context Popup", "/tmp/foma_raw_screens/foma_menu_popup.raw");

    /* 8. Opened Delete Confirmation Dialog (実身削除確認ダイアログ) over Contacts */
    foma_ui_init();
    foma_show_contacts();
    foma_show_confirm_dialog("実身削除の確認", "連絡先：坂村 健",
                             "この実身を完全に削除しますか？\n※ 関連するすべての仮身(Fusen)リンクが\n　 参照不能（リンク切れ）になります。",
                             NULL, NULL);
    dump_foma_screen(dev, "8. Opened Dialog: Delete Confirm", "/tmp/foma_raw_screens/foma_dialog_confirm.raw");

    /* 9. Opened Real Body Properties Sheet (実身属性詳細シート) over Memos */
    foma_ui_init();
    foma_show_memos();
    foma_show_properties_dialog("メモ：BTRON FOMA設計", "0x002A-8F14-C001",
                                "TAD Rev 3.20 (テキスト+仮身)", 4896, 3, "2026-09-05 14:32:08");
    dump_foma_screen(dev, "9. Opened Dialog: Properties Sheet", "/tmp/foma_raw_screens/foma_dialog_properties.raw");

    /* 10. Opened Search Dialog with TIP Virtual IME (実身検索・TIP入力) over Home */
    foma_ui_init();
    foma_show_home_cabinet();
    foma_show_search_dialog("坂村 健", "[あ/漢] TIP/Mozc");
    dump_foma_screen(dev, "10. Opened Dialog: Search & IME", "/tmp/foma_raw_screens/foma_dialog_search.raw");

    /* 11. Opened Power Management Dialog (電源管理・サスペンド) over Control Panel */
    foma_ui_init();
    foma_show_control_panel();
    foma_show_power_dialog(88, "4.12V");
    dump_foma_screen(dev, "11. Opened Dialog: Power & Sleep", "/tmp/foma_raw_screens/foma_dialog_power.raw");

    /* 12. Opened 3G Voice Call Alert Notification (3G 音声着信呼出) over Home */
    foma_ui_init();
    foma_show_home_cabinet();
    foma_show_call_dialog("坂村 健 (Ken Sakamura)", "090-XXXX-XXXX", "00:06");
    dump_foma_screen(dev, "12. Opened Dialog: 3G Incoming Call", "/tmp/foma_raw_screens/foma_dialog_call.raw");

    cls_dev(dev);
    printf("===============================================================\n");
    printf(" FOMA Screen captures dumped successfully to /tmp/foma_raw_screens/\n");
    printf("===============================================================\n");

    return 0;
}
