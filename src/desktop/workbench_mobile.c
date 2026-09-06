/*
 * B-System (BTRON 3.20) Mobile Workbench Coordinator: workbench_mobile.c
 *
 * Implements full FOMA HMI interaction model and all screens from FOMA.md:
 * - 3.1 Workbench / Home Cabinet (起動キャビネット)
 * - 3.2 System Menu (システムメニュー)
 * - 3.3 Control Panel (コントロールパネル) & Settings Sub-dialogs
 * - 3.4 Apps Launcher (アプリ) & TAD Browser Stub
 * - 3.5 Contacts Explorer (連絡先キャビネット) with Real/Virtual Body Links
 * - 3.6 Memos Explorer (メモ) & Viewer
 * - Device Info (端末情報)
 *
 * Keypad & touch dispatch: 5-way D-pad, 3 soft keys, numeric accelerators, mouse clicks.
 * NASA JPL Rule 3 compliant: Bounded state, zero post-boot heap allocations.
 */

#include <btron/mobile_ui.h>
#include <btron/troncode.h>
#include <btron/event.h>

#ifndef SDLK_UP
#define SDLK_UP        BTRON_KEY_UP
#define SDLK_DOWN      BTRON_KEY_DOWN
#define SDLK_LEFT      BTRON_KEY_LEFT
#define SDLK_RIGHT     BTRON_KEY_RIGHT
#define SDLK_RETURN    BTRON_KEY_RETURN
#define SDLK_SPACE     BTRON_KEY_SPACE
#define SDLK_ESCAPE    BTRON_KEY_ESCAPE
#define SDLK_BACKSPACE BTRON_KEY_BACKSPACE
#define SDLK_F1        BTRON_KEY_F1
#define SDLK_F2        BTRON_KEY_F2
#define SDLK_F3        BTRON_KEY_F3
#endif
#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#else
#include <libstr.h>
#define snprintf tkl_snprintf
#define strlen   tkl_strlen
#define strncpy  tkl_strncpy
#define strcmp   tkl_strcmp
#endif

/* Forward declarations for screen constructors */
static void show_home_cabinet_screen(void);
static void show_system_menu_screen(void);
static void show_control_panel_screen(void);
static void show_apps_launcher_screen(void);
static void show_contacts_screen(void);
static void show_memos_screen(void);
static void show_device_info_screen(void);
static void show_contact_detail_screen(const char *name, const char *phone, const char *org, const char *memo);
static void show_memo_detail_screen(const char *title, const char *date, const char *body);
static void show_app_info_screen(const char *name, const char *desc, const char *status);
static void show_settings_sub_screen(int setting_id);
static void show_calculator_screen(void);
static void show_terminal_screen(void);

/* ── Screen ID Enumeration ── */
enum {
    SCR_HOME = 1,
    SCR_SYSTEM_MENU,
    SCR_CONTROL_PANEL,
    SCR_APPS_LAUNCHER,
    SCR_CONTACTS,
    SCR_MEMOS,
    SCR_DEVICE_INFO,
    SCR_CONTACT_DETAIL,
    SCR_MEMO_DETAIL,
    SCR_APP_INFO,
    SCR_SETTINGS_SUB,
    SCR_CALCULATOR,
    SCR_TERMINAL
};

/* ── Home Cabinet Actions ── */
static void on_home_item_selected(FOMA_SCREEN *scr, int idx) {
    (void)scr;
    switch (idx) {
        case 0: show_contacts_screen(); break;
        case 1: show_memos_screen(); break;
        case 2:
            show_memo_detail_screen("予定: 明日の技術会議", "2026-09-07 14:00",
                "場所: YRP研究所 第3会議室\n"
                "議題: µBTRON-FOMA 実装報告\n"
                "出席: 坂村健, Namdak Tonpa, 横山孝徳\n"
                "[仮身] 連絡先:坂村健\n"
                "[仮身] メモ:BTRON FOMA設計");
            break;
        case 3:
            show_memo_detail_screen("文書: BTRON3 仕様解説書", "2026-09-01",
                "B-System TRON Application Databus (TAD)\n"
                "第3版 実身・仮身データモデル規定\n"
                "ITU-T / UMTS FOMA 統合プロファイル\n"
                "[仮身] メモ:TAD変換メモ");
            break;
        case 4: show_apps_launcher_screen(); break;
        case 5: show_control_panel_screen(); break;
        case 6: show_device_info_screen(); break;
        default: break;
    }
}

/* ── 3.1 Home Cabinet Screen (起動キャビネット) ── */
static void show_home_cabinet_screen(void) {
    FOMA_SCREEN scr;
    memset(&scr, 0, sizeof(scr));
    scr.screen_id = SCR_HOME;
    strncpy(scr.title, "実身キャビネット / Workbench", sizeof(scr.title) - 1);
    strncpy(scr.subtitle, "7 items", sizeof(scr.subtitle) - 1);

    strncpy(scr.softkey_left, "[選択]", sizeof(scr.softkey_left) - 1);
    strncpy(scr.softkey_center, "[メニュー]", sizeof(scr.softkey_center) - 1);
    strncpy(scr.softkey_right, "[終了]", sizeof(scr.softkey_right) - 1);

    scr.item_count = 7;
    scr.focus_index = 0;

    const char *labels[] = {
        "連絡先 (Contacts)",
        "メモ (Memos)",
        "予定 (Schedule)",
        "文書 (Documents)",
        "アプリ (Apps)",
        "設定 (Control Panel)",
        "端末情報 (Device Info)"
    };
    const char *badges[] = { "48", "23", "12", "17", "8", "", "OMAP" };

    for (int i = 0; i < scr.item_count; i++) {
        strncpy(scr.items[i].title, labels[i], sizeof(scr.items[i].title) - 1);
        strncpy(scr.items[i].badge, badges[i], sizeof(scr.items[i].badge) - 1);
        scr.items[i].type = FOMA_ITEM_NORMAL;
        scr.items[i].action = on_home_item_selected;
    }

    foma_push_screen(&scr);
}

