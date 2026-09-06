----------------------------------------------------------------------
--	sched.d		電子手帳 : データボックス定義
--
--	(C) Copyright 1997-99 by Personal Media Corporation
----------------------------------------------------------------------

	{% USER_DATA	0L	}	-- データタイプ＝64	固定
	{# 0x2007	0L 0L	}	-- データ番号		固定

.ALIGN = 4

#include <stddef.d>

----------------------------------------------------------------------
-- データ参照テーブル
--		(データ番号)と(オフセット) のペアのエントリとなっており、
--		データ番号によりデータを検索するため、エントリの順番は任意。
--		このテーブルは基本的に変更してはいけない。
----------------------------------------------------------------------

.BASE = 0L

	DATA_END/4		-- データ・エントリワード数

-- 標準パネルデータ定義
	SPNL+0		pnl_close
	SPNL+1		pnl_xclose
	SPNL+2		pnl_update
	SPNL+3		pnl_ersave
	SPNL+4		pnl_cupd
	SPNL+5		pnl_chk
	SPNL+6		pnl_nosptl
	SPNL+7		pnl_nosptt
	SPNL+8		pnl_wrongl
	SPNL+9		pnl_wrongt
	SPNL+10		pnl_clrsch
	SPNL+11		pnl_deladr
	SPNL+12		pnl_prpage

	SPNL+20		er_open
	SPNL+21		er_wind
	SPNL+22		er_menu
	SPNL+23		er_read
	SPNL+24		er_write
	SPNL+25		er_cut
	SPNL+26		er_move
	SPNL+27		er_paste
	SPNL+28		er_memory
	SPNL+29		er_wind_noe		-- er_wind no exit
	SPNL+30		er_srch
	SPNL+33		er_tmpfl
	SPNL+34		er_fsnreq
	SPNL+35		er_regist

--/ メニューデータ定義 /--
	MENU+0		sc_menu			-- メインメニュー
	MENU+1		pop_index		-- ポップアップメニュー

--/ システムメッセージ定義 /--
	SMSG+0		msg_load		-- 読み込み待ち
	SMSG+1		msg_save		-- 書き込み待ち
	SMSG+2		msg_tmpfl		-- 一時ファイル生成待ち

--/ その他のデータ定義 /--
	MISC+0		tx_traytl		-- トレーデータのタイトル
	MISC+1		wind_name
	MISC+60		fld_names		-- 検索用フィールド名のリスト

	MISC+70 	midashi
	MISC+71 	week_str

	MISC+110	img_pagepict_0
	MISC+111	img_pagepict_1

	MISC+230	wcal_def		-- カレンダー
	MISC+231	wsch_def		-- 予定表
	MISC+232	wadr_def		-- 住所録
	MISC+233	wsrc_def		-- 検索パネル定義

	MISC+255	local_db		-- ローカルデータ

.DATA_END:

----// アプリケーションライブラリのデータ定義 //------------------------------

.BASE = 0H
--------------------
.sw_cnfm:	MS_PARTS	{0+CU 0 88 24}		0L	OFFSET:L+4
		MC_STR "確認\0"
--------------------
.sw_cancel:	MS_PARTS	{0+CU 0 88 24}		0L	OFFSET:L+4
		MC_STR "取り消し\0"
--------------------
.sw_uend:	MS_PARTS	{0+CU 0 112 24}		0L	sws_uend:L
--------------------
.sw_xuend:	MS_PARTS+P_DISABLE	{0+CU 0 112 24}	0L	sws_uend:L
.sws_uend:	MC_STR "更新して終了\0"
--------------------
.sw_aend:	MS_PARTS	{0+CU 0 112 24}		0L	OFFSET:L+4
		MC_STR "廃棄して終了\0"
--------------------
.sw_sav:	MS_PARTS	{0+CU 0 88 24}		0L	OFFSET:L+4
		MC_STR "保存\0"
--------------------
.sw_upd:	MS_PARTS	{0+CU 0 88 24}		0L	OFFSET:L+4
		MC_STR "更新\0"
--------------------
.sw_do: 	MS_PARTS	{0+CU 0 88 24}		0L	OFFSET:L+4
		MC_STR "実行\0"
--------------------
.sw_p1page:	MS_PARTS	{0+CU 0 196 24} 	0L	OFFSET:L+4
		MC_STR "表示されている１枚を印刷\0"
--------------------
.sw_pallpage:	MS_PARTS	{0+CU 0 96 24}		0L	OFFSET:L+4
		MC_STR "全部を印刷\0"
----------------------------------------------------------------------
-- 標準パネル定義
--	(項目指定  項目オフセット) のペアで定義する。
--	最大１２項目まで
--	内部番号により処理との対応をとるため、項目の配置は任意に変更可能。
--	項目の定義は、左から右、上から下への順で定義する。

.BASE = 0L

.err_dmy:	"\0"

.str_open:	"読み込みができませんので終了します。\0"
.er_open:	TEXT+pMID+pERR				str_open
		TEXT+pMID+pINE				err_dmy
		PART+pMID+pDEF+pEND			sw_cnfm
--------------------
.str_wind:	"ウィンドウが開けませんので終了します。\0"
.er_wind:	TEXT+pMID+pERR				str_wind
		TEXT+pMID+pINE				err_dmy
		PART+pMID+pDEF+pEND			sw_cnfm
--------------------
.str_wind_noe:	"ウィンドウが開けません。\0"
.er_wind_noe:	TEXT+pMID+pERR				str_wind_noe
		TEXT+pMID+pINE				err_dmy
		PART+pMID+pDEF+pEND			sw_cnfm
--------------------
.str_menu:	"メニューが登録できませんので終了します。\0"
.er_menu:	TEXT+pMID+pERR				str_menu
		TEXT+pMID+pINE				err_dmy
		PART+pMID+pDEF+pEND			sw_cnfm
--------------------
.str_read:	"読み込み中に障害が発生しましたので中断します。\0"
.er_read:	TEXT+pMID+pERR				str_read
		TEXT+pMID+pINE				err_dmy
		PART+pMID+pDEF+pEND			sw_cnfm
--------------------
.str_write:	"書き込み中に障害が発生しましたので中断します。\0"
.er_write:	TEXT+pMID+pERR				str_write
		TEXT+pMID+pINE				err_dmy
		PART+pMID+pDEF+pEND			sw_cnfm
--------------------
.str_memory:	"メモリが不足しましたので処理を中断します。\0"
.er_memory:	TEXT+pMID+pERR				str_memory
		PART+pMID+pDEF+pEND			sw_cnfm
--------------------
.str_cut:	"トレーへの複写／移動ができません。\0"
.er_cut:	TEXT+pMID+pERR				str_cut
		TEXT+pMID+pINE				err_dmy
		PART+pMID+pDEF+pEND			sw_cnfm
--------------------
.str_move:	"そのウィンドウへの複写／移動はできません。\0"
.er_move:	TEXT+pMID+pERR				str_move
		PART+pMID+pDEF+pEND			sw_cnfm
--------------------
.str_paste:	"トレーからの複写／移動ができません。\0"
.er_paste:	TEXT+pMID+pERR				str_paste
		TEXT+pMID+pINE				err_dmy
		PART+pMID+pDEF+pEND			sw_cnfm
--------------------
.str_srch:	"検索文字列は見つかりません。\0"
.er_srch:	TEXT+pERR				str_srch
		PART+pMID+pDEF+pEND			sw_cnfm
--------------------
.str_tmpfl:	"印刷データが生成できませんので終了します。\0"
.er_tmpfl:	TEXT+pMID+pERR				str_tmpfl
		TEXT+pMID+pINE				err_dmy
		PART+pMID+pDEF+pEND			sw_cnfm
--------------------
.str_fsnreq:	"付せんからの起動はできません。\0"
.er_fsnreq:	TEXT+pERR				str_fsnreq
		PART+pMID+pDEF+pEND			sw_cnfm
--------------------
.str_close:	"現在の内容は元の内容と異なっていますがどうしますか？\0"
.pnl_close:	TEXT					str_close
		PART+pNXT+pIN0				sw_cancel
		PART+pMID+pNXT+pIN1			sw_aend
		PART+pRIT+pDEF+pEND+pIN2		sw_uend
.pnl_xclose:	TEXT					str_close
		PART+pNXT+pDEF+pIN0			sw_cancel
		PART+pMID+pNXT+pIN1			sw_aend
		PART+pRIT+pEND+pIN2			sw_xuend
--------------------
.str_update:	"元の内容を廃棄して現在の内容により更新しますか？\0"
.pnl_update:	TEXT					str_update
		PART+pNXT+pIN0				sw_cancel
		PART+pRIT+pDEF+pEND+pIN1		sw_upd
--------------------
.str_ersave:	"元の実身の更新は禁止されています。\0"
.str_ersave1:	"新しい実身へ保存しますか？\0"
.pnl_ersave:	TEXT					str_ersave
		TEXT					str_ersave1
		PART+pNXT+pIN0				sw_cancel
		PART+pRIT+pDEF+pEND+pIN1		sw_sav
--------------------
.str_cupd:	"元の内容は他のアプリケーションにより変更されています。\0"
.str_cupd0:	"変更すると他からの変更内容は廃棄されますがよいですか？\0"
.pnl_cupd:	TEXT					str_cupd
		TEXT					str_cupd0
		PART+pNXT+pIN0				sw_cancel
		PART+pRIT+pDEF+pEND+pIN1		sw_upd
--------------------
.str_chk:	"現在の内容で再度更新しますか？\0"
.pnl_chk:	TEXT					str_cupd
		TEXT					str_chk
		PART+pNXT+pIN0				sw_cancel
		PART+pMID+pNXT+pIN1			sw_aend
		PART+pRIT+pDEF+pEND+pIN2		sw_uend
--------------------
.str_clrsch:	"予定を空欄にします。よろしいですか？\0"
.pnl_clrsch:	TEXT					str_clrsch
		PART+pNXT+pIN0				sw_cancel
		PART+pRIT+pDEF+pEND+pIN1		sw_do
--------------------
.str_deladr:	"住所を削除します。よろしいですか？\0"
.pnl_deladr:	TEXT					str_deladr
		PART+pNXT+pIN0				sw_cancel
		PART+pRIT+pDEF+pEND+pIN1		sw_do
--------------------
.str_nospt:	"処理対象外／不正なデータがありましたので読み飛ばしました。\0"
.str_uns:	"（更新すると元の内容に含まれていた　\0"
.str_uns0:	"　読み飛ばしたデータは失われます。）\0"
.pnl_wrongl:
.pnl_nosptl:	TEXT+pERR				str_nospt
		TEXT+pMID				str_uns
		TEXT+pMID				str_uns0
		PART+pMID+pDEF+pEND			sw_cnfm
.pnl_wrongt:
.pnl_nosptt:	TEXT+pERR				str_nospt
		PART+pMID+pDEF+pEND			sw_cnfm
--------------------
.str_regist:	"データ登録中に障害が発生しました。\0"
.er_regist:	TEXT+pMID+pERR				str_regist
		TEXT+pMID+pINE				err_dmy
		PART+pMID+pDEF+pEND			sw_cnfm
----------------------------------------------------------------------
.str_prpage:	"処理を選択してください。\0"
.pnl_prpage:	TEXT+pMID				str_prpage
		PART+pNXT+pIN0				sw_cancel
		PART+pMID+pNXT+pIN1			sw_pallpage
		PART+pRIT+pDEF+pEND+pIN2		sw_p1page

----------------------------------------------------------------------
-- メニューデータ定義
--	項目の順番の変更、項目の削除の場合は、項目番号変換テーブルの内容も
--		変更する必要がある。
--	最大親項目数は１６個まで
--	キーメニューを設定する場合は、ローマ字入力にも対応するために、通常、
--		そのキーの「ひらかな」と「英小文字」の両方を設定する必要がある
--		シフトの場合は、「カタカナ」と「英大文字」となる。
----------------------------------------------------------------------
-- 表示属性オフセット：	０：デフォールト
		-- メインメニューの枠の幅／パターン番号	：Ｗ
		-- サブメニューの枠の幅／パターン番号	：Ｗ
		-- メインメニューの背景パターン番号	：Ｗ
		-- サブメニューの背景パターン番号	：Ｗ
		-- インジケータのパターン番号		：Ｗ
		-- メインメニューの項目文字の色		：Ｌ
		-- サブメニューの項目文字の色		：Ｌ
		-- サブメニューのキーマクロ文字の色	：Ｌ
----------------------------------------------------------------------
-- SCHED メニュー項目定義

.BASE = 0L

.sc_menu:	sc_mcnv			-- 変換テーブルオフセット
		0			-- 表示属性オフセット
		sc_item_0		-- 終了
		sc_item_1		-- 保存
		sc_item_2		-- 表示
		sc_item_3		-- 編集
		sc_item_4		-- ページ
		sc_item_5		-- 印刷
		MN_WINDOW		-- ウィンドウ
		MN_TOOL 		-- 小物
		0

.BASE = 0H

--------------------
.sc_item_0:	MC_STRKEY1	"Ｅ"		"終了"
		0
--------------------
.sc_item_1:	MC_STR				"保存"
		MC_STR				"新しい実身へ"
		MC_STRKEY1	"Ｓ"		"元の実身へ"
		0
--------------------
.sc_item_2:	MC_STR				"表示"
		MC_IND				"カレンダー"
		MC_IND				"予定表"
		MC_IND				"住所録"
		MC_LINE
		MC_IND				"検索パネル"
		0
--------------------
.sc_item_3:	MC_STR				"編集"
		MC_STRKEY1	"Ｃ"		"トレーへ複写"
		MC_STRKEY1	"Ｚ"		"トレーから複写"
		MC_STRKEY1	"Ｖ"		"トレーへ移動"
		MC_STRKEY1	"Ｘ"		"トレーから移動"
		MC_STR				"削除"
		-- MC_LINE
		-- MC_STRKEY1	"Ａ"		"すべて選択"
		0
--------------------
.sc_item_4:	MC_STR				"ページ"
		MC_STRKEY1	"１"		"先頭へ"
		MC_STRKEY1	"２"		"前へ"
		MC_STRKEY1	"３"		"次へ"
		MC_STRKEY1	"４"		"最後へ"
		MC_LINE
		MC_STRKEY1	"Ｎ"		"ページを追加"
		MC_STR				"ページを削除"
		MC_STR				"空欄にする"
		0
--------------------
.sc_item_5:	MC_STR				"印刷"
		MC_STR				"用紙設定"
		MC_STRKEY1	"Ｐ"		"印刷"
		0
--------------------

-- SCHED メニュー項目番号変換テーブル

.BASE = 0L

--	実番号	内部番号	パラメータ
.sc_mcnv:
	0x001	0		0		-- 終了
						-- 保存
	0x101	1		0		--   新しい実身へ
	0x102	1		1		--   元の実身へ
						-- 表示
	0x201	2		1		--   カレンダー
	0x202	2		2		--   予定表
	0x203	2		4		--   住所録
	--204					--   ………
	0x205	2		8		--   検索パネル
						-- 編集
	0x301	3		0		--   トレーへ複写
	0x302	4		0		--   トレーから複写
	0x303	3		1		--   トレーへ移動
	0x304	4		1		--   トレーから移動
	0x305	5		0		--   削除
						-- ページ
	0x401	6		0		--   最初のページ
	0x402	6		1		--   前のページ
	0x403	7		2		--   次のページ
	0x404	7		3		--   最後のページ
	--405					--   ………
	0x406	8		0		--   ページを追加
	0x407	9		0		--   ページを削除
	0x408	10		0		--   空欄にする
						-- 印刷
	0x501	11		0		--   用紙設定
	0x502	12		0		--   印刷
	0					-- テーブル終了

----------------------------------------------------------------------
-- 住所録インデックスポップアップセレクタ

.BASE = 0H

.lm	=	24		-- 左マージン
.tm	=	6		-- トップマージン
.ih	=	16+1		-- 項目文字高さ
.iw	=	16		-- 項目文字幅
.wid	=	2		-- 最大幅（文字数)
.nitem	=	11		-- 項目数

