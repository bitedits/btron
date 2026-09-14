#!/usr/bin/env python3
"""
scripts/populate_doc_screens.py
Automated injection of native C99 transparent screenshot previews into B-System HTML documentation pages.
- Apps: Opened menu & live content (Cabinet, Editor, Browser, Terminal),
        Control Panel screenshot for Preferences,
        2 screenshots for Workbench (Full Desktop after load + About dialog).
- Plain image tags without CSS effects.
"""

import os
import re

SETTINGS_DIR = "b-system/settings"
APPS_DIR = "b-system/apps"

SETTINGS_MAP = {
    "Appearance.html": [
        {
            "screen": "../img/screens/Appearance_Settings.png",
            "caption": "実機描画フレームバッファより自動抽出された透過外観設定ウィンドウ (Native Headless GDEV Capture)",
            "alt": "外観設定 (Appearance Settings)"
        }
    ],
    "Desktop.html": [
        {
            "screen": "../img/screens/Desktop_Settings.png",
            "caption": "実機描画フレームバッファより自動抽出された透過デスクトップ設定ウィンドウ (Native Headless GDEV Capture)",
            "alt": "デスクトップ設定 (Desktop Settings)"
        }
    ],
    "Display.html": [
        {
            "screen": "../img/screens/Display_Settings.png",
            "caption": "実機描画フレームバッファより自動抽出された透過画面表示設定ウィンドウ (Native Headless GDEV Capture)",
            "alt": "画面表示設定 (Display Settings)"
        }
    ],
    "Input.html": [
        {
            "screen": "../img/screens/Input_Settings.png",
            "caption": "実機描画フレームバッファより自動抽出された透過入力環境設定ウィンドウ (Native Headless GDEV Capture)",
            "alt": "入力環境設定 (Input Settings)"
        }
    ],
    "Language.html": [
        {
            "screen": "../img/screens/Language_Settings.png",
            "caption": "実機描画フレームバッファより自動抽出された透過言語・文字設定ウィンドウ (Native Headless GDEV Capture)",
            "alt": "言語設定 (Language Settings)"
        }
    ],
    "Media.html": [
        {
            "screen": "../img/screens/Media_Settings.png",
            "caption": "実機描画フレームバッファより自動抽出された透過メディア設定ウィンドウ (Native Headless GDEV Capture)",
            "alt": "メディア設定 (Media Settings)"
        }
    ],
    "Network.html": [
        {
            "screen": "../img/screens/Network_Settings.png",
            "caption": "実機描画フレームバッファより自動抽出された透過通信網設定ウィンドウ (Native Headless GDEV Capture)",
            "alt": "通信網設定 (Network Settings)"
        }
    ],
    "Security.html": [
        {
            "screen": "../img/screens/Security_Settings.png",
            "caption": "実機描画フレームバッファより自動抽出された透過保全・権限設定ウィンドウ (Native Headless GDEV Capture)",
            "alt": "保全設定 (Security Settings)"
        }
    ],
    "Sound.html": [
        {
            "screen": "../img/screens/Sound_Settings.png",
            "caption": "実機描画フレームバッファより自動抽出された透過音響・音声設定ウィンドウ (Native Headless GDEV Capture)",
            "alt": "音響設定 (Sound Settings)"
        }
    ],
    "System.html": [
        {
            "screen": "../img/screens/System_Settings.png",
            "caption": "実機描画フレームバッファより自動抽出された透過基本情報設定ウィンドウ (Native Headless GDEV Capture)",
            "alt": "基本情報設定 (System Settings)"
        }
    ],
    "Terminal.html": [
        {
            "screen": "../img/screens/Terminal_Settings.png",
            "caption": "実機描画フレームバッファより自動抽出された透過端末環境設定ウィンドウ (Native Headless GDEV Capture)",
            "alt": "端末設定 (Terminal Settings)"
        }
    ]
}