/* ── 3.2 System Menu Actions ── */
static void on_confirm_exit(void) {
    exit(0);
}

static void on_sys_menu_item_selected(FOMA_SCREEN *scr, int idx) {
    (void)scr;
    switch (idx) {
        case 0: /* 新規実身 */
            foma_show_modal("新規実身の作成", "新規の実身オブジェクトを\nキャビネット内に生成しました。\n[実身ID: RB-0482]", "了解", "閉じる", NULL, NULL);
            break;
        case 1: /* 開く */
            foma_pop_screen();
            foma_nav_activate_selected(foma_get_active_screen());
            break;
        case 2: /* 検索 */
            foma_show_modal("実身・仮身の検索", "全キャビネット検索\n一致件数: 104 件の実身・仮身\n検索インデックス: 最新", "確認", "戻る", NULL, NULL);
            break;
        case 3: /* 仮身リンク作成 */
            foma_show_modal("仮身リンク作成", "選択中の実身に対する\n仮身リンク（Fusen）を\nクリップボードに作成しました。", "了解", "閉じる", NULL, NULL);
            break;
        case 4: /* コピー / 移動 */
            foma_show_modal("コピー / 移動", "実身・仮身の保管場所\n変更先キャビネットを選択してください。", "選択", "戻る", NULL, NULL);
            break;
        case 5: /* 削除 */
            foma_show_modal("実身の削除", "選択されたオブジェクトを\nごみ箱へ移動しますか？", "削除", "取消", NULL, NULL);
            break;
        case 7: /* コントロールパネル */
            foma_pop_screen();
            show_control_panel_screen();
            break;
        case 8: /* 電源管理 */
            foma_show_modal("電源管理 / Power", "バッテリー残量: 92%\n消費電力モード: 標準省電力\nバックライト自動消灯: 30秒", "設定", "戻る", NULL, NULL);
            break;
        case 9: /* 終了 / サスペンド */
            foma_show_modal("システムの終了", "µBTRON-FOMA を終了して\nサスペンド待機状態にしますか？", "終了", "取消", on_confirm_exit, NULL);
            break;
        default: break;
    }
}

/* ── 3.2 System Menu Screen (システムメニュー) ── */
static void show_system_menu_screen(void) {
    FOMA_SCREEN scr;
    memset(&scr, 0, sizeof(scr));
    scr.screen_id = SCR_SYSTEM_MENU;
    strncpy(scr.title, "メニュー / System Menu", sizeof(scr.title) - 1);
    strncpy(scr.subtitle, "", sizeof(scr.subtitle) - 1);

    strncpy(scr.softkey_left, "[決定]", sizeof(scr.softkey_left) - 1);
    strncpy(scr.softkey_center, "", sizeof(scr.softkey_center) - 1);
    strncpy(scr.softkey_right, "[キャンセル]", sizeof(scr.softkey_right) - 1);

    scr.item_count = 10;
    scr.focus_index = 0;

    const char *labels[] = {
        "新規実身 (New Real Body)",
        "開く (Open)",
        "検索 (Search)",
        "仮身リンク作成 (Create V-Body)",
        "コピー / 移動",
        "削除 (Delete)",
        "───────────────",
        "コントロールパネル",
        "電源管理 (Power)",
        "終了 / サスペンド"
    };

    for (int i = 0; i < scr.item_count; i++) {
        strncpy(scr.items[i].title, labels[i], sizeof(scr.items[i].title) - 1);
        if (i == 6) {
            scr.items[i].type = FOMA_ITEM_SEPARATOR;
        } else {
            scr.items[i].type = FOMA_ITEM_NORMAL;
            scr.items[i].action = on_sys_menu_item_selected;
        }
    }

    foma_push_screen(&scr);
}

/* ── 3.3 Control Panel Actions ── */
static void on_control_panel_item_selected(FOMA_SCREEN *scr, int idx) {
    (void)scr;
    show_settings_sub_screen(idx);
}

/* ── 3.3 Control Panel Screen (コントロールパネル) ── */
static void show_control_panel_screen(void) {
    FOMA_SCREEN scr;
    memset(&scr, 0, sizeof(scr));
    scr.screen_id = SCR_CONTROL_PANEL;
    strncpy(scr.title, "コントロールパネル", sizeof(scr.title) - 1);
    strncpy(scr.subtitle, "9 items", sizeof(scr.subtitle) - 1);

    strncpy(scr.softkey_left, "[選択]", sizeof(scr.softkey_left) - 1);
    strncpy(scr.softkey_center, "", sizeof(scr.softkey_center) - 1);
    strncpy(scr.softkey_right, "[戻る]", sizeof(scr.softkey_right) - 1);

    scr.item_count = 9;
    scr.focus_index = 0;

    const char *labels[] = {
        "表示設定 (Display)",
        "入力設定 (Input / TIP / MOZC)",
        "電源・スリープ",
        "音量・バイブ",
        "ネットワーク (FOMA / i-mode)",
        "日時・時計",
        "言語・TRON Code 平面",
        "アクセシビリティ (EnableWare)",
        "バージョン情報"
    };
    const char *badges[] = { "VGA", "TIP", "省電力", "通常", "3G", "JST", "多国語", "ON", "v3.20" };

    for (int i = 0; i < scr.item_count; i++) {
        strncpy(scr.items[i].title, labels[i], sizeof(scr.items[i].title) - 1);
        strncpy(scr.items[i].badge, badges[i], sizeof(scr.items[i].badge) - 1);
        scr.items[i].type = FOMA_ITEM_NORMAL;
        scr.items[i].action = on_control_panel_item_selected;
    }

    foma_push_screen(&scr);
}

