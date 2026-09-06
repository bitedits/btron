#!/usr/bin/env python3
"""
scripts/update_foma_screens.py

Automated conversion of raw µBTRON-FOMA framebuffer dumps into production PNGs
and injection of authentic mobile UI screenshots into mobile.html.

Captured screens:
1) Main (Home Cabinet / 起動キャビネット)
2) Contacts (連絡先キャビネット)
3) Memo (メモ一覧 / Memos)
4) Editor (T-Editor / 基本エディタ)
5) Control Panel (コントロールパネル)
6) Other Apps (アプリ一覧 / Applications)
+ Contact Details & Device Info
"""

import os
import glob
import struct
import zlib
import re

BASE_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
RAW_DIR = "/tmp/foma_raw_screens"
IMG_OUT_DIR = os.path.join(BASE_DIR, "b-system", "img", "foma")
MOBILE_HTML = os.path.join(BASE_DIR, "mobile.html")

def raw_to_png(raw_path, png_path):
    with open(raw_path, "rb") as f:
        data = f.read()

    if len(data) < 8:
        print(f"[WARN] Raw file too small: {raw_path}")
        return False

    width, height = struct.unpack("<II", data[:8])
    raw_pixels = data[8:]

    expected_len = width * height * 4
    if len(raw_pixels) < expected_len:
        print(f"[WARN] Truncated data for {raw_path} (got {len(raw_pixels)}, expected {expected_len})")
        return False

    # Convert 0xAARRGGBB (Little Endian in memory: B, G, R, A) to RGBA lines
    raw_lines = []
    for y in range(height):
        line = bytearray([0]) # PNG filter byte 0 (None)
        row_offset = y * width * 4
        for x in range(width):
            px_offset = row_offset + x * 4
            b = raw_pixels[px_offset]
            g = raw_pixels[px_offset + 1]
            r = raw_pixels[px_offset + 2]
            a = raw_pixels[px_offset + 3]
            line.extend((r, g, b, a))
        raw_lines.append(bytes(line))

    compressed = zlib.compress(b"".join(raw_lines), 9)

    def make_chunk(tag, content):
        crc = zlib.crc32(tag + content) & 0xffffffff
        return struct.pack(">I", len(content)) + tag + content + struct.pack(">I", crc)

    png = [
        b"\x89PNG\r\n\x1a\n",
        make_chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)), # 8-bit RGBA
        make_chunk(b"IDAT", compressed),
        make_chunk(b"IEND", b"")
    ]

    os.makedirs(os.path.dirname(png_path), exist_ok=True)
    with open(png_path, "wb") as f:
        f.write(b"".join(png))

    print(f"  [PNG] {os.path.basename(png_path)} ({width}x{height} px, {os.path.getsize(png_path)} bytes)")
    return True

def convert_all_raws():
    os.makedirs(IMG_OUT_DIR, exist_ok=True)
    raw_files = glob.glob(os.path.join(RAW_DIR, "*.raw"))
    if not raw_files:
        print(f"[ERROR] No raw captures found in {RAW_DIR}")
        return 0

    converted = 0
    for r in sorted(raw_files):
        base = os.path.splitext(os.path.basename(r))[0]
        png_path = os.path.join(IMG_OUT_DIR, f"{base}.png")
        if raw_to_png(r, png_path):
            converted += 1
    return converted