# Apps mapping
APPS_MAP = {
    "Cabinet.html": [
        {
            "screen": "../img/screens/Cabinet_Menu_Opened.png",
            "caption": "実機描画フレームバッファより自動抽出された実身キャビネット・エクスプローラ (ファイルメニュー展開・実身一覧表示)",
            "alt": "実身キャビネット (Cabinet Application - Menu Opened)"
        },
        {
            "screen": "../img/screens/Cabinet_About.png",
            "caption": "実機描画フレームバッファより自動抽出された透過バージョン情報ダイアログ (About Cabinet)",
            "alt": "実身キャビネット バージョン情報 (About Cabinet)"
        }
    ],
    "Editor.html": [
        {
            "screen": "../img/screens/Editor_Menu_Opened.png",
            "caption": "実機描画フレームバッファより自動抽出された基本文章編集 T-Editor (ファイルメニュー展開・本文テキスト表示)",
            "alt": "基本文章編集 (Editor Application - Menu Opened)"
        },
        {
            "screen": "../img/screens/Editor_About.png",
            "caption": "実機描画フレームバッファより自動抽出された透過バージョン情報ダイアログ (About Editor)",
            "alt": "文書編集 バージョン情報 (About Editor)"
        }
    ],
    "Browser.html": [
        {
            "screen": "../img/screens/Browser_Menu_Opened.png",
            "caption": "実機描画フレームバッファより自動抽出された仕様書閲覧 TAD Browser (ファイルメニュー展開・仕様書TAD文書表示)",
            "alt": "仕様書閲覧 (Browser Application - Menu Opened)"
        },
        {
            "screen": "../img/screens/Browser_About.png",
            "caption": "実機描画フレームバッファより自動抽出された透過バージョン情報ダイアログ (About TAD Browser)",
            "alt": "TADブラウザ バージョン情報 (About TAD Browser)"
        }
    ],
    "Terminal.html": [
        {
            "screen": "../img/screens/Terminal_Menu_Opened.png",
            "caption": "実機描画フレームバッファより自動抽出されたBTRON端末エミュレータ GTerm (ファイルメニュー展開・シェル対話表示)",
            "alt": "端末 (Terminal Application - Menu Opened)"
        },
        {
            "screen": "../img/screens/Terminal_About.png",
            "caption": "実機描画フレームバッファより自動抽出された透過バージョン情報ダイアログ (About gterm)",
            "alt": "端末 バージョン情報 (About gterm)"
        }
    ],
    "Cassette.html": [
        {
            "screen": "../img/screens/Cassette_Application.png",
            "caption": "実機描画フレームバッファより自動抽出されたステレオカセットデッキ Cassette (Native Headless GDEV Capture)",
            "alt": "カセット (Cassette Application)"
        },
        {
            "screen": "../img/screens/Cassette_About.png",
            "caption": "実機描画フレームバッファより自動抽出された透過バージョン情報ダイアログ (About Cassette)",
            "alt": "カセット バージョン情報 (About Cassette)"
        }
    ],
    "Orchestra.html": [
        {
            "screen": "../img/screens/Orchestra_Application.png",
            "caption": "実機描画フレームバッファより自動抽出された管弦楽・MIDIオーケストラ演奏画面 (Live Stage MIDI Server & Workstation)",
            "alt": "管弦楽 (Orchestra Application)"
        },
        {
            "screen": "../img/screens/Orchestra_About.png",
            "caption": "実機描画フレームバッファより自動抽出された透過バージョン情報ダイアログ (About Orchestra)",
            "alt": "管弦楽 バージョン情報 (About Orchestra)"
        }
    ],
    "Preferences.html": [
        {
            "screen": "../img/screens/Preferences_Settings.png",
            "caption": "実機描画フレームバッファより自動抽出された環境設定コントロールパネルハブ (Control Panel Hub)",
            "alt": "環境設定コントロールパネル (Preferences Control Panel)"
        }
    ],
    "Volumes.html": [
        {
            "screen": "../img/screens/DriveSetup_Menu_Opened.png",
            "caption": "実機描画フレームバッファより自動抽出されたボリューム管理 Volumes (ディスクメニュー展開・区画操作一覧)",
            "alt": "ボリューム管理 (Volumes / DriveSetup Application - Menu Opened)"
        },
        {
            "screen": "../img/screens/DriveSetup_About.png",
            "caption": "実機描画フレームバッファより自動抽出された透過バージョン情報ダイアログ (About Volumes)",
            "alt": "ボリューム管理 バージョン情報 (About Volumes / DriveSetup)"
        }
    ],
    "Workbench.html": [
        {
            "screen": "../img/screens/Desktop_Full.png",
            "caption": "実機描画フレームバッファより抽出されたBTRONワークベンチ空間デスクトップ (［BTRON］メインメニュー展開・初期起動画面)",
            "alt": "ワークベンチ空間デスクトップ (［BTRON］メインメニュー展開)"
        },
        {
            "screen": "../img/screens/About_Application.png",
            "caption": "実機描画フレームバッファより自動抽出された透過システム情報ダイアログ (About B-System Workstation)",
            "alt": "システム情報 (About B-System Workstation)"
        }
    ]
}