/* ── 3.4 Apps Launcher Actions ── */
static void on_app_launcher_item_selected(FOMA_SCREEN *scr, int idx) {
    (void)scr;
    switch (idx) {
        case 0: /* T-Editor */
            show_memo_detail_screen("T-Editor — 基本エディタ", "2026-09-06",
                "1: /* Sakamura B-TRON T-Editor */\n"
                "2: #include <tk/tkernel.h>\n"
                "3: int main(void) {\n"
                "4:   btron_core_init();\n"
                "5:   [仮身] 実身キャビネット\n"
                "6:   [仮身] TAD仕様解説書\n"
                "7:   return 0;\n"
                "8: }\n"
                "[TAD SPEC Rev 3.20 Ready]");
            break;
        case 1: /* gterm */
            show_terminal_screen();
            break;
        case 2: /* TAD Browser Stub */
            show_app_info_screen("TAD Browser",
                "TRON Application Databus Viewer\n\n"
                "※ TAD Browser は次期バージョンにて\n"
                "   本格実装予定です (FOMA.md 準拠)。\n\n"
                "現バージョンでは Real Body / Virtual Body\n"
                "キャビネット閲覧機能をご利用ください。", "準備中");
            break;
        case 3: /* 簡易ペイント */
            show_app_info_screen("簡易ペイント (Paint)",
                "BTRON 簡易ペイントツール\n\n"
                "5方向キーによるピクセル描画と\n"
                "ソフトキーによるパレット選択に対応。\n"
                "解像度: 480x640 32-bpp", "起動完了");
            break;
        case 4: /* XMPP Chat */
            show_memo_detail_screen("XMPP Chat — 会話通信", "接続中: tonpa@btron",
                "[15:20] Sakamura: T-Kernel 2.0 VirtIO runner OK\n"
                "[15:22] Tonpa: FOMA target 480x640 launched\n"
                "[15:24] Yokoyama: Real/Virtual bodies linked\n"
                "[15:25] Sakamura: Mobile UI toolkit looks authentic!\n"
                "[仮身] 連絡先:Namdak Tonpa");
            break;
        case 5: /* Cabinet Explorer */
            show_home_cabinet_screen();
            break;
        case 6: /* 計算機 */
            show_calculator_screen();
            break;
        case 7: /* カメラ */
            show_app_info_screen("カメラ (Camera)",
                "CMOS 130万画素カメラユニット\n\n"
                "静止画解像度: VGA (480x640)\n"
                "フォーカス: パンフォーカス\n"
                "ホワイトバランス: 自動", "待機中");
            break;
        default: break;
    }
}

/* ── 3.4 Apps Launcher Screen (アプリ) ── */
static void show_apps_launcher_screen(void) {
    FOMA_SCREEN scr;
    memset(&scr, 0, sizeof(scr));
    scr.screen_id = SCR_APPS_LAUNCHER;
    strncpy(scr.title, "アプリ / Applications", sizeof(scr.title) - 1);
    strncpy(scr.subtitle, "8 apps", sizeof(scr.subtitle) - 1);

    strncpy(scr.softkey_left, "[起動]", sizeof(scr.softkey_left) - 1);
    strncpy(scr.softkey_center, "[情報]", sizeof(scr.softkey_center) - 1);
    strncpy(scr.softkey_right, "[戻る]", sizeof(scr.softkey_right) - 1);

    scr.item_count = 8;
    scr.focus_index = 0;

    const char *labels[] = {
        "T-Editor (テキスト)",
        "gterm (端末)",
        "TAD Browser",
        "簡易ペイント",
        "XMPP Chat (30KB)",
        "Cabinet Explorer",
        "計算機",
        "カメラ (Camera)"
    };
    const char *badges[] = { "文書", "CLI", "準備中", "描画", "通信", "実身", "計算", "VGA" };

    for (int i = 0; i < scr.item_count; i++) {
        strncpy(scr.items[i].title, labels[i], sizeof(scr.items[i].title) - 1);
        strncpy(scr.items[i].badge, badges[i], sizeof(scr.items[i].badge) - 1);
        scr.items[i].type = FOMA_ITEM_NORMAL;
        scr.items[i].action = on_app_launcher_item_selected;
    }

    foma_push_screen(&scr);
}

/* ── 3.5 Contacts Actions ── */
static void on_contact_item_selected(FOMA_SCREEN *scr, int idx) {
    (void)scr;
    switch (idx) {
        case 4:
            show_contact_detail_screen("坂村 健 (Ken Sakamura)", "090-XXXX-XXXX",
                                       "YRPユビキタス研究所", "TRONプロジェクト創立者・リーダー");
            break;
        case 5:
            show_contact_detail_screen("小島 秀樹 (Hideki Kojima)", "03-XXXX-XXXX",
                                       "東京大学", "BTRON3 端末仕様設計チーム");
            break;
        case 6:
            show_contact_detail_screen("Namdak Tonpa", "xmpp:tonpa@btron",
                                       "Synrc Research Center", "Cleanroom B-System / µBTRON");
            break;
        case 7:
            show_contact_detail_screen("横山 孝徳 (Takanori Yokoyama)", "080-XXXX-XXXX",
                                       "T-Kernel Pioneer", "T-Engine フォーラム幹事");
            break;
        case 8:
            show_contact_detail_screen("内田 公太 (Kota Uchida)", "090-YYYY-YYYY",
                                       "MikanOS Pioneer", "UEFI / OSアーキテクチャ研究者");
            break;
        default: break;
    }
}