.pop_index:	cnv_index:L
		1L				-- frame
		0L				-- bgpat
		0L				-- indpat
		-1L				-- chcol
		{CU+0  0  wid*iw+32, nitem*ih+tm+tm:} -- area
		0L				-- inact
		0L				-- select
		0L				-- desc
		0L				-- dnum
		item_index:L			-- ptr
		nitem:L				-- nitem
		{CU+lm	0*ih+tm		wid*iw+lm	1*ih+tm	}
		{CU+lm	1*ih+tm		wid*iw+lm	2*ih+tm	}
		{CU+lm	2*ih+tm		wid*iw+lm	3*ih+tm	}
		{CU+lm	3*ih+tm		wid*iw+lm	4*ih+tm	}
		{CU+lm	4*ih+tm		wid*iw+lm	5*ih+tm	}
		{CU+lm	5*ih+tm		wid*iw+lm	6*ih+tm	}
		{CU+lm	6*ih+tm		wid*iw+lm	7*ih+tm	}
		{CU+lm	7*ih+tm		wid*iw+lm	8*ih+tm	}
		{CU+lm	8*ih+tm		wid*iw+lm	9*ih+tm	}
		{CU+lm	9*ih+tm		wid*iw+lm	10*ih+tm}
		{CU+lm	10*ih+tm	wid*iw+lm	11*ih+tm}