def remove_screen_section_if_not_mapped(dir_path, mapped_dict):
    for f in os.listdir(dir_path):
        if f.endswith(".html") and f not in mapped_dict:
            filepath = os.path.join(dir_path, f)
            with open(filepath, "r", encoding="utf-8") as fp:
                content = fp.read()
            if "<!-- 画面プレビュー -->" in content or "画面プレビュー (Screen Preview)" in content:
                new_content = re.sub(r'<!-- 画面プレビュー -->\s*<div class="section">\s*<h2>画面プレビュー \(Screen Preview\)</h2>.*?</div>\s*</div>\s*', '', content, flags=re.DOTALL)
                if new_content != content:
                    with open(filepath, "w", encoding="utf-8") as fp:
                        fp.write(new_content)
                    print(f"Removed screen preview section from unmapped file: {filepath}")

def inject_screen_section(filepath, items):
    if not os.path.exists(filepath):
        return

    with open(filepath, "r", encoding="utf-8") as f:
        content = f.read()

    # Remove existing screen preview section if present
    content = re.sub(r'<!-- 画面プレビュー -->\s*<div class="section">\s*<h2>画面プレビュー \(Screen Preview\)</h2>.*?</div>\s*</div>\s*', '', content, flags=re.DOTALL)

    previews_html = []
    for item in items:
        previews_html.append(f"""      <div class="screen-preview">
        <img src="{item['screen']}" alt="{item['alt']}">
        <p>{item['caption']}</p>
      </div>""")

    previews_body = "\n".join(previews_html)

    img_block = f"""    <!-- 画面プレビュー -->
    <div class="section">
      <h2>画面プレビュー (Screen Preview)</h2>
{previews_body}
    </div>\n\n"""

    # Inject right before <!-- 概要 --> or <h2>概要
    if "<!-- 概要 -->" in content:
        content = content.replace("<!-- 概要 -->", img_block + "    <!-- 概要 -->", 1)
    elif "<h2>概要" in content:
        idx = content.find("<h2>概要")
        sec_idx = content.rfind('<div class="section">', 0, idx)
        if sec_idx != -1:
            content = content[:sec_idx] + img_block + content[sec_idx:]
        else:
            content = content.replace("<h2>概要", img_block + "<h2>概要", 1)
    else:
        idx = content.find('class="tagline-card"')
        if idx != -1:
            end_card = content.find('</div>', idx) + 6
            content = content[:end_card] + "\n\n" + img_block + content[end_card:]

    with open(filepath, "w", encoding="utf-8") as f:
        f.write(content)
    print(f"Populated screenshot previews ({len(items)}) in: {filepath}")

def main():
    print("==========================================================")
    print(" Populating Screenshots for Settings & Selected Apps...")
    print("==========================================================")
    for filename, items in SETTINGS_MAP.items():
        inject_screen_section(os.path.join(SETTINGS_DIR, filename), items)

    for filename, items in APPS_MAP.items():
        inject_screen_section(os.path.join(APPS_DIR, filename), items)

    remove_screen_section_if_not_mapped(APPS_DIR, APPS_MAP)
    remove_screen_section_if_not_mapped(SETTINGS_DIR, SETTINGS_MAP)

    print("==========================================================")
    print(" Finished Populating Documentation Screenshots!")
    print("==========================================================")

if __name__ == "__main__":
    main()
