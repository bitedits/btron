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
    "Commander.html": [
        {
            "screen": "../img/screens/Commander_SC.png",
            "caption": "実機描画フレームバッファより自動抽出された Sokhatsky Commander (sc) 終了確認ダイアログ (F10 Quit Dialog)",
            "alt": "Sokhatsky Commander (sc) 終了確認ダイアログ (Quit Dialog)"
        },
        {
            "screen": "../img/screens/Commander_TV.png",
            "caption": "実機描画フレームバッファより自動抽出された Terminal Vision (tv) 検索ダイアログ (F2 Find Dialog)",
            "alt": "Terminal Vision (tv) 検索ダイアログ (Search Dialog)"
        },
        {
            "screen": "../img/screens/Commander_BE.png",
            "caption": "実機描画フレームバッファより自動抽出された Binary Editor (be) アーキテクチャ選択ダイアログ (F2 Architecture Dialog)",
            "alt": "Binary Editor (be) アーキテクチャ選択ダイアログ (Architecture Dialog)"
        },
        {
            "screen": "../img/screens/Commander_BE_X64.png",
            "caption": "実機描画フレームバッファより自動抽出された Binary Editor (be) 逆アセンブル表示 (EM64T Intel/AMD x86-64)",
            "alt": "Binary Editor (be) EM64T 逆アセンブル (Disassembly)"
        },
        {
            "screen": "../img/screens/Commander_BE_ARM.png",
            "caption": "実機描画フレームバッファより自動抽出された Binary Editor (be) 逆アセンブル表示 (AArch32 / ARM 32-bit)",
            "alt": "Binary Editor (be) AArch32 逆アセンブル (Disassembly)"
        },
        {
            "screen": "../img/screens/Commander_BE_RISCV.png",
            "caption": "実機描画フレームバッファより自動抽出された Binary Editor (be) 逆アセンブル表示 (RISC-V)",
            "alt": "Binary Editor (be) RISC-V 逆アセンブル (Disassembly)"
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
            "caption": "実機描画フレームバッファより自動抽出された管弦楽・MIDIオーケストラ演奏画面。アルバム第一曲『Carol of the Bells』を読み込んだ状態のステージ窓 (780×520)。",
            "alt": "管弦楽 ステージ (Orchestra Stage)"
        },
        {
            "screen": "../img/screens/Orchestra_Arrange.png",
            "caption": "実機描画フレームバッファより自動抽出されたアレンジ窓 (760×420)。17トラックのイベント線が小節格子に並ぶ。",
            "alt": "管弦楽 アレンジ (Orchestra Arrange)"
        },
        {
            "screen": "../img/screens/Orchestra_Piano.png",
            "caption": "実機描画フレームバッファより自動抽出されたピアノ窓 (760×480)。鍵盤・グリッド・ツール行・Undo ボタン。",
            "alt": "管弦楽 ピアノロール (Orchestra Piano Roll)"
        },
        {
            "screen": "../img/screens/Orchestra_Score.png",
            "caption": "実機描画フレームバッファより自動抽出されたスコア窓 (720×320)。選択トラックの一段ノートを高位記号つきで描く。",
            "alt": "管弦楽 スコア (Orchestra Score)"
        },
        {
            "screen": "../img/screens/Orchestra_Mix.png",
            "caption": "実機描画フレームバッファより自動抽出されたミキサー窓 (780×460)。トラックストリップとマスター列。",
            "alt": "管弦楽 ミキサー (Orchestra Mixer)"
        },
        {
            "screen": "../img/screens/Orchestra_Insert.png",
            "caption": "実機描画フレームバッファより自動抽出されたインサート窓 (420×460)。二つのスロットと P1〜P4 のノブ。",
            "alt": "管弦楽 インサート (Orchestra Insert)"
        },
        {
            "screen": "../img/screens/Orchestra_About.png",
            "caption": "実機描画フレームバッファより自動抽出された透過バージョン情報窓 (520×270)。リサイズ不可の唯一の窓。",
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
    ],
    "Clarity.html": [
        {
            "screen": "../img/screens/Clarity_Application.png",
            "caption": "実機描画フレームバッファより自動抽出されたClarity電子帳票DTPウィンドウ (水平・垂直スクロールバー・頁区切り・判型組版表示)",
            "alt": "電子帳票 Clarity DTP Application (Native Headless GDEV Capture)"
        },
        {
            "screen": "../img/screens/Clarity_Menu_Opened.png",
            "caption": "実機描画フレームバッファより自動抽出されたClarity表示メニュー展開画面 (View/Zoom Menu Opened: 拡大・縮小・等倍・全体表示)",
            "alt": "電子帳票 Clarity – 表示/拡大縮小メニュー展開 (View Menu Opened)"
        }
    ],
    "Paint.html": [
        {
            "screen": "../img/screens/Paint_Application.png",
            "caption": "実機描画フレームバッファより自動抽出されたペイント画像ビューア (GIF/PNG復元表示・水平/垂直スクロールバー・市松キャンバス)",
            "alt": "ペイント 画像ビューア (Paint Application - Native Headless GDEV Capture)"
        },
        {
            "screen": "../img/screens/Paint_Menu_Opened.png",
            "caption": "実機描画フレームバッファより自動抽出されたペイント表示メニュー展開画面 (ファイル/表示メニュー: 拡大・縮小・原寸大・ウィンドウに合わせる)",
            "alt": "ペイント – メニュー展開 (Paint File/View Menu Opened)"
        },
        {
            "screen": "../img/screens/Paint_About.png",
            "caption": "実機描画フレームバッファより自動抽出された透過バージョン情報ダイアログ (About Paint)",
            "alt": "ペイント バージョン情報 (About Paint)"
        }
    ],
    "MSX.html": [
        {
            "screen": "../img/screens/MSX_Bullrush.png",
            "caption": "実機描画フレームバッファより自動抽出された BULLRUSH (MSX2/turboR C-BIOS 高速描画・MegaROM カートリッジ)",
            "alt": "BULLRUSH (MSX MegaROM Preview)"
        },
        {
            "screen": "../img/screens/MSX_NeonRevenant.png",
            "caption": "実機描画フレームバッファより自動抽出された NEON REVENANT (Geo3D 256KB 3次元ワイヤーフレーム/ポリゴン演算エンジン)",
            "alt": "NEON REVENANT (MSX Geo3D Engine Preview)"
        },
        {
            "screen": "../img/screens/MSX_NightRaven.png",
            "caption": "実機描画フレームバッファより自動抽出された NIGHT RAVEN GEO3D (V9938 ハードウェアパレット・リアルタイム3Dフライト)",
            "alt": "NIGHT RAVEN GEO3D (MSX Geo3D Hardware Pipeline Preview)"
        },
        {
            "screen": "../img/screens/MSX_XRacing.png",
            "caption": "B-MSX の窓から抽出した XRacing。MSX1 / TMS9918A / PSG。チェッカーの上にロゴ。",
            "alt": "XRacing (MSX1, checkerboard logo)"
        },
        {
            "screen": "../img/screens/MSX_BadApple.png",
            "caption": "B-MSX の窓から抽出した Bad Apple (MDPC)。MSX2+ / V9958。PSG と YM2413。りんごを持つシルエット。",
            "alt": "Bad Apple MSX2+ silhouette"
        },
        {
            "screen": "../img/screens/MSX_DynamiteGoGo.png",
            "caption": "B-MSX の窓から抽出した Dynamite Go! Go!。MSX2 JP / V9938 / PSG。タイトルの DYNAMITE GO! GO!。",
            "alt": "Dynamite Go! Go! title"
        },
        {
            "screen": "../img/screens/MSX_Yazzier.png",
            "caption": "B-MSX の窓から抽出した YAZZIER。MSX1 / TMS9918A / PSG。虹とレンガのタイトル。",
            "alt": "YAZZIER title card"
        },
        {
            "screen": "../img/screens/MSX_C0man.png",
            "caption": "B-MSX の窓から抽出した c0man。MSX1 / TMS9918A / PSG。ハイスコアと PRESS SPACE TO START。",
            "alt": "c0man high scores"
        }
    ],
    "XMB.html": [
        {
            "screen": "../img/screens/XMB_Application.png",
            "caption": "実機描画フレームバッファより自動抽出された Cross Media Bar (XMB) メディアランチャー画面 (Dynamic Wave & Real-Time Category Navigation)",
            "alt": "Cross Media Bar (XMB) 画面"
        }
    ],
    "SDK.html": [
        {
            "screen": "../img/screens/Demoscene_Application.png",
            "caption": "実機描画フレームバッファより自動抽出された Lil Cu 64 ネイティブ 3D デモシーン画面 (4D Hopf Fibration & Procedural DSP Synth)",
            "alt": "Lil Cu 64 Demoscene 画面"
        }
    ]
}