.item_index:	MC_IND	"あ"
		MC_IND	"か"
		MC_IND	"さ"
		MC_IND	"た"
		MC_IND	"な"
		MC_IND	"は"
		MC_IND	"ま"
		MC_IND	"や"
		MC_IND	"ら"
		MC_IND	"わ"
		MC_IND	"他"
		0

.BASE = 0L
--		実番号	内部番号	パラメータ
.cnv_index:	1	1		0  -- "あ"
		2	1		1  -- "か"
		3	1		2  -- "さ"
		4	1		3  -- "た"
		5	1		4  -- "な"
		6	1		5  -- "は"
		7	1		6  -- "ま"
		8	1		7  -- "や"
		9	1		8  -- "ら"
		10	1		9  -- "わ"
		11	1		10 -- "他"
		0

----------------------------------------------------------------------
-- システムメッセージパネル用メッセージ
----------------------------------------------------------------------
.BASE = 0H

.msg_load:	"読み込み中です。しばらくお待ちください。\0"
.msg_save:	"書き込み中です。しばらくお待ちください。\0"
.msg_tmpfl:	"印刷データを作成中です。しばらくお待ちください。\0"

----------------------------------------------------------------------
-- その他のデータ
----------------------------------------------------------------------
.BASE = 0H

.tx_traytl:	"文章データ\0"		-- トレーデータのタイトル