def generate_foma_screens_html():
    return """
        <!-- µBTRON-FOMA Mobile UI Showcase -->
        <section class="audit-card" id="foma-screens">
            <h3><span>µBTRON-FOMA モバイル UI 画面ギャラリー (Mobile UI Showcase)</span></h3>
            <p>
                以下の画面は、実機描画エンジン（C99 Headless Framebuffer Compositor）によりピクセル単位で正確に生成された µBTRON-FOMA の画面群である。480×640 VGA 縦画面（Portrait）、5方向キーによるフォーカス遷移、3ソフトキー操作体系、および実身・仮身（Fusen）ハイパーメディアモデルを忠実に再現している。
            </p>
            <p style="font-size:0.9em; opacity:0.85; margin-top:0.6rem;">
                The screenshots below are natively rendered from the C99 framebuffer compositor for µBTRON-FOMA on a 480×640 VGA vertical viewport. They demonstrate keypad-driven focus navigation, context-sensitive soft keys, and the authentic Real Body / Virtual Body (実身・仮身) hypermedia environment.
            </p>

            <style>
                .foma-gallery-grid {
                    display: grid;
                    grid-template-columns: repeat(auto-fit, minmax(320px, 1fr));
                    gap: 1.8rem;
                    margin-top: 1.8rem;
                }
                .foma-phone-card {
                    background: #f8fafc;
                    border: 1px solid #cbd5e1;
                    border-radius: 16px;
                    padding: 1.2rem;
                    display: flex;
                    flex-direction: column;
                    align-items: center;
                    box-shadow: 0 4px 16px rgba(0, 0, 0, 0.04);
                    transition: transform 0.2s ease, box-shadow 0.2s ease;
                }
                .foma-phone-card:hover {
                    transform: translateY(-3px);
                    box-shadow: 0 8px 24px rgba(0, 87, 183, 0.12);
                    border-color: #0057b7;
                }
                .foma-screen-frame {
                    background: #000;
                    border: 4px solid #1e293b;
                    border-radius: 12px;
                    overflow: hidden;
                    box-shadow: inset 0 0 10px rgba(0,0,0,0.5), 0 4px 12px rgba(0,0,0,0.15);
                    max-width: 100%;
                    display: flex;
                    justify-content: center;
                }
                .foma-screen-img {
                    width: 100%;
                    max-width: 320px;
                    height: auto;
                    display: block;
                    image-rendering: pixelated;
                }
                .foma-card-info {
                    width: 100%;
                    margin-top: 1rem;
                    text-align: left;
                }
                .foma-card-title {
                    font-size: 1.05rem;
                    font-weight: 700;
                    color: #003366;
                    margin-bottom: 0.35rem;
                    display: flex;
                    align-items: center;
                    justify-content: space-between;
                }
                .foma-badge {
                    font-size: 0.75rem;
                    background: #e0f2fe;
                    color: #0284c7;
                    border: 1px solid #bae6fd;
                    padding: 0.15rem 0.5rem;
                    border-radius: 999px;
                    font-weight: 600;
                }
                .foma-card-desc {
                    font-size: 0.88rem;
                    color: #475569;
                    line-height: 1.5;
                }
            </style>

            <div class="foma-gallery-grid">

                <!-- 1. Main Home Cabinet -->
                <div class="foma-phone-card">
                    <div class="foma-screen-frame">
                        <img src="b-system/img/foma/foma_main.png" alt="1. メイン画面 (起動キャビネット / Home Cabinet)" class="foma-screen-img">
                    </div>
                    <div class="foma-card-info">
                        <div class="foma-card-title">
                            <span>1. 起動キャビネット (Home)</span>
                            <span class="foma-badge">Screen 3.1</span>
                        </div>
                        <div class="foma-card-desc">
                            永続ホームキャビネット。連絡先(48)、メモ(23)、予定(12)、文書(17)、アプリ(8)等の実身を一覧表示。最上段ステータスバーに 3G 電波・時計・電池ブロック、下段に [選択][メニュー][終了] ソフトキーを配置。
                        </div>
                    </div>
                </div>

                <!-- 2. Contacts Explorer -->
                <div class="foma-phone-card">
                    <div class="foma-screen-frame">
                        <img src="b-system/img/foma/foma_contacts.png" alt="2. 連絡先キャビネット (Contacts Explorer)" class="foma-screen-img">
                    </div>
                    <div class="foma-card-info">
                        <div class="foma-card-title">
                            <span>2. 連絡先キャビネット</span>
                            <span class="foma-badge">Screen 3.5</span>
                        </div>
                        <div class="foma-card-desc">
                            五十音別グループ（あ・か・さ行）で階層化されたアドレス帳実身。選択中の行にハイコントラスト強調と ▶ カーソルを表示。ソフトキー [詳細][発信][戻る] で直接発信や詳細リンク閲覧が可能。
                        </div>
                    </div>
                </div>

                <!-- 3. Memos Explorer -->
                <div class="foma-phone-card">
                    <div class="foma-screen-frame">
                        <img src="b-system/img/foma/foma_memo.png" alt="3. メモ一覧 (Memos Explorer)" class="foma-screen-img">
                    </div>
                    <div class="foma-card-info">
                        <div class="foma-card-title">
                            <span>3. メモ一覧 (実身・仮身)</span>
                            <span class="foma-badge">Screen 3.6</span>
                        </div>
                        <div class="foma-card-desc">
                            日付付きテキスト実身と埋め込み仮身（Fusen）リンクの一覧。[仮身] 連絡先:坂村健 や [仮身] 予定:明日の会議 へのハイパーリンクがリスト内でシームレスに混在。
                        </div>
                    </div>
                </div>

                <!-- 4. T-Editor -->
                <div class="foma-phone-card">
                    <div class="foma-screen-frame">
                        <img src="b-system/img/foma/foma_editor.png" alt="4. 基本エディタ (T-Editor / Document View)" class="foma-screen-img">
                    </div>
                    <div class="foma-card-info">
                        <div class="foma-card-title">
                            <span>4. 基本エディタ (T-Editor)</span>
                            <span class="foma-badge">TAD Text</span>
                        </div>
                        <div class="foma-card-desc">
                            縦画面に最適化された BTRON 基本エディタ。行番号表示、TAD SPEC 3.20 準拠の構造化テキスト、および文中に埋め込まれた青色 [仮身] ハイパーリンクを直接閲覧・編集。
                        </div>
                    </div>
                </div>

                <!-- 5. Control Panel -->
                <div class="foma-phone-card">
                    <div class="foma-screen-frame">
                        <img src="b-system/img/foma/foma_control_panel.png" alt="5. コントロールパネル (Control Panel)" class="foma-screen-img">
                    </div>
                    <div class="foma-card-info">
                        <div class="foma-card-title">
                            <span>5. コントロールパネル</span>
                            <span class="foma-badge">Screen 3.3</span>
                        </div>
                        <div class="foma-card-desc">
                            端末環境設定ハブ。表示、5段トグル/ポケベル入力(TIP/Mozc)、省電力・スリープ、3G FOMA/i-mode 通信網、TRON Code 多国語文字平面、EnableWare 等の9項目をキーパッドで設定。
                        </div>
                    </div>
                </div>

                <!-- 6. Apps Launcher -->
                <div class="foma-phone-card">
                    <div class="foma-screen-frame">
                        <img src="b-system/img/foma/foma_other_apps.png" alt="6. アプリケーション一覧 (Apps Launcher)" class="foma-screen-img">
                    </div>
                    <div class="foma-card-info">
                        <div class="foma-card-title">
                            <span>6. アプリ一覧 (Apps)</span>
                            <span class="foma-badge">Screen 3.4</span>
                        </div>
                        <div class="foma-card-desc">
                            8種の組み込みモバイルアプリケーション。T-Editor、gterm端末シェル、TAD Browser（次期実装準備中）、簡易ペイント、XMPPチャット、計算機、カメラ等を数字キー 1〜8 で即座に起動。
                        </div>
                    </div>
                </div>

                <!-- Bonus: Contact Detail with Fusen -->
                <div class="foma-phone-card">
                    <div class="foma-screen-frame">
                        <img src="b-system/img/foma/foma_contact_detail.png" alt="連絡先詳細 (Contact Detail with [仮身] Fusen)" class="foma-screen-img">
                    </div>
                    <div class="foma-card-info">
                        <div class="foma-card-title">
                            <span>連絡先詳細 (実身・仮身構造)</span>
                            <span class="foma-badge">Detail</span>
                        </div>
                        <div class="foma-card-desc">
                            実身（連絡先）内部に配置された複数の仮身リンク。[仮身] 電話発信、[仮身] 電子メール、[仮身] 関連メモ、[仮身] スケジュールが連動し、選択キーで目的のアプリケーションが起動。
                        </div>
                    </div>
                </div>

                <!-- Bonus: Device Info -->
                <div class="foma-phone-card">
                    <div class="foma-screen-frame">
                        <img src="b-system/img/foma/foma_device_info.png" alt="端末情報 (Device Info)" class="foma-screen-img">
                    </div>
                    <div class="foma-card-info">
                        <div class="foma-card-title">
                            <span>端末情報 (Device Info)</span>
                            <span class="foma-badge">AArch32</span>
                        </div>
                        <div class="foma-card-desc">
                            TI OMAP2430 (ARM1136 AArch32) SoC 情報、坂村 T-Kernel 2.0 リアルタイムOS（14サブシステム稼動）、VirtIO MMIO (0x10001000) コントローラ、480×640 TFT 32-bpp 仕様を表示。
                        </div>
                    </div>
                </div>

            </div>

            <div style="margin-top: 2.5rem; padding-top: 1.5rem; border-top: 1px solid #e2e8f0;">
                <h4 style="font-size: 1.25rem; font-weight: 700; color: #003366; margin-bottom: 0.5rem;">
                    開いたダイアログ・操作メニュー画面 (Opened Dialogs & Context Menus)
                </h4>
                <p style="font-size: 0.95rem; color: #475569; margin-bottom: 1.5rem;">
                    B-TRON / 超漢字の HMI 設計指針に準拠した、モーダルダイアログ・警告ボックス・属性シート・TIP（テキスト入力プロセッサ）・電源管理・着信通知等の OS 構成要素の実動画面。
                </p>
            </div>

            <div class="foma-gallery-grid">

                <!-- 7. Opened Context Popup Menu -->
                <div class="foma-phone-card">
                    <div class="foma-screen-frame">
                        <img src="b-system/img/foma/foma_menu_popup.png" alt="操作ポップアップメニュー (Floating Context Popup Menu)" class="foma-screen-img">
                    </div>
                    <div class="foma-card-info">
                        <div class="foma-card-title">
                            <span>7. 浮動操作メニュー (Menu)</span>
                            <span class="foma-badge">Popup Overlay</span>
                        </div>
                        <div class="foma-card-desc">
                            [メニュー] キー押下時に前面へ展開されるドロップシャドウ付き浮動コンテキストメニュー。数字キー 1〜6 によるダイレクト選択、アクティブ行の青色ハイライト、および ▶ カーソルを完備。
                        </div>
                    </div>
                </div>

                <!-- 8. Delete Confirmation Modal -->
                <div class="foma-phone-card">
                    <div class="foma-screen-frame">
                        <img src="b-system/img/foma/foma_dialog_confirm.png" alt="実身削除の確認ダイアログ (Delete Confirmation Modal)" class="foma-screen-img">
                    </div>
                    <div class="foma-card-info">
                        <div class="foma-card-title">
                            <span>8. 実身削除確認ダイアログ</span>
                            <span class="foma-badge">Alert Modal</span>
                        </div>
                        <div class="foma-card-desc">
                            実身削除時に表示される警告モーダル。[！] 警告バッジ、削除対象名、および「参照している全ての仮身リンクが無効になる」旨の注意文、二重枠線、[削除実行][取消] ボタンを配置。
                        </div>
                    </div>
                </div>

                <!-- 9. Real Body Properties Sheet -->
                <div class="foma-phone-card">
                    <div class="foma-screen-frame">
                        <img src="b-system/img/foma/foma_dialog_properties.png" alt="実身属性詳細シート (Real Body Properties Sheet)" class="foma-screen-img">
                    </div>
                    <div class="foma-card-info">
                        <div class="foma-card-title">
                            <span>9. 実身属性詳細シート</span>
                            <span class="foma-badge">Properties</span>
                        </div>
                        <div class="foma-card-desc">
                            B-TRON 実身のメタデータ表示ダイアログ。HFDS 実身番号(ID)、データ種別(TAD Rev 3.20)、作成・更新日時、レコード長、仮身参照数、アクセス権(RW)、VirtIO保存先を表示。
                        </div>
                    </div>
                </div>

                <!-- 10. Search Dialog with TIP Virtual IME -->
                <div class="foma-phone-card">
                    <div class="foma-screen-frame">
                        <img src="b-system/img/foma/foma_dialog_search.png" alt="実身・仮身検索ダイアログ (Search Dialog with TIP Virtual IME)" class="foma-screen-img">
                    </div>
                    <div class="foma-card-info">
                        <div class="foma-card-title">
                            <span>10. 実身検索・TIP入力</span>
                            <span class="foma-badge">Input / IME</span>
                        </div>
                        <div class="foma-card-desc">
                            アクティブなテキスト入力ボックスと点滅キャレット、右上に [あ/漢] TIP/Mozc かな漢字変換ステータス、検索範囲ラジオボタン (●/○)、および仮身検索オプションチェックボックスを装備。
                        </div>
                    </div>
                </div>

                <!-- 11. Power Management & Sleep Dialog -->
                <div class="foma-phone-card">
                    <div class="foma-screen-frame">
                        <img src="b-system/img/foma/foma_dialog_power.png" alt="電源管理・サスペンドダイアログ (Power Management Dialog)" class="foma-screen-img">
                    </div>
                    <div class="foma-card-info">
                        <div class="foma-card-title">
                            <span>11. 電源管理・待受設定</span>
                            <span class="foma-badge">Power Modal</span>
                        </div>
                        <div class="foma-card-desc">
                            モバイル端末の省電力管理シート。電池残量バーグラフ（88% / 4.12V）、スリープ待機モード、T-Kernel 2.0 リセット、電源切断、キー誤操作ロックの各項目を選択可能。
                        </div>
                    </div>
                </div>

                <!-- 12. 3G Voice Call Alert Notification -->
                <div class="foma-phone-card">
                    <div class="foma-screen-frame">
                        <img src="b-system/img/foma/foma_dialog_call.png" alt="3G 音声着信ダイアログ (3G Incoming Call Alert)" class="foma-screen-img">
                    </div>
                    <div class="foma-card-info">
                        <div class="foma-card-title">
                            <span>12. 3G 音声通話 着信呼出</span>
                            <span class="foma-badge">Telephony</span>
                        </div>
                        <div class="foma-card-desc">
                            3G FOMA 音声通話着信オーバーレイ。着信中インジケータ、発信者名「坂村 健」、電話番号、呼出時間タイマー、および [応答][保留][拒否] の3系統アクションボタンを備える。
                        </div>
                    </div>
                </div>

            </div>
        </section>
"""

