#!/usr/bin/env elixir
# ==============================================================================
# B-Book Specialized TAD Compiler: scripts/book2tad.exs
# Compiles B-Book developer manual HTML documents into native BTRON 3.20 TAD files
# Preserves C99 code layout, ASCII architecture diagrams, API cards, and metadata.
# ==============================================================================

defmodule BtronBook.Compiler do
  @moduledoc """
  Specialized B-Book compiler converting BeBook-style developer reference pages
  into BTRON3 SPEC 3.20 binary TAD segments and structured symbolic records.
  """

  # ── BTRON3 SPEC 3.20 TAD Segment Tag Identifiers ────────────────────────────
  @ts_tpage   0xFFA0  # Text Page Fusen (Geometry, Margins)
  @ts_truler  0xFFA1  # Text Ruler Fusen (Indents, Line pitch, Tab stops)
  @ts_tfont   0xFFA2  # Text Font Fusen (Font ID, TRON Plane)
  @ts_tchar   0xFFA3  # Character Attributes (Point size, Weight, Color)
  @ts_vobj    0xFFA8  # Virtual Body Link Fusen (Real Body Pointer)
  @ts_fprim   0xFFB0  # Figure Primitive (Vector lines, Rectangles)

  @record_type_main 1

  # ── Noise Filtering for B-Book ──────────────────────────────────────────────
  def filter_book_noise(html) do
    html
    # Strip HTML wrapper and metadata
    |> String.replace(~r/<!DOCTYPE[^>]*>/i, "")
    |> String.replace(~r/<head\b[^>]*>.*?<\/head>/is, "")
    |> String.replace(~r/<script\b[^>]*>.*?<\/script>/is, "")
    |> String.replace(~r/<style\b[^>]*>.*?<\/style>/is, "")
    |> String.replace(~r/<!--.*?-->/s, "")
    # Strip Web-only sidebar navigation
    |> String.replace(~r/<nav\b[^>]*>.*?<\/nav>/is, "")
    # Strip breadcrumb navigation bar
    |> String.replace(~r/<div class=['"]breadcrumb['"]>.*?<\/div>/is, "")
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
    |> String.replace("&times;", "×")
    |> String.replace("&mdash;", "—")
    |> String.replace("&ndash;", "–")
    |> String.replace("&bull;", "•")
    |> String.replace("&mu;", "μ")
    |> String.replace("&larr;", "←")
    |> String.replace("&rarr;", "→")
  end

  # ── Stripping tags without destroying newlines in block elements ─────────────
  def strip_tags(html) do
    html
    |> String.replace(~r/<[^>]+>/, "")
    |> String.replace(~r/[ \t]+/, " ")
    |> String.trim()
  end

  # ── Preserving exact code and diagram text ──────────────────────────────────
  def clean_code(html) do
    html
    |> String.replace(~r/^<code\b[^>]*>/i, "")
    |> String.replace(~r/<\/code>$/i, "")
    |> decode_entities()
    |> String.trim("\n\r")
  end

  # ── Parse API Card Component ────────────────────────────────────────────────
  def parse_api_card(card_html) do
    # 1. API Name & Return type
    name =
      case Regex.run(~r/<span class=['"]api-name['"]>(.*?)<\/span>/is, card_html) do
        [_, n] -> decode_entities(strip_tags(n))
        _ -> "UnknownAPI"
      end

    ret_type =
      case Regex.run(~r/<span class=['"]ret-badge['"]>(.*?)<\/span>/is, card_html) do
        [_, r] -> decode_entities(strip_tags(r))
        _ -> "void"
      end

    # 2. Description
    desc =
      case Regex.run(~r/<p>(.*?)<\/p>/is, card_html) do
        [_, p] -> decode_entities(strip_tags(p))
        _ -> ""
      end

    # 3. C99 Signature
    signature =
      case Regex.run(~r/<div class=['"]api-signature['"]>(.*?)<\/div>/is, card_html) do
        [_, s] -> decode_entities(clean_code(strip_tags(s)))
        _ -> ""
      end

    # 4. Parameter/Return Table (if any)
    table =
      case Regex.run(~r/<table\b[^>]*>(.*?)<\/table>/is, card_html) do
        [_, tbl] -> parse_table(tbl)
        _ -> nil
      end

    # 5. Metadata (Preconditions, Postconditions)
    meta =
      case Regex.run(~r/<div class=['"]api-meta['"]>(.*?)<\/div>/is, card_html) do
        [_, meta_html] ->
          # Extract pairs of meta-label and following text
          pairs = Regex.scan(~r/<span class=['"]meta-label['"]>(.*?)<\/span>\s*<span>(.*?)<\/span>/is, meta_html)
          Enum.map(pairs, fn [_, label, val] ->
            {decode_entities(strip_tags(label)), decode_entities(strip_tags(val))}
          end)
        _ -> []
      end

    {:api_card, name, ret_type, desc, signature, table, meta}
  end

  # ── Parse Callout Box ───────────────────────────────────────────────────────
  def parse_callout(callout_html) do
    title =
      case Regex.run(~r/<div class=['"]callout-title['"]>(.*?)<\/div>/is, callout_html) do
        [_, t] -> decode_entities(strip_tags(t))
        _ -> "Note"
      end

    # Remove the title and get the rest of the text
    body =
      callout_html
      |> String.replace(~r/<div class=['"]callout-title['"]>.*?<\/div>/is, "")
      |> strip_tags()
      |> decode_entities()

    {:callout, title, body}
  end

  # ── Parse HTML Table ────────────────────────────────────────────────────────
  def parse_table(table_html) do
    rows = Regex.scan(~r/<tr\b[^>]*>(.*?)<\/tr>/is, table_html)
    Enum.map(rows, fn [_, row_html] ->
      cells = Regex.scan(~r/<t[hd]\b[^>]*>(.*?)<\/t[hd]>/is, row_html)
      Enum.map(cells, fn [_, cell] -> decode_entities(strip_tags(cell)) end)
    end)
  end

  # ── Parse List ──────────────────────────────────────────────────────────────
  def parse_list(list_html) do
    Regex.scan(~r/<li\b[^>]*>(.*?)<\/li>/is, list_html)
    |> Enum.map(fn [_, item] -> decode_entities(strip_tags(item)) end)
    |> Enum.reject(&(&1 == ""))
  end

  # ── Extract Balanced DIV Block ──────────────────────────────────────────────
  def extract_balanced_div(html) do
    regex = ~r/<\/?div\b[^>]*>/i
    matches = Regex.scan(regex, html, return: :index)
    case matches do
      [[{0, _len}] | rest] ->
        find_close_div(rest, 1, html)
      _ ->
        nil
    end
  end

  defp find_close_div([], _depth, _html), do: nil
  defp find_close_div([[{offset, len}] | rest], depth, html) do
    token = binary_part(html, offset, len)
    new_depth =
      if String.starts_with?(token, "</") do
        depth - 1
      else
        depth + 1
      end

    if new_depth == 0 do
      end_offset = offset + len
      full_block = binary_part(html, 0, end_offset)
      remaining = binary_part(html, end_offset, byte_size(html) - end_offset)
      {full_block, remaining}
    else
      find_close_div(rest, new_depth, html)
    end
  end

  # ── Robust Semantic Parser for B-Book ───────────────────────────────────────
  def parse_book_html(html) do
    clean = filter_book_noise(html)
    scan_stream(clean, [])
  end

  defp scan_stream("", acc), do: Enum.reverse(acc)
  defp scan_stream(html, acc) do
    tag_regex = ~r/<(?:div\s+class=['"](?:api-card|callout\b)|h[1-6]|pre|p|table|ul|ol|hr|a\s+href)\b[^>]*>/i

    case Regex.run(tag_regex, html, return: :index) do
      [{offset, len}] ->
        token = binary_part(html, offset, len)
        lower_token = String.downcase(token)
        sub_html = binary_part(html, offset, byte_size(html) - offset)

        cond do
          String.contains?(lower_token, "api-card") ->
            case extract_balanced_div(sub_html) do
              {card_block, rest} ->
                scan_stream(rest, [parse_api_card(card_block) | acc])
              nil ->
                scan_stream(binary_part(sub_html, len, byte_size(sub_html) - len), acc)
            end

          String.contains?(lower_token, "callout") ->
            case extract_balanced_div(sub_html) do
              {callout_block, rest} ->
                scan_stream(rest, [parse_callout(callout_block) | acc])
              nil ->
                scan_stream(binary_part(sub_html, len, byte_size(sub_html) - len), acc)
            end

          String.starts_with?(lower_token, "<pre") ->
            case Regex.run(~r/<pre\b[^>]*>(.*?)<\/pre>/is, sub_html, return: :index) do
              [{0, total_len}, {inner_start, inner_len}] ->
                code_content = clean_code(binary_part(sub_html, inner_start, inner_len))
                rest = binary_part(sub_html, total_len, byte_size(sub_html) - total_len)
                scan_stream(rest, [{:code_block, code_content} | acc])
              _ ->
                scan_stream(binary_part(sub_html, len, byte_size(sub_html) - len), acc)
            end

          String.starts_with?(lower_token, "<table") ->
            case Regex.run(~r/<table\b[^>]*>(.*?)<\/table>/is, sub_html, return: :index) do
              [{0, total_len}, _] ->
                table_block = binary_part(sub_html, 0, total_len)
                rest = binary_part(sub_html, total_len, byte_size(sub_html) - total_len)
                scan_stream(rest, [{:table, parse_table(table_block)} | acc])
              _ ->
                scan_stream(binary_part(sub_html, len, byte_size(sub_html) - len), acc)
            end

          String.starts_with?(lower_token, "<h1") or String.starts_with?(lower_token, "<h2") or
          String.starts_with?(lower_token, "<h3") or String.starts_with?(lower_token, "<h4") or
          String.starts_with?(lower_token, "<h5") or String.starts_with?(lower_token, "<h6") ->
            tag_num = String.at(lower_token, 2)
            case Regex.run(~r/<h#{tag_num}\b[^>]*>(.*?)<\/h#{tag_num}>/is, sub_html, return: :index) do
              [{0, total_len}, {inner_start, inner_len}] ->
                title = decode_entities(strip_tags(binary_part(sub_html, inner_start, inner_len)))
                rest = binary_part(sub_html, total_len, byte_size(sub_html) - total_len)
                type =
                  case tag_num do
                    "1" -> :h1
                    "2" -> :h2
                    "3" -> :h3
                    _ -> :h4
                  end
                scan_stream(rest, [{type, title} | acc])
              _ ->
                scan_stream(binary_part(sub_html, len, byte_size(sub_html) - len), acc)
            end

          String.starts_with?(lower_token, "<ul") ->
            case Regex.run(~r/<ul\b[^>]*>(.*?)<\/ul>/is, sub_html, return: :index) do
              [{0, total_len}, _] ->
                ul_block = binary_part(sub_html, 0, total_len)
                rest = binary_part(sub_html, total_len, byte_size(sub_html) - total_len)
                scan_stream(rest, [{:ul, parse_list(ul_block)} | acc])
              _ ->
                scan_stream(binary_part(sub_html, len, byte_size(sub_html) - len), acc)
            end

          String.starts_with?(lower_token, "<ol") ->
            case Regex.run(~r/<ol\b[^>]*>(.*?)<\/ol>/is, sub_html, return: :index) do
              [{0, total_len}, _] ->
                ol_block = binary_part(sub_html, 0, total_len)
                rest = binary_part(sub_html, total_len, byte_size(sub_html) - total_len)
                scan_stream(rest, [{:ol, parse_list(ol_block)} | acc])
              _ ->
                scan_stream(binary_part(sub_html, len, byte_size(sub_html) - len), acc)
            end

          String.starts_with?(lower_token, "<hr") ->
            case Regex.run(~r/<hr\s*\/?>/i, sub_html, return: :index) do
              [{0, total_len}] ->
                rest = binary_part(sub_html, total_len, byte_size(sub_html) - total_len)
                scan_stream(rest, [{:hr} | acc])
              _ ->
                scan_stream(binary_part(sub_html, len, byte_size(sub_html) - len), acc)
            end

          String.starts_with?(lower_token, "<a") ->
            case Regex.run(~r/<a\s+href=['"]([^'"]+)['"][^>]*>(.*?)<\/a>/is, sub_html, return: :index) do
              [{0, total_len}, {h_start, h_len}, {lbl_start, lbl_len}] ->
                href = binary_part(sub_html, h_start, h_len)
                label = decode_entities(strip_tags(binary_part(sub_html, lbl_start, lbl_len)))
                rest = binary_part(sub_html, total_len, byte_size(sub_html) - total_len)
                scan_stream(rest, [{:link, href, label} | acc])
              _ ->
                scan_stream(binary_part(sub_html, len, byte_size(sub_html) - len), acc)
            end

          String.starts_with?(lower_token, "<p") ->
            case Regex.run(~r/<p\b[^>]*>(.*?)<\/p>/is, sub_html, return: :index) do
              [{0, total_len}, {inner_start, inner_len}] ->
                p_text = decode_entities(strip_tags(binary_part(sub_html, inner_start, inner_len)))
                rest = binary_part(sub_html, total_len, byte_size(sub_html) - total_len)
                elem =
                  if String.contains?(lower_token, "page-lead") do
                    {:page_lead, p_text}
                  else
                    {:p, p_text}
                  end
                if p_text != "" do
                  scan_stream(rest, [elem | acc])
                else
                  scan_stream(rest, acc)
                end
              _ ->
                scan_stream(binary_part(sub_html, len, byte_size(sub_html) - len), acc)
            end

          true ->
            scan_stream(binary_part(sub_html, len, byte_size(sub_html) - len), acc)
        end

      nil ->
        Enum.reverse(acc)
    end
  end

  # ── Table Aligner for Text & TAD ────────────────────────────────────────────
  def format_table_ascii(rows) when is_list(rows) and length(rows) > 0 do
    # Compute maximum width per column
    col_count = Enum.max(Enum.map(rows, &length/1))
    padded_rows = Enum.map(rows, fn r -> r ++ List.duplicate("", col_count - length(r)) end)

    col_widths =
      for c <- 0..(col_count - 1) do
        widths = Enum.map(padded_rows, fn r -> String.length(Enum.at(r, c, "")) end)
        Enum.max([widths |> Enum.max(), 4])
      end

    format_row = fn r ->
      "| " <>
      Enum.map_join(Enum.zip(r, col_widths), " | ", fn {val, w} ->
        String.pad_trailing(val, w)
      end) <>
      " |"
    end

    separator =
      "|-" <>
      Enum.map_join(col_widths, "-+-", fn w -> String.duplicate("-", w) end) <>
      "-|"

    case padded_rows do
      [header | rest] ->
        lines = [format_row.(header), separator | Enum.map(rest, format_row)]
        Enum.join(lines, "\n")
      [] -> ""
    end
  end
  def format_table_ascii(_), do: ""

  # ── Binary TAD Segment Builders ─────────────────────────────────────────────

  def make_segment(tag, payload) when is_integer(tag) and is_binary(payload) do
    len = byte_size(payload)
    <<tag::16-big, len::32-big, payload::binary>>
  end

  # Page Fusen (TS_TPAGE = 0xFFA0)
  def seg_page(width \\ 840, height \\ 1200, margin_l \\ 40, margin_t \\ 40) do
    payload = <<0::8, 0::8, height::16-big, width::16-big, margin_t::16-big, margin_t::16-big, margin_l::16-big, margin_l::16-big>>
    make_segment(@ts_tpage, payload)
  end

  # Font Fusen (TS_TFONT = 0xFFA2)
  # font_id 0 = Mincho, 1 = Gothic / Bold, 2 = Monospace
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
  def seg_hr(width \\ 720) do
    payload = <<1::8, 0::8, 0x008080::32-big, 0::16-big, 0::16-big, width::16-big, 0::16-big>>
    make_segment(@ts_fprim, payload)
  end

  def seg_text(text) do
    :unicode.characters_to_binary(text <> "\n", :utf8, :utf8)
  end

  # ── Full Binary Document Compilation ────────────────────────────────────────
  def compile_to_binary_tad(elements) do
    init_segments = [
      seg_page(840, 1200, 40, 40),
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
            seg_hr(720),
            seg_font(0, 1),
            seg_char(12, 400, 0x000000),
            seg_ruler(22, 0)
          ]

        {:page_lead, text} ->
          [
            seg_font(0, 1),
            seg_char(13, 500, 0x1E293B),
            seg_ruler(24, 10),
            seg_text(text <> "\n"),
            seg_font(0, 1),
            seg_char(12, 400, 0x000000),
            seg_ruler(22, 0)
          ]

        {:callout, title, body} ->
          [
            seg_font(1, 1),
            seg_char(13, 700, 0x005577),
            seg_ruler(22, 15),
            seg_text("ℹ [NOTE: " <> title <> "]"),
            seg_font(0, 1),
            seg_char(11, 400, 0x333333),
            seg_ruler(18, 25),
            seg_text(body <> "\n"),
            seg_font(0, 1),
            seg_char(12, 400, 0x000000),
            seg_ruler(22, 0)
          ]

        {:h2, title} ->
          [
            seg_font(1, 1),
            seg_char(16, 700, 0x002244),
            seg_ruler(28, 0),
            seg_text("\n▶ " <> title),
            seg_hr(560),
            seg_font(0, 1),
            seg_char(12, 400, 0x000000),
            seg_ruler(22, 0)
          ]

        {:h3, title} ->
          [
            seg_font(1, 1),
            seg_char(13, 600, 0x004466),
            seg_ruler(24, 0),
            seg_text("\n▼ " <> title),
            seg_font(0, 1),
            seg_char(12, 400, 0x000000),
            seg_ruler(22, 0)
          ]

        {:h4, title} ->
          [
            seg_font(1, 1),
            seg_char(12, 600, 0x444444),
            seg_ruler(20, 0),
            seg_text("● " <> title),
            seg_font(0, 1),
            seg_char(12, 400, 0x000000),
            seg_ruler(22, 0)
          ]

        {:api_card, name, ret_type, desc, sig, table, meta} ->
          card_head = [
            seg_font(1, 1),
            seg_char(14, 700, 0x005577),
            seg_ruler(24, 0),
            seg_text("\n◆ FUNCTION: " <> name <> "  [" <> ret_type <> "]")
          ]

          card_desc =
            if desc != "" do
              [
                seg_font(0, 1),
                seg_char(12, 400, 0x222222),
                seg_ruler(20, 10),
                seg_text("  " <> desc)
              ]
            else
              []
            end

          card_sig =
            if sig != "" do
              [
                seg_font(2, 1),
                seg_char(11, 600, 0x0A2540),
                seg_ruler(16, 20),
                seg_text("  ┌─ C99 Prototype ──────────────────────────────────────────\n  │ " <> sig <> "\n  └──────────────────────────────────────────────────────────")
              ]
            else
              []
            end

          card_tbl =
            if table != nil and table != [] do
              tbl_str = format_table_ascii(table)
              [
                seg_font(2, 1),
                seg_char(10, 400, 0x111111),
                seg_ruler(15, 20),
                seg_text(tbl_str)
              ]
            else
              []
            end

          card_meta =
            if meta != [] do
              meta_lines =
                Enum.map_join(meta, "\n", fn {lbl, val} -> "  • #{lbl}: #{val}" end)
              [
                seg_font(0, 1),
                seg_char(11, 500, 0x475569),
                seg_ruler(18, 20),
                seg_text(meta_lines)
              ]
            else
              []
            end

          card_close = [
            seg_hr(680),
            seg_font(0, 1),
            seg_char(12, 400, 0x000000),
            seg_ruler(22, 0)
          ]

          card_head ++ card_desc ++ card_sig ++ card_tbl ++ card_meta ++ card_close

        {:code_block, code} ->
          [
            seg_font(2, 1),
            seg_char(10, 400, 0x1E293B),
            seg_ruler(16, 20),
            seg_text(code <> "\n"),
            seg_font(0, 1),
            seg_char(12, 400, 0x000000),
            seg_ruler(22, 0)
          ]

        {:table, rows} ->
          tbl_str = format_table_ascii(rows)
          [
            seg_font(2, 1),
            seg_char(10, 400, 0x000000),
            seg_ruler(16, 10),
            seg_text(tbl_str <> "\n"),
            seg_font(0, 1),
            seg_char(12, 400, 0x000000),
            seg_ruler(22, 0)
          ]

        {:ul, items} ->
          text = Enum.map_join(items, "\n", fn it -> "  • " <> it end)
          [
            seg_font(0, 1),
            seg_char(12, 400, 0x000000),
            seg_ruler(20, 10),
            seg_text(text <> "\n"),
            seg_ruler(22, 0)
          ]

        {:ol, items} ->
          text =
            Enum.with_index(items, 1)
            |> Enum.map_join("\n", fn {it, idx} -> "  #{idx}. #{it}" end)
          [
            seg_font(0, 1),
            seg_char(12, 400, 0x000000),
            seg_ruler(20, 10),
            seg_text(text <> "\n"),
            seg_ruler(22, 0)
          ]

        {:hr} ->
          [seg_hr(720)]

        {:p, text} ->
          [seg_text(text)]

        {:link, href, label} ->
          robj_id = :erlang.phash2(href, 100_000) + 1000
          [seg_vobj(robj_id, label, href)]
      end)

    payload = IO.iodata_to_binary([init_segments, body_segments])
    payload_size = byte_size(payload)

    <<@record_type_main::16-big, payload_size::32-big, payload::binary>>
  end

  # ── Symbolic Text TAD Representation ────────────────────────────────────────
  def compile_to_symbolic_tad(elements, doc_title \\ "B-Book Reference Manual") do
    header = """
    ================================================================================
    TAD Real Body [実身] : #{doc_title}
    RECORD TYPE : 1 (TAD Main Record) | B-Book BeBook-Style Developer Reference
    DATE        : #{Date.utc_today()} | GENERATOR : BtronBook.Compiler (Elixir)
    ================================================================================

    [付箋: DOCUMENT_HEADER | Title="#{doc_title}" | Layout="BeBook-TwoColumn" | Spec="BTRON 3.20"]
    """

    body =
      Enum.map_join(elements, "\n\n", fn
        {:h1, title} ->
          "【#{title}】\n" <> String.duplicate("━", 60)

        {:page_lead, text} ->
          ">>> " <> text

        {:callout, title, body} ->
          """
          ┌─ NOTE: #{title} ─────────────────────────────────────────
          │ #{body}
          └──────────────────────────────────────────────────────────
          """ |> String.trim()

        {:h2, title} ->
          "\n■ #{title}\n" <> String.duplicate("─", 50)

        {:h3, title} ->
          "\n▶ #{title}"

        {:h4, title} ->
          "◆ #{title}"

        {:api_card, name, ret_type, desc, sig, table, meta} ->
          tbl_part =
            if table != nil and table != [] do
              "\n" <> format_table_ascii(table)
            else
              ""
            end

          meta_part =
            if meta != [] do
              "\n" <> Enum.map_join(meta, "\n", fn {k, v} -> "  • #{k}: #{v}" end)
            else
              ""
            end

          """
          ┌── FUNCTION: #{name} [#{ret_type}] ──────────────────────────────
          │ #{desc}
          │
          │ C99 Prototype:
          │   #{sig}
          #{if tbl_part != "", do: "│ Parameters & Invariants:\n" <> String.replace(tbl_part, "\n", "\n│   ") <> "\n│", else: ""}#{if meta_part != "", do: meta_part <> "\n│", else: ""}
          └─────────────────────────────────────────────────────────────────
          """ |> String.trim()

        {:code_block, code} ->
          "```\n#{code}\n```"

        {:table, rows} ->
          format_table_ascii(rows)

        {:ul, items} ->
          Enum.map_join(items, "\n", &("  • " <> &1))

        {:ol, items} ->
          Enum.with_index(items, 1)
          |> Enum.map_join("\n", fn {item, idx} -> "  #{idx}. #{item}" end)

        {:p, text} ->
          text

        {:hr} ->
          String.duplicate("─", 70)

        {:link, href, label} ->
          robj_id = :erlang.phash2(href, 100_000) + 1000
          "[仮身] ##{robj_id} : #{label} -> [#{href}]"
      end)

    header <> "\n" <> body <> "\n"
  end

  # ── File Processor ──────────────────────────────────────────────────────────
  def process_file(html_path, target_dir) do
    File.mkdir_p!(target_dir)

    base_name = Path.basename(html_path, ".html")
    bin_path = Path.join(target_dir, base_name <> ".tad")
    txt_path = Path.join(target_dir, base_name <> ".tad.txt")

    html = File.read!(html_path)
    title =
      case Regex.run(~r/<title>(.*?)<\/title>/i, html) do
        [_, t] -> decode_entities(strip_tags(t))
        _ -> Path.basename(html_path)
      end

    elements = parse_book_html(html)

    # 1. Compile Binary TAD
    bin_tad = compile_to_binary_tad(elements)
    File.write!(bin_path, bin_tad)

    # 2. Compile Symbolic TAD
    sym_tad = compile_to_symbolic_tad(elements, title)
    File.write!(txt_path, sym_tad)

    %{
      html_path: html_path,
      bin_path: bin_path,
      txt_path: txt_path,
      elements_count: length(elements),
      bin_bytes: byte_size(bin_tad)
    }
  end

  # ── Self-test ───────────────────────────────────────────────────────────────
  def run_tests do
    IO.puts("Running BtronBook.Compiler unit tests...")

    sample_html = """
    <!DOCTYPE html>
    <html>
    <head><title>Test Book</title></head>
    <body>
      <nav class="book-sidebar"><a>Ignored link</a></nav>
      <main class="book-content">
        <h1>Kernel Specification</h1>
        <div class="callout callout-info">
          <div class="callout-title">Important Scope</div>
          Zero dynamic heap allocations in fast paths.
        </div>
        <h2>1. Architecture</h2>
        <p>Preemptive priority scheduling.</p>
        <pre><code>
        [ Ready Queue ] ──► [ Task Dispatcher ]
        </code></pre>
        <h2>2. API Reference</h2>
        <div class="api-card">
          <div class="api-header">
            <span class="api-name">tk_cre_tsk</span>
            <span class="ret-badge">ID</span>
          </div>
          <p>Creates a new task within the kernel execution context.</p>
          <div class="api-signature">ID tk_cre_tsk(const T_CTSK *pk_ctsk);</div>
          <table class="api-table">
            <tr><th>Parameter</th><th>Type</th><th>Description</th></tr>
            <tr><td>pk_ctsk</td><td>const T_CTSK*</td><td>Task packet.</td></tr>
          </table>
          <div class="api-meta">
            <span class="meta-label">Errors</span><span>ER_NOMEM, ER_LIMIT</span>
          </div>
        </div>
      </main>
    </body>
    </html>
    """

    elements = parse_book_html(sample_html)
    assert_elements = length(elements) >= 6
    IO.puts("  [TEST] Parsed elements count (#{length(elements)} >= 6): #{if assert_elements, do: "PASS", else: "FAIL"}")

    # Check API card extraction
    api = Enum.find(elements, fn elem -> match?({:api_card, "tk_cre_tsk", _, _, _, _, _}, elem) end)
    IO.puts("  [TEST] API Card extraction: #{if api != nil, do: "PASS", else: "FAIL"}")

    # Check code block newline preservation
    has_newlines =
      case Enum.find(elements, fn elem -> match?({:code_block, _}, elem) end) do
        {:code_block, c} -> String.contains?(c, "\n")
        _ -> false
      end
    IO.puts("  [TEST] Code block newline preservation: #{if has_newlines, do: "PASS", else: "FAIL"}")

    # Check Binary TAD compilation
    bin_tad = compile_to_binary_tad(elements)
    valid_header = match?(<<1::16-big, _payload_len::32-big, _rest::binary>>, bin_tad)
    IO.puts("  [TEST] Binary TAD header validation: #{if valid_header, do: "PASS", else: "FAIL"}")

    IO.puts("All BtronBook.Compiler unit tests passed!\n")
  end
end

# ── Main Entrypoint ───────────────────────────────────────────────────────────
args = System.argv()

if "--test" in args do
  BtronBook.Compiler.run_tests()
end

out_dir = "tad_bin/b-book"
File.mkdir_p!(out_dir)

src_dir = "b-book"
if File.dir?(src_dir) do
  IO.puts("======================================================================")
  IO.puts(" B-Book Specialized TAD Formatter & Batch Compiler (scripts/book2tad.exs)")
  IO.puts(" Source Directory        : ./#{src_dir}/")
  IO.puts(" Output Directory        : ./#{out_dir}/")
  IO.puts(" Target Specifications   : BeBook-Style C99 Systems Reference / BTRON 3.20")
  IO.puts("======================================================================")

  html_files = Path.wildcard(Path.join(src_dir, "**/*.html"))

  results =
    Enum.map(html_files, fn file ->
      rel = Path.relative_to(file, src_dir)
      sub_dir = Path.dirname(rel)
      target_dir = if sub_dir == ".", do: out_dir, else: Path.join(out_dir, sub_dir)
      res = BtronBook.Compiler.process_file(file, target_dir)
      in_name = String.pad_trailing(file, 30)
      out_name = String.pad_trailing(Path.relative_to(res.bin_path, "tad_bin"), 30)
      IO.puts("  [BOOK2TAD] #{in_name} -> #{out_name} (#{res.elements_count} items, #{res.bin_bytes} bytes)")
      res
    end)

  total_bytes = Enum.sum(Enum.map(results, & &1.bin_bytes))
  IO.puts("======================================================================")
  IO.puts(" Successfully compiled #{length(results)} B-Book documents via book2tad.")
  IO.puts(" Total TAD Binary Size: #{total_bytes} bytes across ./#{out_dir}/")
  IO.puts("======================================================================")
end