.wind_name:	"（カレンダー）\0"	-- ウィンドウ名
		"（予定表）\0"
		"（住所録）\0"
		"検索パネル\0"
		0

----------------------------------------------------------------------
-- 固定イメージデータ

.ALIGN = 4
.BASE = 0L

.IMG_BIAS	=	32+6

--			データポインタ
.img_pagepict_0:	IMG_BIAS+sym_pagepict_0
.img_pagepict_1:	IMG_BIAS+sym_pagepict_1

----------------------------------------------------------------------
-- ローカルイメージデータ番号の定義
.SYM_PAGEPICT	=	110		-- ページ変更用ピクトグラムスイッチ

----------------------------------------------------------------------
-- カレンダー
.BASE = 0L

.wcal_def:	2			-- パーツ数
		21			-- パーツまでのオフセット
		{217 134}		-- ウィンドウサイズ
		{6			-- ページを表わす多角形：頂点数
		   4   4		-- 頂点の座標
		   4 101
		 207 101
		 207  22
		 142  22
		 142   4 }
		{12 24 26 11}		-- 日付の位置、枠サイズ
		pm_before1		--
		pm_next1		--
		0

----------------------------------------------------------------------
.BASE = 0H
-- ピクトグラムモーメンタリスイッチ
.pm_before1:	PM_PARTS+P_DISP+P_NOFRAME:L
		{CU+144   4  176  20}
		0L  SYM_PAGEPICT+0L   0L 0L