/* ── 3.5 Contacts Explorer Screen (連絡先キャビネット) ── */
static void show_contacts_screen(void) {
    FOMA_SCREEN scr;
    memset(&scr, 0, sizeof(scr));
    scr.screen_id = SCR_CONTACTS;
    strncpy(scr.title, "連絡先 / Contacts", sizeof(scr.title) - 1);
    strncpy(scr.subtitle, "48 items", sizeof(scr.subtitle) - 1);

    strncpy(scr.softkey_left, "[詳細]", sizeof(scr.softkey_left) - 1);
    strncpy(scr.softkey_center, "[発信]", sizeof(scr.softkey_center) - 1);
    strncpy(scr.softkey_right, "[戻る]", sizeof(scr.softkey_right) - 1);

    scr.item_count = 9;
    scr.focus_index = 4; /* Focus first actual contact */

    /* Headers */
    strncpy(scr.items[0].title, "あいうえおグループ", sizeof(scr.items[0].title) - 1);
    scr.items[0].type = FOMA_ITEM_HEADER;

    strncpy(scr.items[1].title, "かきくけこグループ", sizeof(scr.items[1].title) - 1);
    scr.items[1].type = FOMA_ITEM_HEADER;

    strncpy(scr.items[2].title, "さしすせそグループ", sizeof(scr.items[2].title) - 1);
    scr.items[2].type = FOMA_ITEM_HEADER;

    /* Separator */
    strncpy(scr.items[3].title, "───────────────", sizeof(scr.items[3].title) - 1);
    scr.items[3].type = FOMA_ITEM_SEPARATOR;

    /* Contact rows */
    struct { const char *name; const char *phone; } contacts[] = {
        { "坂村 健", "090-XXXX-XXXX" },
        { "小島 秀樹", "03-XXXX-XXXX" },
        { "Namdak Tonpa", "xmpp:tonpa@btron" },
        { "横山 孝徳", "080-XXXX-XXXX" },
        { "内田 公太", "090-YYYY-YYYY" }
    };

    for (int i = 0; i < 5; i++) {
        int idx = 4 + i;
        strncpy(scr.items[idx].title, contacts[i].name, sizeof(scr.items[idx].title) - 1);
        strncpy(scr.items[idx].badge, contacts[i].phone, sizeof(scr.items[idx].badge) - 1);
        scr.items[idx].type = FOMA_ITEM_NORMAL;
        scr.items[idx].action = on_contact_item_selected;
    }

    foma_push_screen(&scr);
}

/* ── 3.6 Memos Actions ── */
static void on_memo_item_selected(FOMA_SCREEN *scr, int idx) {
    (void)scr;
    switch (idx) {
        case 0:
            show_memo_detail_screen("BTRON FOMA設計", "2026-09-05",
                "µBTRON-FOMA アーキテクチャ覚書\n\n"
                "1. 5方向キーによる完全キーパッド操作\n"
                "2. 240x320 / 480x640 縦画面対応\n"
                "3. 実身・仮身モデルによる情報整理\n"
                "4. Sakamura T-Kernel 2.0 リアルタイム基盤\n"
                "5. VirtIO MMIO 仮想化対応\n\n"
                "[仮身] 連絡先:坂村健\n"
                "[仮身] 予定:明日の会議");
            break;
        case 1:
            show_memo_detail_screen("仮身リンク実験", "2026-09-04",
                "実身と仮身（Fusen）の連動検証\n\n"
                "仮身をクリックすると対象実身が\n"
                "即座にオープンすることを確認。\n"
                "循環参照や多重リンクも健全に動作。\n\n"
                "[仮身] メモ:TAD変換メモ");
            break;
        case 2:
            show_memo_detail_screen("TAD変換メモ", "2026-09-03",
                "TRON Application Databus (TAD) 互換層\n\n"
                "セグメントレコード: 文字コードプレーン\n"
                "多言語コード: JIS X 0208 / 0212\n"
                "UTF-8 ブリッジ完全双方向変換対応");
            break;
        case 4: /* [仮身] 連絡先:坂村健 */
            show_contact_detail_screen("坂村 健 (Ken Sakamura)", "090-XXXX-XXXX",
                                       "YRPユビキタス研究所", "TRONプロジェクト創立者・リーダー");
            break;
        case 5: /* [仮身] 予定:明日の会議 */
            show_memo_detail_screen("予定: 明日の技術会議", "2026-09-07 14:00",
                "場所: YRP研究所 第3会議室\n"
                "議題: µBTRON-FOMA 実装報告\n"
                "出席: 坂村健, Namdak Tonpa, 横山孝徳");
            break;
        default: break;
    }
}

