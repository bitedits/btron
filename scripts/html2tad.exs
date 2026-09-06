#!/usr/bin/env elixir
# ==============================================================================
# BTRON3 SPEC 3.20 Native TAD Compiler: scripts/html2tad.exs
# Converts HTML documentation from ./doc/ into binary TAD files (.tad)
# Compatible with B-right/V Cho-Kanji (超漢字) and B-System Cleanroom OS.
# ==============================================================================

defmodule BtronTAD.Compiler do
  @moduledoc """
  Compiles semantic HTML into BTRON3 SPEC 3.20 binary TAD segments and symbolic TAD records.
  Implements strict noise filtering and lossless hyper-data Virtual Body link generation.
  """

  # ── BTRON3 SPEC 3.20 TAD Segment Tag Identifiers ────────────────────────────
  @ts_tpage   0xFFA0  # Text Page Fusen (Geometry, Margins)
  @ts_truler  0xFFA1  # Text Ruler Fusen (Indents, Line pitch, Tab stops)
  @ts_tfont   0xFFA2  # Text Font Fusen (Font ID, TRON Plane)
  @ts_tchar   0xFFA3  # Character Attributes (Point size, Weight, Color)
  @ts_vobj    0xFFA8  # Virtual Body Link Fusen (Real Body Pointer)
  @ts_fprim   0xFFB0  # Figure Primitive (Vector lines, Rectangles)

  # Record Type 1 = TAD Main Record
  @record_type_main 1

  # ── Noise Filter Rules (Unified per Source Catalog Profile) ─────────────────
  def filter_html_noise(html) do
    html
    # Common HTML envelope removal
    |> String.replace(~r/<!DOCTYPE[^>]*>/i, "")
    |> String.replace(~r/<head\b[^>]*>.*?<\/head>/is, "")
    |> String.replace(~r/<script\b[^>]*>.*?<\/script>/is, "")
    |> String.replace(~r/<style\b[^>]*>.*?<\/style>/is, "")
    |> String.replace(~r/<!--.*?-->/s, "")
    # Profile 1: ./doc/ & ./b-system/ (Ukrainian/Japanese header banner)
    |> String.replace(~r/<div style=['"][^'"]*(?:background:#0057b7|background:#333|background:#1b365d)[^'"]*['"]>.*?<\/div>/is, "")
    # Profile 2: ./b-hmi/ & ./b-free/ (Breadcrumbs & Navigation controls)
    |> String.replace(~r/<nav\b[^>]*>.*?<\/nav>/is, "")
    |> String.replace(~r/<div class=['"](?:header-nav|breadcrumb|top-banner)['"]>.*?<\/div>/is, "")
    # Profile 3: Common breadcrumb links ("Повернутися", "Попередня", "Наступна", "Back to", etc.)
    |> String.replace(~r/<a\s+href=['"][^'"]*['"]>(?:Повернутися|Попередня|Наступна|Return to|Previous|Next|Back to index|Головна)[^<]*<\/a>(?:<br\s*\/?>)?/iu, "")
    |> String.trim()
  end

  # ── HTML Entity Decoder ─────────────────────────────────────────────────────
  def decode_entities(text) do
    text
    |> String.replace("&nbsp;", " ")
    |> String.replace("&lt;", "<")
    |> String.replace("&gt;", ">")
    |> String.replace("&amp;", "&")
    |> String.replace("&quot;", "\"")
    |> String.replace("&apos;", "'")
    |> String.replace("&copy;", "©")
    |> String.replace("&mdash;", "—")
    |> String.replace("&ndash;", "–")
    |> String.replace("&bull;", "•")
  end

  # ── HTML Tokenizer & Semantic Parser ────────────────────────────────────────
  def parse_html(html) do
    clean_html = filter_html_noise(html)

    # Regex token stream for block & inline elements
    regex = ~r/(<h[1-6]\b[^>]*>.*?<\/h[1-6]>|<pre\b[^>]*>.*?<\/pre>|<p\b[^>]*>.*?<\/p>|<table\b[^>]*>.*?<\/table>|<ul\b[^>]*>.*?<\/ul>|<ol\b[^>]*>.*?<\/ol>|<hr\s*\/?>|<img\s+[^>]*>|<a\s+href=['"]([^'"]+)['"][^>]*>(.*?)<\/a>)/is

    elements =
      Regex.scan(regex, clean_html)
      |> Enum.map(fn match ->
        full = hd(match)
        lower = String.downcase(full)
        cond do
          String.starts_with?(lower, "<a") ->
            case Regex.run(~r/href=['"]([^'"]+)['"]/i, full) do
              [_, href] -> {:link, href, decode_entities(strip_tags(full))}
              _ -> {:text, decode_entities(strip_tags(full))}
            end
          String.starts_with?(lower, "<img") ->
            src =
              case Regex.run(~r/src=['"]([^'"]+)['"]/i, full) do
                [_, s] -> s
                _ -> "figure.png"
              end
            alt =
              case Regex.run(~r/alt=['"]([^'"]+)['"]/i, full) do
                [_, a] -> a
                _ -> ""
              end
            {:image, src, alt}
          String.starts_with?(lower, "<h1") -> {:h1, decode_entities(strip_tags(full))}
          String.starts_with?(lower, "<h2") -> {:h2, decode_entities(strip_tags(full))}
          String.starts_with?(lower, "<h3") -> {:h3, decode_entities(strip_tags(full))}
          String.starts_with?(lower, "<h4") or String.starts_with?(lower, "<h5") or String.starts_with?(lower, "<h6") ->
            {:h4, decode_entities(strip_tags(full))}
          String.starts_with?(lower, "<pre") -> {:pre, decode_entities(strip_tags(full))}
          String.starts_with?(lower, "<table") -> {:table, parse_table(full)}
          String.starts_with?(lower, "<ul") -> {:ul, parse_list(full)}
          String.starts_with?(lower, "<ol") -> {:ol, parse_list(full)}
          String.starts_with?(lower, "<hr") -> {:hr}
          String.starts_with?(lower, "<p") -> {:p, decode_entities(strip_tags(full))}
          true -> {:text, decode_entities(strip_tags(full))}
        end
      end)
      |> Enum.reject(fn
        {:p, ""} -> true
        {:text, ""} -> true
        _ -> false
      end)

    elements
  end

  defp strip_tags(html) do
    html
    |> String.replace(~r/<[^>]+>/, "")
    |> String.replace(~r/\s+/, " ")
    |> String.trim()
  end

  defp parse_list(html) do
    Regex.scan(~r/<li\b[^>]*>(.*?)<\/li>/is, html)
    |> Enum.map(fn [_, item] -> decode_entities(strip_tags(item)) end)
    |> Enum.reject(&(&1 == ""))
  end

  defp parse_table(html) do
    rows = Regex.scan(~r/<tr\b[^>]*>(.*?)<\/tr>/is, html)
    Enum.map(rows, fn [_, row_html] ->
      cells = Regex.scan(~r/<t[hd]\b[^>]*>(.*?)<\/t[hd]>/is, row_html)
      Enum.map(cells, fn [_, cell] -> decode_entities(strip_tags(cell)) end)
    end)
  end

  # ── Binary TAD Segment Builders ─────────────────────────────────────────────

  # Segment Packet: << Tag::16, Length::32, Payload::binary >>
  def make_segment(tag, payload) when is_integer(tag) and is_binary(payload) do
    len = byte_size(payload)
    <<tag::16-big, len::32-big, payload::binary>>
  end

  # Page Fusen (TS_TPAGE = 0xFFA0)
  def seg_page(width \\ 800, height \\ 1200, margin_l \\ 40, margin_t \\ 40) do
    payload = <<0::8, 0::8, height::16-big, width::16-big, margin_t::16-big, margin_t::16-big, margin_l::16-big, margin_l::16-big>>
    make_segment(@ts_tpage, payload)
  end

  # Font Fusen (TS_TFONT = 0xFFA2)
  def seg_font(font_id, plane \\ 1) do
    payload = <<0::8, font_id::16-big, plane::8>>
    make_segment(@ts_tfont, payload)
  end

  # Char Attribute Fusen (TS_TCHAR = 0xFFA3)
  def seg_char(size_pt, weight \\ 400, color_rgb \\ 0x000000) do
    payload = <<0::8, size_pt::16-big, weight::16-big, color_rgb::32-big>>
    make_segment(@ts_tchar, payload)
  end

  # Ruler Fusen (TS_TRULER = 0xFFA1)
  def seg_ruler(line_pitch \\ 22, indent \\ 0) do
    payload = <<0::8, line_pitch::16-big, indent::16-big>>
    make_segment(@ts_truler, payload)
  end

  # Virtual Body Link Fusen (TS_VOBJ = 0xFFA8)
  def seg_vobj(target_id, label, path \\ "") do
    label_bytes = :unicode.characters_to_binary(label, :utf8, :utf8)
    path_bytes = :unicode.characters_to_binary(path, :utf8, :utf8)
    l_len = byte_size(label_bytes)
    p_len = byte_size(path_bytes)

    payload = <<0::8, target_id::32-big, l_len::16-big, label_bytes::binary, p_len::16-big, path_bytes::binary>>
    make_segment(@ts_vobj, payload)
  end

  # Figure Line Separator (TS_FPRIM = 0xFFB0, SubID = 1)
  def seg_hr(width \\ 680) do
    payload = <<1::8, 0::8, 0x888888::32-big, 0::16-big, 0::16-big, width::16-big, 0::16-big>>
    make_segment(@ts_fprim, payload)
  end

  # BTRON3 Figure / Picture Segment (TS_FPRIM = 0xFFB0, SubID = 10)
  def seg_image(src, caption \\ "", width \\ 480, height \\ 140, type \\ 0) do
    cap_bytes = :unicode.characters_to_binary(caption, :utf8, :utf8)
    src_bytes = :unicode.characters_to_binary(src, :utf8, :utf8)
    c_len = byte_size(cap_bytes)
    s_len = byte_size(src_bytes)

    payload = <<10::8, width::16-big, height::16-big, type::8, c_len::16-big, cap_bytes::binary, s_len::16-big, src_bytes::binary>>
    make_segment(@ts_fprim, payload)
  end

  defp get_image_dimensions(file_path) do
    case File.read(file_path) do
      {:ok, << "GIF", _ver::binary-size(3), w::16-little, h::16-little, _rest::binary >>} ->
        {w, h}
      {:ok, << 0x89, "PNG\r\n\x1a\n", _chunk_len::32, "IHDR", w::32-big, h::32-big, _rest::binary >>} ->
        {w, h}
      _ ->
        {480, 140}
    end
  end

  # Text UTF-8 segment
  def seg_text(text) do
    text_bytes = :unicode.characters_to_binary(text <> "\n", :utf8, :utf8)
    text_bytes
  end

  # ── Full Binary Document Compilation ────────────────────────────────────────
  def compile_to_binary_tad(elements, _doc_title \\ "BTRON Document", base_dir \\ "") do
    init_segments = [
      seg_page(800, 1200, 40, 40),
      seg_font(0, 1),
      seg_char(12, 400, 0x000000),
      seg_ruler(22, 0)
    ]

    body_segments =
      Enum.flat_map(elements, fn
        {:h1, title} ->
          [
            seg_font(1, 1),
            seg_char(22, 700, 0x003366),
            seg_ruler(32, 0),
            seg_text("■ " <> title),
            seg_hr(680),
            seg_font(0, 1),
            seg_char(12, 400, 0x000000),
            seg_ruler(22, 0)
          ]

        {:h2, title} ->
          [
            seg_font(1, 1),
            seg_char(16, 700, 0x002244),
            seg_ruler(26, 0),
            seg_text("▶ " <> title),
            seg_font(0, 1),
            seg_char(12, 400, 0x000000),
            seg_ruler(22, 0)
          ]

        {:h3, title} ->
          [
            seg_font(1, 1),
            seg_char(14, 600, 0x333333),
            seg_text("◆ " <> title),
            seg_font(0, 1),
            seg_char(12, 400, 0x000000)
          ]

        {:h4, title} ->
          [
            seg_font(1, 1),
            seg_char(12, 600, 0x444444),
            seg_text("● " <> title),
            seg_font(0, 1),
            seg_char(12, 400, 0x000000)
          ]

        {:p, text} ->
          [seg_text(text)]

        {:pre, code} ->
          [
            seg_font(2, 1),
            seg_char(10, 400, 0x112233),
            seg_ruler(16, 20),
            seg_text(code),
            seg_font(0, 1),
            seg_char(12, 400, 0x000000),
            seg_ruler(22, 0)
          ]

        {:ul, items} ->
          Enum.map(items, fn item -> seg_text("  • " <> item) end)

        {:ol, items} ->
          Enum.with_index(items, 1)
          |> Enum.map(fn {item, idx} -> seg_text("  #{idx}. " <> item) end)

        {:table, rows} ->
          table_text =
            Enum.map_join(rows, "\n", fn row ->
              "| " <> Enum.join(row, " | ") <> " |"
            end)
          [
            seg_font(2, 1),
            seg_char(10, 400, 0x000000),
            seg_text(table_text),
            seg_font(0, 1),
            seg_char(12, 400, 0x000000)
          ]

        {:link, href, label} ->
          robj_id = :erlang.phash2(href, 100_000) + 1000
          [
            seg_vobj(robj_id, label, href)
          ]

        {:image, src, alt} ->
          caption = if alt != "", do: alt, else: "BTRON3 Figure / Picture: " <> Path.basename(src)
          actual_path = if base_dir != "" and not File.exists?(src), do: Path.join(base_dir, src), else: src
          {w, h} = get_image_dimensions(actual_path)
          [
            seg_image(src, caption, w, h, 0)
          ]

        {:hr} ->
          [seg_hr(680)]

        {:text, text} ->
          [seg_text(text)]
      end)

    payload = IO.iodata_to_binary([init_segments, body_segments])
    payload_size = byte_size(payload)

    <<@record_type_main::16-big, payload_size::32-big, payload::binary>>
  end

  # ── Symbolic Text TAD Representation ────────────────────────────────────────
  def compile_to_symbolic_tad(elements, doc_title \\ "BTRON Document", base_dir \\ "") do
    header = """
    ================================================================================
    TAD Real Body [実身] : #{doc_title}
    RECORD TYPE : 1 (TAD Main Record) | BTRON3 SPEC 3.20 Cleanroom Edition
    DATE        : #{Date.utc_today()} | GENERATOR : BtronTAD.Compiler (Elixir OTP 29)
    ================================================================================

    [付箋: DOCUMENT_HEADER | Title="#{doc_title}" | Font="Cho-Kanji-Mincho" | Size=16]
    """

    body =
      Enum.map_join(elements, "\n\n", fn
        {:h1, title} -> "【#{title}】\n" <> String.duplicate("━", 40)
        {:h2, title} -> "■ #{title}\n" <> String.duplicate("─", 40)
        {:h3, title} -> "▶ #{title}"
        {:h4, title} -> "◆ #{title}"
        {:p, text} -> text
        {:pre, code} -> "```\n#{code}\n```"
        {:ul, items} -> Enum.map_join(items, "\n", &("  • " <> &1))
        {:ol, items} ->
          Enum.with_index(items, 1)
          |> Enum.map_join("\n", fn {item, idx} -> "  #{idx}. #{item}" end)
        {:table, rows} ->
          Enum.map_join(rows, "\n", fn row -> "| " <> Enum.join(row, " | ") <> " |" end)
        {:link, href, label} ->
          robj_id = :erlang.phash2(href, 100_000) + 1000
          "[仮身] ##{robj_id} : #{label} -> [#{href}]"
        {:image, src, alt} ->
          actual_path = if base_dir != "" and not File.exists?(src), do: Path.join(base_dir, src), else: src
          {w, h} = get_image_dimensions(actual_path)
          "[付箋: FIGURE w=#{w} h=#{h} src=\"#{src}\" caption=\"#{alt}\"]\n" <>
          "[画像: #{if(alt != "", do: alt, else: Path.basename(src))}]"
        {:hr} -> String.duplicate("─", 70)
        {:text, text} -> text
      end)

    header <> "\n" <> body <> "\n"
  end

  # ── Unified File Processing Pipeline ────────────────────────────────────────
  def process_file(html_path, target_dir, _source_root \\ "") do
    File.mkdir_p!(target_dir)

    # Copy associated assets (gif/, img/, figures/) into target directory
    html_dir = Path.dirname(html_path)
    for asset_name <- ["gif", "img", "figures"] do
      src_asset_dir = Path.join(html_dir, asset_name)
      tgt_asset_dir = Path.join(target_dir, asset_name)
      if File.dir?(src_asset_dir) and not File.dir?(tgt_asset_dir) do
        File.cp_r!(src_asset_dir, tgt_asset_dir)
      end
    end

    base_name = Path.basename(html_path, ".html")
    bin_path = Path.join(target_dir, base_name <> ".tad")
    txt_path = Path.join(target_dir, base_name <> ".tad.txt")

    html_content = File.read!(html_path)
    elements = parse_html(html_content)

    title =
      case Enum.find(elements, fn tup -> elem(tup, 0) in [:h1, :h2] end) do
        {_, t} -> t
        _ -> base_name
      end

    base_dir = html_dir
    binary_tad = compile_to_binary_tad(elements, title, base_dir)
    symbolic_tad = compile_to_symbolic_tad(elements, title, base_dir)

    File.write!(bin_path, binary_tad)
    File.write!(txt_path, symbolic_tad)

    %{
      html_path: html_path,
      bin_path: bin_path,
      txt_path: txt_path,
      elements_count: length(elements),
      bin_bytes: byte_size(binary_tad),
      title: title
    }
  end

  # ── Built-In Unit Test Suite ────────────────────────────────────────────────
  def run_tests do
    IO.puts("==========================================================")
    IO.puts(" BTRON3 SPEC 3.20 TAD Compiler Test Suite (Elixir)")
    IO.puts("==========================================================")

    test_html = """
    <!DOCTYPE HTML>
    <html>
      <head><title>Test Doc</title><style>.x { color: red; }</style></head>
      <body>
        <div style='background:#0057b7; color:#ffd700;'>Специфікація BTRON3</div>
        <a href="index.html">Повернутися до змісту</a>
        <h1>Розділ 1. Основні типи даних</h1>
        <p>BTRON підтримує 8-бітні, 16-бітні та 32-бітні типи даних.</p>
        <pre>typedef char B;\ntypedef short H;</pre>
        <ul><li>Item 1</li><li>Item 2</li></ul>
        <a href="tad1.html">Посилання на TAD специфікацію</a>
      </body>
    </html>
    """

    clean = filter_html_noise(test_html)
    assert(not String.contains?(clean, "Специфікація BTRON3"), "Noise banner filtered")
    assert(not String.contains?(clean, "Повернутися"), "Breadcrumb filtered")
    assert(not String.contains?(clean, "<style>"), "Style tags removed")

    elements = parse_html(test_html)
    assert(Enum.any?(elements, &match?({:h1, "Розділ 1. Основні типи даних"}, &1)), "Parsed H1 element")
    assert(Enum.any?(elements, &match?({:p, _}, &1)), "Parsed P element")
    assert(Enum.any?(elements, &match?({:pre, _}, &1)), "Parsed PRE element")
    assert(Enum.any?(elements, &match?({:ul, ["Item 1", "Item 2"]}, &1)), "Parsed UL list")
    assert(Enum.any?(elements, &match?({:link, "tad1.html", "Посилання на TAD специфікацію"}, &1)), "Parsed A link to Virtual Body")

    bin_tad = compile_to_binary_tad(elements, "Unit Test TAD")
    <<rec_type::16-big, payload_len::32-big, payload::binary>> = bin_tad
    assert(rec_type == 1, "Record Type is 1 (TAD Main Record)")
    assert(payload_len == byte_size(payload), "Payload length header matches exact byte size")
    assert(byte_size(bin_tad) > 100, "Binary TAD contains structured segments")

    assert(String.contains?(payload, <<@ts_tpage::16-big>>), "Contains TS_TPAGE segment (0xFFA0)")
    assert(String.contains?(payload, <<@ts_tfont::16-big>>), "Contains TS_TFONT segment (0xFFA2)")
    assert(String.contains?(payload, <<@ts_tchar::16-big>>), "Contains TS_TCHAR segment (0xFFA3)")
    assert(String.contains?(payload, <<@ts_vobj::16-big>>), "Contains TS_VOBJ segment (0xFFA8)")

    IO.puts("==========================================================")
    IO.puts(" ALL ELIXIR TAD COMPILER UNIT TESTS PASSED (100%)")
    IO.puts("==========================================================\n")
  end

  # ── Canonical 4 Books Compiler (Integrated Directly into ./tad_bin/) ────────
  def compile_foundational_books(target_dir \\ "tad_bin") do
    File.mkdir_p!(target_dir)

    btron3_elements = [
      {:h1, "BTRON3 仕様書 バージョン 3.20.00 (Sakamura BTRON Architecture)"},
      {:image, "doc/shared_data/gif/all_struct.gif", "図 1: BTRON3 実身・仮身データ構造仕様 (Shared Data Structure)"},
      {:link, "shared_data/index.tad", "Part 1: 共通データ構造仕様 (Shared Data Specifications)"},
      {:link, "os_spec/index.tad", "Part 2: オペレーティングシステム機能仕様 (OS Specification)"},
      {:link, "os_spec/indexfig.tad", "Static Analysis & Bounded Heap Memory Model (NASA JPL Rule 3)"},
      {:h2, "目次 (Table of Contents)"},
      {:h3, "第1部：共通データ構造仕様 (Shared Data)"},
      {:link, "shared_data/data_type.tad", "第1章 基本データ型とエラーコード (Data Types & Error Codes)"},
      {:link, "shared_data/tron_code.tad", "第2章 TRONコード文字体系仕様 (TRON Multilingual Character Code)"},
      {:link, "shared_data/tad1.tad", "第3章 TAD (TRON Application Databus) 文書フォーマット仕様"},
      {:link, "shared_data/fd_format.tad", "第4章 BTRON FS 実身／仮身ファイルシステム構造仕様"},
      {:h3, "第2部：OS機能仕様 (OS Specification)"},
      {:link, "os_spec/kernel/kernel.tad", "第5章 μITRON リアルタイムカーネルとタスク管理 (Kernel & Tasking)"},
      {:link, "os_spec/dp/dp.tad", "第6章 表示プリミティブ DP (Display Primitives Graphics Engine)"},
      {:link, "os_spec/shell/shell.tad", "第7章 GUIシェルとウィンドウマネージャ (Window Manager & Shell)"},
      {:link, "os_spec/indexfig.tad", "第8章 静的解析と決定論的メモリ制約 (Static Scope & Bounded Memory)"},
      {:hr},
      {:h2, "基本データ型定義 (C99 Types)"},
      {:pre, "typedef char            B;   /* 符号付き 8ビット整数 */\ntypedef short           H;   /* 符号付き 16ビット整数 */\ntypedef int             W;   /* 符号付き 32ビット整数 */\ntypedef unsigned char   UB;  /* 符号なし 8ビット整数 */\ntypedef unsigned short  UH;  /* 符号なし 16ビット整数 */\ntypedef unsigned int    UW;  /* 符号なし 32ビット整数 */\ntypedef void           *VP;  /* 汎用ポインタ */"},
      {:p, "BTRON3仕様では、ブート完了後の動的ヒープ確保 (malloc/free) を完全禁止し、全てのメモリ領域を有界化します。"}
    ]

    tkernel_elements = [
      {:h1, "T-Kernel 2.0 リアルタイムOS仕様書及び開発ガイド"},
      {:image, "doc/os_spec/kernel/gif/processtask.gif", "図 2: μITRON リアルタイムタスク状態遷移図 (Task State Machine)"},
      {:link, "t-kernel/tkernel_spec.tad", "第1章 T-Kernel 2.0 コアアーキテクチャ (Core Architecture)"},
      {:link, "t-kernel/tkernel_startup.tad", "第2章 ブート及び初期化シーケンス (Startup Sequence)"},
      {:link, "t-kernel/tkernel_qemu.tad", "第3章 QEMU仮想環境とボード展開 (QEMU & Board Deployment)"},
      {:link, "t-kernel/index.tad", "第4章 T-Kernel 2.0 開発者ドキュメント索引 (Developer Index)"},
      {:h2, "タスク状態遷移モデル (Task State Model)"},
      {:ul, [
        "DORMANT   : 休止状態 (タスク生成後未起動、または終了後)",
        "READY     : 実行可能状態 (CPU割り当て待ちキューに待機)",
        "RUNNING   : 実行状態 (CPU上で実行中)",
        "WAITING   : 待ち状態 (セマフォ、タイマー、メッセージ待ち)",
        "SUSPENDED : 強制待ち状態 (他タスクからのsus_tsk要求)"
      ]},
      {:h2, "コアシステムコール (Core APIs)"},
      {:pre, "ER tk_cre_tsk(T_CTSK *pk_ctsk);\nER tk_sta_tsk(ID tskid, INT stacd);\nER tk_ext_tsk(void);\nER tk_dly_tsk(TMO dlytim);\nER tk_wai_sem(ID semid, INT cnt, TMO tmout);"}
    ]

    bfree_elements = [
      {:h1, "B-Free 自由なBTRON3オペレーティングシステム技術解説書"},
      {:image, "doc/os_spec/kernel/gif/filesystem.gif", "図 3: BTRON ファイルシステム構造仕様 (Filesystem Structure)"},
      {:link, "b-free/manifest.tad", "第1章 B-Free マニフェストと自由ソフトウェアの理念"},
      {:link, "b-free/kernel.tad", "第2章 μITRON 3.0 マイクロカーネルアーキテクチャ"},
      {:link, "b-free/posix.tad", "第3章 POSIXエミュレーション層とシステムコール"},
      {:link, "b-free/btron.tad", "第4章 B-Free OS 統合デスクトップ環境"},
      {:link, "b-free/boot_arch.tad", "第5章 ブート機構とソースツリー構造"},
      {:link, "b-free/source_tree.tad", "第6章 B-Free ソースツリー構成とビルド体系"},
      {:h2, "設計理念と自由ソフトウェアの精神"},
      {:p, "Ken Sakamura教授が提唱した「万人に開かれた標準」をGPL (GNU General Public License) の下で実現するクリーンルーム実装。"}
    ]

    tron_hmi_elements = [
      {:h1, "TRON 人間・機械インタフェース (HMI) 設計仕様書及び標準カタログ"},
      {:image, "doc/os_spec/shell/gif/title_bar.gif", "図 4: BTRON3 標準ウィンドウとタイトルバー意匠 (Window Geometry)"},
      {:link, "b-hmi/index.tad", "第1章 TRON HMI 統合仕様書・設計指針ガイド (HMI Specification)"},
      {:link, "b-hmi/part1/chap03_sui.tad", "第2章 SUI 実身操作パネル標準仕様 (Standard User Interface)"},
      {:link, "b-hmi/part1/chap04_gui.tad", "第3章 GUI ウィンドウマネージャと角枠リサイズ意匠 (GUI Blueprint)"},
      {:link, "b-hmi/part_book/switches.tad", "第4章 HMI部品カタログ：プッシュ・シーソー・トグルスイッチ仕様"},
      {:link, "b-hmi/part_book/volumes.tad", "第5章 HMI部品カタログ：ロータリーダイヤル・ポテンショメータ仕様"},
      {:link, "b-hmi/part_book/selectors.tad", "第6章 HMI部品カタログ：ラジオボタン・マトリクスセレクタ仕様"},
      {:link, "os_spec/shell/parts.tad", "第7章 BTRON3 シェル標準GUI部品仕様 (Shell Parts Book)"},
      {:h2, "標準動作三原則 (The Standard Action Triad)"},
      {:ol, [
        "操作対象の指定 (Target Selection)",
        "操作内容の指定 (Operation Specification)",
        "実行の確認／確定 (Execution Confirmation)"
      ]},
      {:h2, "標準HMI部品カタログ (Parts Book)"},
      {:ul, [
        "プッシュスイッチ (Push Switch - モメンタリ／オルタネート)",
        "アップダウンスステッパ (Up/Down Stepper)",
        "ラジオボタンマトリクス (Radio Matrix - 排他的選択)",
        "ロータリーダイヤル (Rotary Dial - 270度連続角度調整)",
        "セグメントVUメーター (Bar VU Meter - ピークホールド付き)",
        "ユニバーサルコントローラ (Universal Controller - 7キー統一リモート)"
      ]}
    ]

    base_books = [
      {"01_btron3_spec", "BTRON3 3.20 Specification Book", btron3_elements},
      {"02_tkernel_book", "T-Kernel 2.0 Real-Time OS Book", tkernel_elements},
      {"03_bfree_os_book", "B-Free Operating System Book", bfree_elements}
    ]

    books = if File.dir?("b-hmi") do
      base_books ++ [{"04_tron_hmi_book", "TRON Human-Machine Interface Book", tron_hmi_elements}]
    else
      base_books
    end

    IO.puts("======================================================================")
    IO.puts(" B-System Foundational TAD Books Compiler (True BTRON3 SPEC 3.20)")
    IO.puts(" Output Directory: ./#{target_dir}/")
    IO.puts("======================================================================")

    Enum.each(books, fn {base_name, title, elems} ->
      bin_tad = compile_to_binary_tad(elems, title)
      txt_tad = compile_to_symbolic_tad(elems, title)

      bin_path = Path.join(target_dir, base_name <> ".tad")
      txt_path = Path.join(target_dir, base_name <> ".tad.txt")

      File.write!(bin_path, bin_tad)
      File.write!(txt_path, txt_tad)

      in_name = String.pad_trailing(base_name <> ".tad", 26)
      IO.puts("  [COMPILED BOOK]   #{in_name} : #{String.pad_trailing(title, 38)} (#{byte_size(bin_tad)} bytes)")
    end)

    IO.puts("======================================================================")
    IO.puts(" Successfully generated all #{length(books)} Canonical Binary TAD books in ./#{target_dir}/")
    IO.puts("======================================================================\n")
  end

  defp assert(true, msg), do: IO.puts("  [PASS] #{msg}")
  defp assert(false, msg) do
    IO.puts("  [FAIL] #{msg}")
    System.halt(1)
  end
end

# ── Main Entrypoint ───────────────────────────────────────────────────────────
args = System.argv()

if "--test" in args do
  BtronTAD.Compiler.run_tests()
end

out_dir = "tad_bin"
File.mkdir_p!(out_dir)

# 1. Compile Canonical BTRON3 Books directly into ./tad_bin/
BtronTAD.Compiler.compile_foundational_books(out_dir)

# 2. Unified HTML -> TAD Conversion for Local Source Catalogs
# Catalog Profiles:
#   - doc/      : BTRON3 Standard Specification (Shared Data, μITRON Kernel, DP Graphics, Shell)
#   - b-free/   : Free Software BTRON3 Architecture Manifesto & Cleanroom Kernel
#   - b-system/ : B-System Posix & VirtIO Core Architecture & System Specifications
#   - t-kernel/ : T-Kernel 2.0 Real-Time OS & Board Deployment Manuals
#   - b-hmi/    : TRON Human-Machine Interface Guidelines (if present locally)
source_trees = [
  {"doc", out_dir},
  {"b-free", Path.join(out_dir, "b-free")},
  {"b-system", Path.join(out_dir, "b-system")},
  {"t-kernel", Path.join(out_dir, "t-kernel")}
] ++ (if File.dir?("b-hmi"), do: [{"b-hmi", Path.join(out_dir, "b-hmi")}], else: [])

IO.puts("======================================================================")
IO.puts(" B-System HTML -> Binary BTRON 3.20 TAD Unified Batch Compiler (Elixir)")
IO.puts(" Output Directory        : ./#{out_dir}/")
IO.puts(" Active Source Catalogs  : #{Enum.map_join(source_trees, ", ", fn {s, _} -> s end)}")
IO.puts(" Target Specifications   : BTRON3 SPEC 3.20 / B-right/V Cho-Kanji")
IO.puts("======================================================================")

all_results =
  Enum.flat_map(source_trees, fn {src_root, dst_root} ->
    if File.dir?(src_root) do
      html_files = Path.wildcard(Path.join(src_root, "**/*.html"))
      Enum.map(html_files, fn file ->
        rel = Path.relative_to(file, src_root)
        sub_dir = Path.dirname(rel)
        target_dir = if sub_dir == ".", do: dst_root, else: Path.join(dst_root, sub_dir)
        res = BtronTAD.Compiler.process_file(file, target_dir, src_root)
        in_name = String.pad_trailing(Path.join(src_root, rel), 32)
        out_name = String.pad_trailing(Path.relative_to(res.bin_path, out_dir), 32)
        IO.puts("  [COMPILED] #{in_name} -> #{out_name} (#{res.elements_count} items, #{res.bin_bytes} bytes)")
        res
      end)
    else
      []
    end
  end)

# Also compile root index.html and releases.html if present
root_index = "index.html"
if File.exists?(root_index) do
  res = BtronTAD.Compiler.process_file(root_index, out_dir, "")
  IO.puts("  [COMPILED] index.html                       -> index.tad                        (#{res.elements_count} items, #{res.bin_bytes} bytes)")
end

root_releases = "releases.html"
if File.exists?(root_releases) do
  res = BtronTAD.Compiler.process_file(root_releases, out_dir, "")
  IO.puts("  [COMPILED] releases.html                    -> releases.tad                     (#{res.elements_count} items, #{res.bin_bytes} bytes)")
end

total_bytes = Enum.sum(Enum.map(all_results, & &1.bin_bytes))
IO.puts("======================================================================")
IO.puts(" Successfully compiled #{length(all_results)} HTML documents into Native Binary TAD files.")
IO.puts(" Total TAD Binary Size: #{total_bytes} bytes across ./#{out_dir}/")
IO.puts("======================================================================")