--
.pm_next1:	PM_PARTS+P_DISP+P_NOFRAME:L
		{CU+176   4  208  20}
		0L  SYM_PAGEPICT+1L   0L 0L

----------------------------------------------------------------------
-- 予定表
.BASE = 0L

.wsch_def:	8			-- パーツ数
		17			-- パーツまでのオフセット
		{302 192}		-- ウィンドウサイズ
		{6			-- ページを表わす多角形：頂点数
		   4   4		-- 頂点の座標
		   4 159
		 290 159
		 290  26
		 220  26
		 220   4 }
		pm_before2		--
		pm_next2		--
		tb_sch1			-- テキストボックス
		tb_sch2			-- テキストボックス
		tb_sch3			-- テキストボックス
		tb_sch4			-- テキストボックス
		tb_sch5			-- テキストボックス
		tb_sch6			-- テキストボックス
		0

.BASE = 0H

.P_DA = 0x8000	-- PARTDISP 指定

.tb_sch1:	TB_PARTS+P_DPNF+P_DA:L	{CU+8	 32  284   52}	 20+1L	 0L
		0L 1L 0x10000000L 7L
.tb_sch2:	TB_PARTS+P_DPNF+P_DA:L	{CU+8	 53  284   73}	 20+1L	 0L
		0L 1L 0x10000000L 7L