/* ── 3.6 Memos Explorer Screen (メモ) ── */
static void show_memos_screen(void) {
    FOMA_SCREEN scr;
    memset(&scr, 0, sizeof(scr));
    scr.screen_id = SCR_MEMOS;
    strncpy(scr.title, "メモ / Memos", sizeof(scr.title) - 1);
    strncpy(scr.subtitle, "23 items", sizeof(scr.subtitle) - 1);

    strncpy(scr.softkey_left, "[開く]", sizeof(scr.softkey_left) - 1);
    strncpy(scr.softkey_center, "[新規]", sizeof(scr.softkey_center) - 1);
    strncpy(scr.softkey_right, "[戻る]", sizeof(scr.softkey_right) - 1);

    scr.item_count = 6;
    scr.focus_index = 0;

    const char *labels[] = {
        "2026-09-05  BTRON FOMA設計",
        "2026-09-04  仮身リンク実験",
        "2026-09-03  TAD変換メモ",
        "───────────────",
        "[仮身] 連絡先:坂村健",
        "[仮身] 予定:明日の会議"
    };
    const char *badges[] = { "設計", "実験", "TAD", "", "仮身", "仮身" };

    for (int i = 0; i < scr.item_count; i++) {
        strncpy(scr.items[i].title, labels[i], sizeof(scr.items[i].title) - 1);
        strncpy(scr.items[i].badge, badges[i], sizeof(scr.items[i].badge) - 1);
        if (i == 3) {
            scr.items[i].type = FOMA_ITEM_SEPARATOR;
        } else if (i >= 4) {
            scr.items[i].type = FOMA_ITEM_VIRTUAL_BODY;
            scr.items[i].action = on_memo_item_selected;
        } else {
            scr.items[i].type = FOMA_ITEM_NORMAL;
            scr.items[i].action = on_memo_item_selected;
        }
    }

    foma_push_screen(&scr);
}

/* ── Contact Detail Screen ── */
static void show_contact_detail_screen(const char *name, const char *phone, const char *org, const char *memo) {
    FOMA_SCREEN scr;
    memset(&scr, 0, sizeof(scr));
    scr.screen_id = SCR_CONTACT_DETAIL;
    snprintf(scr.title, sizeof(scr.title), "詳細: %s", name);
    strncpy(scr.subtitle, "実身詳細", sizeof(scr.subtitle) - 1);

    strncpy(scr.softkey_left, "[発信]", sizeof(scr.softkey_left) - 1);
    strncpy(scr.softkey_center, "[編集]", sizeof(scr.softkey_center) - 1);
    strncpy(scr.softkey_right, "[戻る]", sizeof(scr.softkey_right) - 1);

    scr.item_count = 6;
    scr.focus_index = 2;

    snprintf(scr.items[0].title, sizeof(scr.items[0].title), "氏名: %s", name);
    scr.items[0].type = FOMA_ITEM_NORMAL;

    snprintf(scr.items[1].title, sizeof(scr.items[1].title), "所属: %s", org ? org : "-");
    scr.items[1].type = FOMA_ITEM_NORMAL;

    snprintf(scr.items[2].title, sizeof(scr.items[2].title), "[仮身] 電話発信: %s", phone);
    strncpy(scr.items[2].badge, "通話", sizeof(scr.items[2].badge) - 1);
    scr.items[2].type = FOMA_ITEM_VIRTUAL_BODY;

    snprintf(scr.items[3].title, sizeof(scr.items[3].title), "[仮身] メモ: %s", memo ? memo : "備考なし");
    strncpy(scr.items[3].badge, "実身", sizeof(scr.items[3].badge) - 1);
    scr.items[3].type = FOMA_ITEM_VIRTUAL_BODY;

    strncpy(scr.items[4].title, "[仮身] 電子メールを作成", sizeof(scr.items[4].title) - 1);
    strncpy(scr.items[4].badge, "メール", sizeof(scr.items[4].badge) - 1);
    scr.items[4].type = FOMA_ITEM_VIRTUAL_BODY;

    strncpy(scr.items[5].title, "[仮身] スケジュールを確認", sizeof(scr.items[5].title) - 1);
    strncpy(scr.items[5].badge, "予定", sizeof(scr.items[5].badge) - 1);
    scr.items[5].type = FOMA_ITEM_VIRTUAL_BODY;

    foma_push_screen(&scr);
}

/* ── Memo Detail / Text Viewer Screen ── */
static void show_memo_detail_screen(const char *title, const char *date, const char *body) {
    (void)body;
    FOMA_SCREEN scr;
    memset(&scr, 0, sizeof(scr));
    scr.screen_id = SCR_MEMO_DETAIL;
    snprintf(scr.title, sizeof(scr.title), "実身: %s", title);
    strncpy(scr.subtitle, date ? date : "", sizeof(scr.subtitle) - 1);

    strncpy(scr.softkey_left, "[編集]", sizeof(scr.softkey_left) - 1);
    strncpy(scr.softkey_center, "[仮身]", sizeof(scr.softkey_center) - 1);
    strncpy(scr.softkey_right, "[戻る]", sizeof(scr.softkey_right) - 1);

    /* Split body into displayable lines */
    scr.item_count = 0;
    const char *p = body;
    char line[FOMA_STR_MAX];
    int line_idx = 0;

    while (*p && scr.item_count < FOMA_MAX_ITEMS) {
        if (*p == '\n') {
            line[line_idx] = '\0';
            if (line_idx > 0) {
                strncpy(scr.items[scr.item_count].title, line, sizeof(scr.items[scr.item_count].title) - 1);
                if (strstr(line, "[仮身]") != NULL) {
                    scr.items[scr.item_count].type = FOMA_ITEM_VIRTUAL_BODY;
                    strncpy(scr.items[scr.item_count].badge, "仮身", sizeof(scr.items[scr.item_count].badge) - 1);
                } else {
                    scr.items[scr.item_count].type = FOMA_ITEM_NORMAL;
                }
                scr.item_count++;
            }
            line_idx = 0;
            p++;
        } else {
            if (line_idx < FOMA_STR_MAX - 1) {
                line[line_idx++] = *p;
            }
            p++;
        }
    }
    if (line_idx > 0 && scr.item_count < FOMA_MAX_ITEMS) {
        line[line_idx] = '\0';
        strncpy(scr.items[scr.item_count].title, line, sizeof(scr.items[scr.item_count].title) - 1);
        if (strstr(line, "[仮身]") != NULL) {
            scr.items[scr.item_count].type = FOMA_ITEM_VIRTUAL_BODY;
            strncpy(scr.items[scr.item_count].badge, "仮身", sizeof(scr.items[scr.item_count].badge) - 1);
        } else {
            scr.items[scr.item_count].type = FOMA_ITEM_NORMAL;
        }
        scr.item_count++;
    }

    scr.focus_index = 0;
    foma_push_screen(&scr);
}