def update_mobile_html():
    if not os.path.exists(MOBILE_HTML):
        print(f"[ERROR] mobile.html not found at {MOBILE_HTML}")
        return False

    with open(MOBILE_HTML, "r", encoding="utf-8") as f:
        content = f.read()

    new_section = generate_foma_screens_html().strip()

    # If section already exists, replace it
    pattern = r"<!-- µBTRON-FOMA Mobile UI Showcase -->.*?<!-- Relation to B-System -->"
    if re.search(pattern, content, flags=re.DOTALL):
        updated = re.sub(pattern, new_section + "\n\n        <!-- Relation to B-System -->", content, flags=re.DOTALL)
    else:
        # Insert before "<!-- Relation to B-System -->"
        target_marker = "<!-- Relation to B-System -->"
        if target_marker in content:
            updated = content.replace(target_marker, new_section + "\n\n        " + target_marker)
        else:
            # Fallback: insert before </main>
            updated = content.replace("</main>", new_section + "\n    </main>")

    with open(MOBILE_HTML, "w", encoding="utf-8") as f:
        f.write(updated)

    print(f"[OK] Successfully updated {MOBILE_HTML} with live FOMA mobile screenshots.")
    return True

def main():
    print("===============================================================")
    print(" Updating FOMA Mobile UI Screenshots & HTML Documentation")
    print("===============================================================")

    cnt = convert_all_raws()
    print(f"Total converted PNG screens: {cnt}")
    update_mobile_html()
    print("===============================================================")

if __name__ == "__main__":
    main()