.tb_sch3:	TB_PARTS+P_DPNF+P_DA:L	{CU+8	 74  284   94}	 20+1L	 0L
		0L 1L 0x10000000L 7L
.tb_sch4:	TB_PARTS+P_DPNF+P_DA:L	{CU+8	 95  284  115}	 20+1L	 0L
		0L 1L 0x10000000L 7L
.tb_sch5:	TB_PARTS+P_DPNF+P_DA:L	{CU+8	116  284  136}	 20+1L	 0L
		0L 1L 0x10000000L 7L
.tb_sch6:	TB_PARTS+P_DPNF+P_DA:L	{CU+8	137  284  157}	 20+1L	 0L
		0L 1L 0x10000000L 7L
----------------------------------------------------------------------
-- ピクトグラムモーメンタリスイッチ
.pm_before2:	PM_PARTS+P_DISP+P_NOFRAME:L
		{CU+228   4  260  20}
		0L  SYM_PAGEPICT+0L   0L 0L
--
.pm_next2:	PM_PARTS+P_DISP+P_NOFRAME:L
		{CU+260   4  292  20}
		0L  SYM_PAGEPICT+1L   0L 0L
----------------------------------------------------------------------
-- 住所録
.BASE = 0L

.wadr_def:	12			-- パーツ数
		23			-- パーツまでのオフセット
		{354 236}		-- ウィンドウサイズ
		{6			-- ページを表わす多角形：頂点数
		   4   4		-- 頂点の座標
		   4 202
		 340 202
		 340  26
		 276  26
		 276   4 }
		{244 176 268 200}	-- インデックス表示矩形
		{272 198}		-- ページ表示開始位置
		pm_before3		--
		pm_next3		--
		tb_yomi			-- テキストボックス
		tb_postno		-- テキストボックス
		tb_adr1			-- テキストボックス
		tb_adr2			-- テキストボックス
		tb_adr3			-- テキストボックス
		tb_name			-- テキストボックス
		tb_tel			-- テキストボックス
		tb_fax			-- テキストボックス
		tb_memo1		-- テキストボックス
		tb_memo2		-- テキストボックス
		tl_yomi			-- 文字列
		tl_postno		-- 文字列
		tl_adr1			-- 文字列
		tl_adr2			-- 文字列
		tl_adr3			-- 文字列
		tl_name			-- 文字列
		tl_tel			-- 文字列
		tl_fax			-- 文字列
		tl_memo1		-- 文字列
		tl_memo2		-- 文字列
		0

.BASE = 0H

.tb_yomi:	TB_PARTS+P_DPNF+P_DA:L	{CU+162   8  274   28}	  8+1L	 0L
		0L 1L 0x10000000L 7L
.tb_postno:	TB_PARTS+P_DPNF+P_DA:L	{CU+20	  8  132   28}	  8+1L	 0L
		0L 1L 0x10000000L 7L
.tb_adr1:	TB_PARTS+P_DPNF+P_DA:L	{CU+60	 29  336   49}	 20+1L	 0L
		0L 1L 0x10000000L 7L
.tb_adr2:	TB_PARTS+P_DPNF+P_DA:L	{CU+60	 50  336   70}	 20+1L	 0L
		0L 1L 0x10000000L 7L