/* ── App Info Stub Screen ── */
static void show_app_info_screen(const char *name, const char *desc, const char *status) {
    show_memo_detail_screen(name, status, desc);
}

/* ── Device Info Screen (端末情報) ── */
static void show_device_info_screen(void) {
    FOMA_SCREEN scr;
    memset(&scr, 0, sizeof(scr));
    scr.screen_id = SCR_DEVICE_INFO;
    strncpy(scr.title, "端末情報 / Device Info", sizeof(scr.title) - 1);
    strncpy(scr.subtitle, "AArch32", sizeof(scr.subtitle) - 1);

    strncpy(scr.softkey_left, "[更新]", sizeof(scr.softkey_left) - 1);
    strncpy(scr.softkey_center, "[詳細]", sizeof(scr.softkey_center) - 1);
    strncpy(scr.softkey_right, "[戻る]", sizeof(scr.softkey_right) - 1);

    scr.item_count = 10;
    scr.focus_index = 0;

    const char *labels[] = {
        "端末名: NTT DoCoMo FOMA SH903i",
        "SoC: TI OMAP2430 (ARM1136 Core)",
        "アーキテクチャ: AArch32 (ARMv6)",
        "リアルタイムOS: Sakamura T-Kernel 2.0",
        "サブシステム数: 14 T-Kernel Subsystems",
        "ハードウェアランナー: VirtIO MMIO (0x10001000)",
        "画面解像度: 480x640 TFT LCD 32-bpp (VGA)",
        "日本語入力: TIP / Cleanroom Mozc",
        "文字体系: TRON Code 多国語体系 (JIS 1〜4)",
        "データ基盤: HFDS 実身・仮身ハイパーメディア"
    };
    const char *badges[] = { "FOMA", "OMAP", "ARMv6", "T-Kernel", "14/14", "MMIO", "VGA", "TIP", "TRON", "HFDS" };

    for (int i = 0; i < scr.item_count; i++) {
        strncpy(scr.items[i].title, labels[i], sizeof(scr.items[i].title) - 1);
        strncpy(scr.items[i].badge, badges[i], sizeof(scr.items[i].badge) - 1);
        scr.items[i].type = FOMA_ITEM_NORMAL;
    }

    foma_push_screen(&scr);
}

/* ── Settings Sub-panels ── */
static void show_settings_sub_screen(int setting_id) {
    FOMA_SCREEN scr;
    memset(&scr, 0, sizeof(scr));
    scr.screen_id = SCR_SETTINGS_SUB;

    strncpy(scr.softkey_left, "[変更]", sizeof(scr.softkey_left) - 1);
    strncpy(scr.softkey_center, "[初期値]", sizeof(scr.softkey_center) - 1);
    strncpy(scr.softkey_right, "[戻る]", sizeof(scr.softkey_right) - 1);

    switch (setting_id) {
        case 0: /* 表示設定 */
            strncpy(scr.title, "設定: 表示設定 (Display)", sizeof(scr.title) - 1);
            strncpy(scr.subtitle, "4 items", sizeof(scr.subtitle) - 1);
            scr.item_count = 4;
            strncpy(scr.items[0].title, "画面の明るさ (Brightness)", sizeof(scr.items[0].title) - 1);
            strncpy(scr.items[0].badge, "レベル 4", sizeof(scr.items[0].badge) - 1);
            strncpy(scr.items[1].title, "バックライト点灯時間", sizeof(scr.items[1].title) - 1);
            strncpy(scr.items[1].badge, "30 秒", sizeof(scr.items[1].badge) - 1);
            strncpy(scr.items[2].title, "文字サイズ (Cho-Kanji Font)", sizeof(scr.items[2].title) - 1);
            strncpy(scr.items[2].badge, "16x16 標準", sizeof(scr.items[2].badge) - 1);
            strncpy(scr.items[3].title, "デスクトップ配色テーマ", sizeof(scr.items[3].title) - 1);
            strncpy(scr.items[3].badge, "超漢字ネイビー", sizeof(scr.items[3].badge) - 1);
            break;
        case 1: /* 入力設定 */
            strncpy(scr.title, "設定: 入力設定 (TIP / Mozc)", sizeof(scr.title) - 1);
            strncpy(scr.subtitle, "4 items", sizeof(scr.subtitle) - 1);
            scr.item_count = 4;
            strncpy(scr.items[0].title, "入力方式 (5段トグル入力)", sizeof(scr.items[0].title) - 1);
            strncpy(scr.items[0].badge, "標準ケータイ", sizeof(scr.items[0].badge) - 1);
            strncpy(scr.items[1].title, "ポケベル入力 (2タッチ)", sizeof(scr.items[1].title) - 1);
            strncpy(scr.items[1].badge, "無効", sizeof(scr.items[1].badge) - 1);
            strncpy(scr.items[2].title, "統計連文節変換 (Mozc)", sizeof(scr.items[2].title) - 1);
            strncpy(scr.items[2].badge, "有効", sizeof(scr.items[2].badge) - 1);
            strncpy(scr.items[3].title, "自動確定待ち時間", sizeof(scr.items[3].title) - 1);
            strncpy(scr.items[3].badge, "0.6 秒", sizeof(scr.items[3].badge) - 1);
            break;
        case 4: /* ネットワーク */
            strncpy(scr.title, "設定: ネットワーク (3G FOMA)", sizeof(scr.title) - 1);
            strncpy(scr.subtitle, "4 items", sizeof(scr.subtitle) - 1);
            scr.item_count = 4;
            strncpy(scr.items[0].title, "3G 通信状態 (W-CDMA 2100MHz)", sizeof(scr.items[0].title) - 1);
            strncpy(scr.items[0].badge, "接続中", sizeof(scr.items[0].badge) - 1);
            strncpy(scr.items[1].title, "FOMA ハイスピード (HSDPA)", sizeof(scr.items[1].title) - 1);
            strncpy(scr.items[1].badge, "3.6 Mbps", sizeof(scr.items[1].badge) - 1);
            strncpy(scr.items[2].title, "VirtIO ネットワークアダプタ", sizeof(scr.items[2].title) - 1);
            strncpy(scr.items[2].badge, "virtio-net", sizeof(scr.items[2].badge) - 1);
            strncpy(scr.items[3].title, "i-mode パケット通信", sizeof(scr.items[3].title) - 1);
            strncpy(scr.items[3].badge, "定額制契約", sizeof(scr.items[3].badge) - 1);
            break;
        default:
            show_device_info_screen();
            return;
    }

    scr.focus_index = 0;
    foma_push_screen(&scr);
}