SCREEN_MARKER = "<!-- 画面プレビュー -->"
OVERVIEW_ANCHOR = "<!-- 概要 -->"
KAGEE_NOTE = (
    "Kagee の Bad Apple はカートリッジではない。"
    "<code>capture_screens --msx</code> は <code>assets/kagee/BADAPPLE.vid</code> と "
    "<code>BADAPPLE.aud</code> があるとき、その窓も同じ手順で書く。"
    "このチェックアウトにはその 2 ファイルが無い。"
)

def screen_items(items):
    """Copy the list and attach the Kagee row when this is the MSX gallery."""
    out = list(items)
    if not any(item.get("screen") == "../img/screens/MSX_C0man.png" for item in items):
        return out
    png = os.path.normpath(os.path.join(
        os.path.dirname(__file__), "..", "b-system", "img", "screens", "Kagee_BadApple.png"))
    if os.path.isfile(png):
        out.append({
            "screen": "../img/screens/Kagee_BadApple.png",
            "caption": "Kagee 窓から抽出した Bad Apple。カートリッジではない。96×64 Huffman / 4-bit PCM。",
            "alt": "Kagee Bad Apple"
        })
    else:
        out.append({"note": KAGEE_NOTE})
    return out

def strip_screen_section(content):
    """Drop the preview block and stop at the overview anchor.

    A loose paragraph inside the preview (the Kagee note) sits between the
    last screen-preview and the section close, so a search for two closing
    divs continues through the overview and the rest of the page.
    """
    start = content.find(SCREEN_MARKER)
    if start == -1:
        return content
    anchor = content.find(OVERVIEW_ANCHOR, start)
    if anchor != -1:
        anchor_line = content.rfind("\n", start, anchor) + 1
        return content[:start] + content[anchor_line:]
    return re.sub(
        r'<!-- 画面プレビュー -->\s*<div class="section">\s*<h2>画面プレビュー \(Screen Preview\)</h2>.*?</div>\s*</div>\s*',
        '',
        content,
        count=1,
        flags=re.DOTALL)