.tb_adr3:	TB_PARTS+P_DPNF+P_DA:L	{CU+60	 71  336   91}	 20+1L	 0L
		0L 1L 0x10000000L 7L
.tb_name:	TB_PARTS+P_DPNF+P_DA:L	{CU+60	 92  336  112}	 20+1L	 0L
		0L 1L 0x10000000L 7L
.tb_tel:	TB_PARTS+P_DPNF+P_DA:L	{CU+60	113  336  133}	 20+1L	 0L
		0L 1L 0x10000000L 7L
.tb_fax:	TB_PARTS+P_DPNF+P_DA:L	{CU+60	134  336  154}	 20+1L	 0L
		0L 1L 0x10000000L 7L
.tb_memo1:	TB_PARTS+P_DPNF+P_DA:L	{CU+60	155  336  175}	 20+1L	 0L
		0L 1L 0x10000000L 7L
.tb_memo2:	TB_PARTS+P_DPNF+P_DA:L	{CU+60	176  230  196}	 12+1L	 0L
		0L 1L 0x10000000L 7L

.BASE = 0L

.tl_yomi:	{CU+134 28-5}	"よみ\0"
.tl_postno:	{CU+8	28-5}	"〒\0"
.tl_adr1:	{CU+8	49-5}	"住所１\0"
.tl_adr2:	{CU+8	70-5}	"住所２\0"
.tl_adr3:	{CU+8	91-5}	"住所３\0"
.tl_name:	{CU+8  112-5}	"氏名　\0"
.tl_tel:	{CU+8  133-5}	"電話　\0"
.tl_fax:	{CU+8  154-5}	"ＦＡＸ\0"
.tl_memo1:	{CU+8  175-5}	"メモ１\0"
.tl_memo2:	{CU+8  196-5}	"メモ２\0"

----------------------------------------------------------------------
-- ピクトグラムモーメンタリスイッチ
.BASE = 0H

.pm_before3:	PM_PARTS+P_DISP+P_NOFRAME:L
		{CU+280   4  312  20}
		0L  SYM_PAGEPICT+0L   0L 0L
--
.pm_next3:	PM_PARTS+P_DISP+P_NOFRAME:L
		{CU+312   4  344  20}
		0L  SYM_PAGEPICT+1L   0L 0L
----------------------------------------------------------------------
-- 検索パネル定義：
.BASE = 0L

.wsrc_def:	3			-- パーツ数
		4			-- パーツまでのオフセット
		{254   82}		-- ウィンドウサイズ
		sw_srch1		--
		sw_srch2		--
		tb_srch			-- テキストボックス
		tl_srch			-- 文字列＃０
		0
.BASE = 0H
.tb_srch:	TB_PARTS+P_DPAP:L	{CU+46	  4  240  24}	20+1L	0L
.sw_srch1:	MS_PARTS:L		{CU+8	 30  126  50}	0L OFFSET:L+4
		MC_ATTR -1L -1 0 12 12 "最初のページから" 0
.sw_srch2:	MS_PARTS:L		{CU+134  30  240  50}	0L OFFSET:L+4
		MC_ATTR -1L -1 0 12 12 "次のページから" 0

.BASE = 0L

.tl_srch:	{CU+8  24-5}		"検索：\0"

----------------------------------------------------------------------
-- 検索用フィールド名のリスト
.BASE = 0L

.fld_names:	fld_yomi
		fld_postno
		fld_yubin
		fld_yubinno
		fld_adr
		fld_adr1
		fld_adr2
		fld_adr3
		fld_name
		fld_namae
		fld_denwa
		fld_tel
		fld_fax
		fld_memo
		fld_memo1
		fld_memo2
		0

-- ※※ フィールド名は、かなはカタカナ、英字は大文字に統一すること。 ※※
--		マスク	フィールド名
.fld_yomi:	0x0001	"ヨミ\0"
.fld_postno:	0x0002	"〒\0"
.fld_yubin:	0x0002	"郵便\0"
.fld_yubinno:	0x0002	"郵便番号\0"
.fld_adr:	0x001c	"住所\0"
.fld_adr1:	0x0004	"住所１\0"
.fld_adr2:	0x0008	"住所２\0"
.fld_adr3:	0x0010	"住所３\0"
.fld_name:	0x0020	"氏名\0"
.fld_namae:	0x0020	"名前\0"
.fld_denwa:	0x0040	"電話\0"
.fld_tel:	0x0040	"ＴＥＬ\0"
.fld_fax:	0x0080	"ＦＡＸ\0"
.fld_memo:	0x0300	"メモ\0"
.fld_memo1:	0x0100	"メモ１\0"
.fld_memo2:	0x0200	"メモ２\0"