/* ── Calculator Screen ── */
static double s_calc_acc = 0.0;
static char s_calc_buf[32] = "0";

static void show_calculator_screen(void) {
    FOMA_SCREEN scr;
    memset(&scr, 0, sizeof(scr));
    scr.screen_id = SCR_CALCULATOR;
    strncpy(scr.title, "計算機 / Calculator", sizeof(scr.title) - 1);
    snprintf(scr.subtitle, sizeof(scr.subtitle), "%s", s_calc_buf);

    strncpy(scr.softkey_left, "[計算]", sizeof(scr.softkey_left) - 1);
    strncpy(scr.softkey_center, "[クリア]", sizeof(scr.softkey_center) - 1);
    strncpy(scr.softkey_right, "[戻る]", sizeof(scr.softkey_right) - 1);

    scr.item_count = 5;
    scr.focus_index = 0;

    strncpy(scr.items[0].title, "現在の表示値", sizeof(scr.items[0].title) - 1);
    strncpy(scr.items[0].badge, s_calc_buf, sizeof(scr.items[0].badge) - 1);

    strncpy(scr.items[1].title, "[+] 加算", sizeof(scr.items[1].title) - 1);
    strncpy(scr.items[2].title, "[-] 減算", sizeof(scr.items[2].title) - 1);
    strncpy(scr.items[3].title, "[*] 乗算", sizeof(scr.items[3].title) - 1);
    strncpy(scr.items[4].title, "[/] 除算", sizeof(scr.items[4].title) - 1);

    foma_push_screen(&scr);
}

/* ── Terminal Console Screen ── */
static void show_terminal_screen(void) {
    show_memo_detail_screen("gterm — 端末シェル", "tty0 @ 115200 8N1",
        "B-System 3.20 (sakamura-tkernel-virtio) Ken Sakamura\n"
        "[BOOT] Machine: NTT DoCoMo FOMA SH903i (ARM1136 AArch32)\n"
        "[CORE] Sakamura T-Kernel 2.0 Engine Target 10: BTRON_FOMA\n"
        "[VIRTIO] MMIO block device registered @ 0x10001000\n"
        "[FOMA] Vertical screen 480x640 initialized\n"
        "$ uname -a\n"
        "BTRON3 3.20 (Cleanroom Sakamura T-Kernel 2.0)\n"
        "$ tk_get_tid()\n"
        "Current Task ID: 1 (workbench_mobile)\n"
        "$ _\n");
}

/* ── Master Mobile Workbench Initializer ── */
void foma_workbench_init(void) {
    foma_ui_init();
    show_home_cabinet_screen();
}