def insert_block_at(content, pos, block):
    """Put block just before pos without touching what already sits there.

    The whitespace in front of pos becomes the marker's own indent and the anchor
    keeps its line, so nothing accumulates: strip + re-insert is byte-identical and
    the unmapped pages stop drifting by four spaces per run.
    """
    return content[:pos] + block.rstrip("\n") + "\n\n" + content[pos:]

def remove_screen_section_if_not_mapped(dir_path, mapped_dict):
    for f in os.listdir(dir_path):
        if f.endswith(".html") and f not in mapped_dict:
            filepath = os.path.join(dir_path, f)
            with open(filepath, "r", encoding="utf-8") as fp:
                content = fp.read()
            if SCREEN_MARKER in content or "画面プレビュー (Screen Preview)" in content:
                new_content = strip_screen_section(content)
                if new_content != content:
                    with open(filepath, "w", encoding="utf-8") as fp:
                        fp.write(new_content)
                    print(f"Removed screen preview section from unmapped file: {filepath}")

def inject_screen_section(filepath, items):
    if not os.path.exists(filepath):
        return

    items = screen_items(items)

    with open(filepath, "r", encoding="utf-8") as f:
        original = f.read()

    previews_html = []
    for item in items:
        if item.get("note"):
            previews_html.append(f"      <p>{item['note']}</p>")
            continue
        previews_html.append(f"""      <div class="screen-preview">
        <img src="{item['screen']}" alt="{item['alt']}">
        <p>{item['caption']}</p>
      </div>""")

    previews_body = "\n".join(previews_html)

    # Leave the indent that already sits in front of the marker.
    img_block = f"""<!-- 画面プレビュー -->
    <div class="section">
      <h2>画面プレビュー (Screen Preview)</h2>
{previews_body}
    </div>

"""

    start = original.find(SCREEN_MARKER)
    anchor = original.find(OVERVIEW_ANCHOR, start) if start != -1 else -1
    if start != -1 and anchor != -1:
        anchor_line = original.rfind("\n", start, anchor) + 1
        content = original[:start] + img_block + original[anchor_line:]
    else:
        content = strip_screen_section(original)
        if OVERVIEW_ANCHOR in content:
            content = insert_block_at(content, content.find(OVERVIEW_ANCHOR), img_block)
        elif "<h2>概要" in content:
            idx = content.find("<h2>概要")
            sec_idx = content.rfind('<div class="section">', 0, idx)
            content = insert_block_at(content, sec_idx if sec_idx != -1 else idx, img_block)
        else:
            idx = content.find('class="tagline-card"')
            if idx != -1:
                end_card = content.find('</div>', idx) + len('</div>')
                content = content[:end_card] + "\n\n" + img_block.rstrip("\n") + "\n\n" + content[end_card:]

    if content != original:
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