----------------------------------------------------------------------
.midashi:	"あかさたなはまやらわ他\0"
.week_str:	"　日　月　火　水　木　金　土\0"
----------------------------------------------------------------------
-- ローカルデータボックスに登録するデータ

.BASE = 0L
.local_db:	sym_pagepict_0
		sym_pagepict_1
		0

.BASE = 0H
.sym_pagepict_0:	BMAP_DATA:L	SYM_PAGEPICT+0L
--	24+512L		$CBMP(0L 1L 0x0808 32 {0 0 32 16}
	24+64L		$CBMP(0L 1L 0x0101 4 {0 0 32 16}
□□□□□□□□□□□□□□□□ □□□□□□□□□□□□□□□□
□□□□□□□□□□□□□□□□ □□□□□□□■■■□□□□□□
□□□□□□□□□□□□□□□□ □□□□■■■■■■□□□□□□
□□□□□□□□□□□□□□□□ □■■■■■■■■■□□□□□□
□□□□□□□□□□□□□□■■ ■■■■■■■■■■□□□□□□
□□□□□□□□□□□■■■■■ ■■■■■■■■■■□□□□□□
□□□□□□□□■■■■■■■■ ■■■■■■■■■■□□□□□□
□□□□□■■■■■■■■■■■ ■■■■■■■■■■□□□□□□
□□□□□■■■■■■■■■■■ ■■■■■■■■■■□□□□□□
□□□□□□□□■■■■■■■■ ■■■■■■■■■■□□□□□□
□□□□□□□□□□□■■■■■ ■■■■■■■■■■□□□□□□
□□□□□□□□□□□□□□■■ ■■■■■■■■■■□□□□□□
□□□□□□□□□□□□□□□□ □■■■■■■■■■□□□□□□
□□□□□□□□□□□□□□□□ □□□□■■■■■■□□□□□□
□□□□□□□□□□□□□□□□ □□□□□□□■■■□□□□□□
□□□□□□□□□□□□□□□□ □□□□□□□□□□□□□□□□)

.BASE = 0H
.sym_pagepict_1:	BMAP_DATA:L	SYM_PAGEPICT+1
--	24+512L		$CBMP(0L 1L 0x0808 32 {0 0 32 16}
	24+64L		$CBMP(0L 1L 0x0101 4 {0 0 32 16}
□□□□□□□□□□□□□□□□ □□□□□□□□□□□□□□□□
□□□□□□■■■□□□□□□□ □□□□□□□□□□□□□□□□
□□□□□□■■■■■■□□□□ □□□□□□□□□□□□□□□□
□□□□□□■■■■■■■■■□ □□□□□□□□□□□□□□□□
□□□□□□■■■■■■■■■■ ■■□□□□□□□□□□□□□□
□□□□□□■■■■■■■■■■ ■■■■■□□□□□□□□□□□
□□□□□□■■■■■■■■■■ ■■■■■■■■□□□□□□□□
□□□□□□■■■■■■■■■■ ■■■■■■■■■■■□□□□□
□□□□□□■■■■■■■■■■ ■■■■■■■■■■■□□□□□
□□□□□□■■■■■■■■■■ ■■■■■■■■□□□□□□□□
□□□□□□■■■■■■■■■■ ■■■■■□□□□□□□□□□□
□□□□□□■■■■■■■■■■ ■■□□□□□□□□□□□□□□
□□□□□□■■■■■■■■■□ □□□□□□□□□□□□□□□□
□□□□□□■■■■■■□□□□ □□□□□□□□□□□□□□□□
□□□□□□■■■□□□□□□□ □□□□□□□□□□□□□□□□
□□□□□□□□□□□□□□□□ □□□□□□□□□□□□□□□□)

----------------------------------------------------------------------