/* ── Keypad & Touch Event Dispatcher ── */
BOOL foma_workbench_process_event(const EVT *ev) {
    if (!ev) return FALSE;

    FOMA_SCREEN *cur = foma_get_active_screen();
    if (!cur) return FALSE;

    /* Handle active modal dialog first */
    if (foma_is_modal_active()) {
        FOMA_MODAL *mod = foma_get_active_modal();
        if (ev->type == EV_KEY_DOWN) {
            if (ev->key == SDLK_LEFT || ev->key == SDLK_RIGHT) {
                mod->selected_btn = 1 - mod->selected_btn;
                return TRUE;
            } else if (ev->key == SDLK_RETURN || ev->key == SDLK_SPACE || ev->key == SDLK_F1) {
                if (mod->selected_btn == 0 && mod->on_confirm) {
                    mod->on_confirm();
                } else if (mod->selected_btn == 1 && mod->on_cancel) {
                    mod->on_cancel();
                }
                foma_close_modal();
                return TRUE;
            } else if (ev->key == SDLK_ESCAPE || ev->key == SDLK_BACKSPACE || ev->key == SDLK_F3) {
                if (mod->on_cancel) mod->on_cancel();
                foma_close_modal();
                return TRUE;
            }
        } else if (ev->type == EV_BUT_DOWN) {
            /* Any click dismisses or confirms modal */
            foma_close_modal();
            return TRUE;
        }
        return TRUE;
    }

    /* Process Screen Event */
    if (ev->type == EV_KEY_DOWN) {
        /* 1. 5-Way Directional Pad */
        if (ev->key == SDLK_UP) {
            foma_nav_move_focus(cur, -1);
            return TRUE;
        } else if (ev->key == SDLK_DOWN) {
            foma_nav_move_focus(cur, 1);
            return TRUE;
        } else if (ev->key == SDLK_RETURN || ev->key == SDLK_SPACE) {
            /* Center / OK / Select key */
            foma_nav_activate_selected(cur);
            return TRUE;
        }

        /* 2. Soft Keys */
        /* Left Soft Key: F1, '[', 'q', '1' */
        if (ev->key == SDLK_F1 || ev->key == '[' || ev->key == 'q' || ev->key == 'Q') {
            foma_trigger_softkey_left(cur);
            return TRUE;
        }
        /* Center Soft Key / Menu: F2, 'm', 'w', '2' */
        if (ev->key == SDLK_F2 || ev->key == 'm' || ev->key == 'M' || ev->key == 'w') {
            if (cur->screen_id == SCR_HOME) {
                show_system_menu_screen();
            } else {
                foma_trigger_softkey_center(cur);
            }
            return TRUE;
        }
        /* Right Soft Key / Back / Clear: F3, ']', 'e', Esc, Backspace */
        if (ev->key == SDLK_F3 || ev->key == ']' || ev->key == 'e' || ev->key == 'E' ||
            ev->key == SDLK_ESCAPE || ev->key == SDLK_BACKSPACE) {
            foma_trigger_softkey_right(cur);
            return TRUE;
        }

        /* 3. Numeric Keypad Accelerators: '1'..'9' */
        if (ev->key >= '1' && ev->key <= '9') {
            int num_idx = ev->key - '1';
            if (num_idx < cur->item_count &&
                cur->items[num_idx].type != FOMA_ITEM_SEPARATOR &&
                cur->items[num_idx].type != FOMA_ITEM_HEADER) {
                foma_nav_set_focus(cur, num_idx);
                foma_nav_activate_selected(cur);
                return TRUE;
            }
        }
    } else if (ev->type == EV_BUT_DOWN) {
        /* Mouse / Touch Tap Handling */
        H mx = ev->pos.x;
        H my = ev->pos.y;

        /* A. Bottom Soft Key Bar Tap */
        if (my >= FOMA_SCREEN_H - FOMA_SOFTKEY_BAR_H) {
            H btn_w = FOMA_SCREEN_W / 3;
            if (mx < btn_w) {
                foma_trigger_softkey_left(cur);
            } else if (mx < btn_w * 2) {
                if (cur->screen_id == SCR_HOME) {
                    show_system_menu_screen();
                } else {
                    foma_trigger_softkey_center(cur);
                }
            } else {
                foma_trigger_softkey_right(cur);
            }
            return TRUE;
        }

        /* B. List Row Tap */
        if (my >= FOMA_LIST_TOP && my < FOMA_LIST_BOTTOM) {
            int row_rel = (my - FOMA_LIST_TOP) / FOMA_ROW_H;
            int item_idx = cur->top_index + row_rel;
            if (item_idx >= 0 && item_idx < cur->item_count) {
                if (cur->items[item_idx].type != FOMA_ITEM_SEPARATOR &&
                    cur->items[item_idx].type != FOMA_ITEM_HEADER) {
                    foma_nav_set_focus(cur, item_idx);
                    foma_nav_activate_selected(cur);
                    return TRUE;
                }
            }
        }
    }

    return FALSE;
}

/* ── Public Screen Openers (for testing, scripting, and screenshot capturing) ── */
void foma_show_home_cabinet(void) {
    show_home_cabinet_screen();
}

void foma_show_contacts(void) {
    show_contacts_screen();
}

void foma_show_memos(void) {
    show_memos_screen();
}

void foma_show_apps_launcher(void) {
    show_apps_launcher_screen();
}

void foma_show_control_panel(void) {
    show_control_panel_screen();
}

void foma_show_system_menu(void) {
    show_system_menu_screen();
}

void foma_show_device_info(void) {
    show_device_info_screen();
}

void foma_show_contact_detail_sample(void) {
    show_contact_detail_screen("坂村 健 (Ken Sakamura)", "090-XXXX-XXXX",
                               "YRPユビキタス研究所", "TRONプロジェクト創立者・リーダー");
}

void foma_show_editor_sample(void) {
    show_memo_detail_screen("T-Editor — 基本エディタ", "2026-09-06",
        "1: /* Sakamura B-TRON T-Editor */\n"
        "2: #include <tk/tkernel.h>\n"
        "3: int main(void) {\n"
        "4:   btron_core_init();\n"
        "5:   [仮身] 実身キャビネット\n"
        "6:   [仮身] TAD仕様解説書\n"
        "7:   return 0;\n"
        "8: }\n"
        "[TAD SPEC Rev 3.20 Ready]");
}

void foma_show_calculator(void) {
    show_calculator_screen();
}

void foma_show_terminal(void) {
    show_terminal_screen();
}
