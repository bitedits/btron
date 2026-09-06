--
--	parsamp.d (パーツ操作例/databox)
--
--	(C) Copyright 2002 by Personal Media Corporation.
--

	{% USER_DATA:L	0L}		-- データタイプ
	{# 0x2000L	0L	0L}	-- データ番号


---------------------------------------------------------------------- 共通定義
.BASE = 0L
.ALIGN = 4

#include	<stddef.d>


------------------------------------------------------------------ 参照テーブル
.BASE = 0L

	DATA_END/4			-- データ総数

	-- 通常のパネル
	SPNL+0		pnl_update	-- 更新終了確認パネル

	-- エラーパネル
	SPNL+100	epnl_system	-- システムに異常
	SPNL+101	epnl_execmsg	-- 起動方法不正
	SPNL+102	epnl_memory	-- メモリ不足
	SPNL+103	epnl_fread	-- 付箋固有データが読み込めない
	SPNL+104	epnl_fwrite	-- 付箋固有データが書き込めない
	SPNL+105	epnl_window	-- ウィンドウが開けられない
	SPNL+106	epnl_menu	-- メインメニューが開けられない
	SPNL+107	epnl_pushtray	-- トレーへ複写できない
	SPNL+108	epnl_poptray	-- トレーから複写/移動できない
	SPNL+109	epnl_paste	-- ウィンドウへ複写/移動できない
	SPNL+110	epnl_tmptray	-- 一時トレーに格納できない

	-- その他のパネル

	-- パネル内文字列

	-- メニュー
	MENU+0		menu_main	-- メインメニューの定義

	-- システムメッセージパネル

	-- ウィンドウ定義
	MISC+0		def_mainwin	-- 主ウィンドウ定義

	-- その他
	MISC+100	tray_name	-- トレー格納時の名称
--	MISC+255	local_db	-- local databox

#include	<libapp.i>

.DATA_END:


--------------------------------------------------------------- libapp 用の定義
#include	<libapp.d>


-------------------------------------------------------------- 共通定義(その他)
-- その他
.BASE = 0H
.CHSSTD		= 16			-- 基本文字サイズ(dot 単位)

-- 特殊文字コードの定義
.BASE = 0H
.TNULL		= 0x0000
.TC_TAB		= 0x0009
.TC_NL		= 0x000a
.TC_NC		= 0x000b
.TC_FF		= 0x000c
.TC_CR		= 0x000d
.TC_LANG	= 0xfe00
.TC_FDLM	= 0xff21		-- パスの区切り
.TC_SPEC	= 0xff00
.TC_ESC		= 0xff80

-- 言語/スクリプト指定の定義
.BASE = 0H
.TSC_SYS	= 0x0021		-- system script

-- TAD 付箋/セグメントの定義(include/tad.h より引用)
.BASE = 0H				-- 標準可変長セグメントID
.TS_INFO	= 0xffe0		-- 管理情報セグメント
.TS_TEXT	= 0xffe1		-- 文章開始セグメント
.TS_TEXTEND	= 0xffe2		-- 文章終了セグメント
.TS_FIG		= 0xffe3		-- 図形開始セグメント
.TS_FIGEND	= 0xffe4		-- 図形終了セグメント
.TS_IMAGE	= 0xffe5		-- 画像セグメント
.TS_VOBJ	= 0xffe6		-- 仮身セグメント
.TS_DFUSEN	= 0xffe7		-- 指定付箋セグメント
.TS_FFUSEN	= 0xffe8		-- 機能付箋セグメント
.TS_SFUSEN	= 0xffe9		-- 設定付箋セグメント

.BASE = 0H				-- 文章付箋セグメントID
.TS_TPAGE	= 0xffa0		-- 文章ページ割付け指定付箋
.TS_TRULER	= 0xffa1		-- 行書式指定付箋
.TS_TFONT	= 0xffa2		-- 文字指定付箋
.TS_TCHAR	= 0xffa3		-- 特殊文字指定付箋
.TS_TATTR	= 0xffa4		-- 文字割り付け指定付箋
.TS_TSTYLE	= 0xffa5		-- 文字修飾指定付箋
.TS_TVAR	= 0xffad		-- 変数参照指定付箋
.TS_TMEMO	= 0xffae		-- 文章メモ指定付箋
.TS_TAPPL	= 0xffaf		-- 文章アプリケーション指定付箋

.BASE = 0H				-- 図形付箋セグメントID
.TS_FPRIM	= 0xffb0		-- 図形要素セグメント
.TS_FDEF	= 0xffb1		-- データ定義セグメント
.TS_FGRP	= 0xffb2		-- グループ定義セグメント
.TS_FMAC	= 0xffb3		-- マクロ定義/参照セグメント
.TS_FATTR	= 0xffb4		-- 図形修飾セグメント
.TS_FPAGE	= 0xffb5		-- 図形ページ割り付け指定付箋
.TS_FMEMO	= 0xffbe		-- 図形メモ指定付箋
.TS_FAPPL	= 0xffbf		-- 図形アプリケーション指定付箋

-- include/btron/hmi.h より引用
.BASE = 0H
.MC_KEY		= 0x10f0
.MC_KEY1	= 0x1010		-- キーマクロ 1 個
.MC_KEY2	= 0x1020		-- キーマクロ 2 個
.MC_KEY3	= 0x1030		-- キーマクロ 3 個
.MC_KEY4	= 0x1040
.MC_KEY5	= 0x1050
.MC_KEY6	= 0x1060
.MC_KEY7	= 0x1070
.MC_KEY8	= 0x1080
.MC_KEY9	= 0x1090
.MC_KEY10	= 0x10a0
.MC_KEY11	= 0x10b0
.MC_KEY12	= 0x10c0
.MC_KEY13	= 0x10d0
.MC_KEY14	= 0x10e0		-- キーマクロ 14 個
.MC_KEY15	= 0x10f0		-- キーマクロ 15 個

-- hmi の window 属性の表記と互換
.BASE = 0L
.WA_FRONT	= 0x00000001		-- 通常／前面ウィンドウ    (０:通常)
.WA_SUBW	= 0x00000002		-- 主／従属ウィンドウ      (０:主)
.WA_SIZE	= 0x00000004		-- ドラッグ変形の可否      (０:否)
.WA_HHDL	= 0x00000008		-- ドラッグ左右変形の可否  (０:否)
.WA_VHDL	= 0x00000010		-- ドラッグ上下変形の可否  (０:否)
.WA_RBAR	= 0x00000020		-- 右スクロールバーの有無  (０:無)
.WA_BBAR	= 0x00000040		-- 下スクロールバーの有無  (０:無)
.WA_LBAR	= 0x00000080		-- 左スクロールバーの有無  (０:無)
.WA_TITL	= 0x00000100		-- タイトルバーの有無      (０:有)
 
.WA_NORMAL	= 0x0000007c		-- 通常(変形可、右/下スクロールバー)
.WA_FIXSIZE	= 0x00000000		-- 固定サイズ
 
.WA_BGDSP	= 0x00000200		-- 入力不可状態の時も表示を更新
.WA_FULL	= 0x00000400		-- 全面モード
.WA_SFULL	= 0x00000800		-- フルスクリーン
.WA_STD		= 0			-- 正規座標でウィンドウ枠を指定
.WA_WORK	= 0x00001000		-- 作業領域座標でウィンドウ枠を指定
.WA_FRAME	= 0x00002000		-- 外枠座標でウィンドウ枠を指定
.WA_FMOVE	= 0x00004000		-- オープン時、画面内に強制移動
.WA_GROUP	= 0x00010000		-- グループ主ウインドウ

-- PNL_ITEM 構造体表記と互換
.BASE = 0L
.NULL_ITEM	= 0			-- 空項目
.PTR_ITEM	= 1			-- ポインタイメージ
.PICT_ITEM	= 2			-- ピクトグラムイメージ
.PAT_ITEM	= 3			-- パターンイメージ
.BMAP_ITEM	= 4			-- ビットマップイメージ
.TEXT_ITEM	= 6			-- 文字列
.PARTS_ITEM	= 7			-- コントロールパーツ
.ACT_ITEM	= 0x80			-- 動作項目 (PD プレスの通知)
.ATR_ITEM	= 0x20			-- 属性付き文字列

-- パーツ属性
.BASE = 0L
.P_BLINK	= 0x2000		-- 反転(TB_PARTS系ではかな漢字変換抑止)


------------------------------------------------------------ パーツの定義(共通)
.BASE = 0H
.sw_cnfm:	MS_PARTS
		{0+CU	0	CHSSTD*5+8	CHSSTD+8}
		0L	OFFSET:L+4
		MC_STR	"確認"
		0

.sw_cancel:	MS_PARTS
		{0+CU	0	CHSSTD*5+8	CHSSTD+8}
		0L	OFFSET:L+4
		MC_STR	"取り消し"
		0

.sw_destroy:	MS_PARTS
		{0+CU	0	CHSSTD*7	CHSSTD+8}
		0L	OFFSET:L+4
		MC_STR	"廃棄して終了"
		0

.sw_update:	MS_PARTS
		{0+CU	0	CHSSTD*7	CHSSTD+8}
		0L	OFFSET:L+4
		MC_STR	"更新して終了"
		0


------------------------------------------------------------ 通常のパネルの定義
.BASE = 0L
.str_update:	"現在の内容は元の内容と異なっていますがどうしますか？\0"
.pnl_update:	TEXT				str_update
		PART+pIN0+pNXT			sw_cancel
		PART+pIN1+pMID+pNXT		sw_destroy
		PART+pIN2+pRIT+pDEF+pEND	sw_update


------------------------------------------------------------ エラーパネルの定義
.BASE = 0L
.str_system:	"異常が発生しましたので終了します。\0"
.epnl_system:	TEXT+pMID+pERR			str_system
		TEXT+pMID+pINE			err_dmy
		PART+pMID+pDEF+pEND		sw_cnfm

.str_execmsg:	"起動方法が正しくありません。\0"
.epnl_execmsg:	TEXT+pMID+pERR			str_execmsg
		PART+pMID+pDEF+pEND		sw_cnfm

.str_memory:	"メモリが不足しましたので処理を中断します。\0"
.epnl_memory:	TEXT+pMID+pERR			str_memory
		TEXT+pMID+pINE			err_dmy
		PART+pMID+pDEF+pEND		sw_cnfm

.str_fread:	"付箋固有データが読み込めません。\0"
.epnl_fread:	TEXT+pMID+pERR			str_fread
		TEXT+pMID+pINE			err_dmy
		PART+pMID+pDEF+pEND		sw_cnfm

.str_fwrite:	"付箋固有データが書き込めません。\0"
.epnl_fwrite:	TEXT+pMID+pERR			str_fwrite
		TEXT+pMID+pINE			err_dmy
		PART+pMID+pDEF+pEND		sw_cnfm

.str_window:	"ウィンドウが開けませんので終了します。\0"
.epnl_window:	TEXT+pMID+pERR			str_window
		TEXT+pMID+pINE			err_dmy
		PART+pMID+pDEF+pEND		sw_cnfm
 
.str_menu:	"メニューの登録ができませんので終了します。\0"
.epnl_menu:	TEXT+pMID+pERR			str_menu
		TEXT+pMID+pINE			err_dmy
		PART+pMID+pDEF+pEND		sw_cnfm
 
.str_pushtray:	"トレーにデータが格納できません。\0"
.epnl_pushtray:	TEXT+pMID+pERR			str_pushtray
		TEXT+pMID+pINE			err_dmy
		PART+pMID+pDEF+pEND		sw_cnfm

.str_poptray:	"トレーからの複写／移動ができません。\0"
.epnl_poptray:	TEXT+pMID+pERR			str_poptray
		TEXT+pMID+pINE			err_dmy
		PART+pMID+pDEF+pEND		sw_cnfm

.str_paste:	"そのウィンドウへの複写／移動はできません。\0"
.epnl_paste:	TEXT+pMID+pERR			str_paste
		TEXT+pMID+pINE			err_dmy
		PART+pMID+pDEF+pEND		sw_cnfm
 
.str_tmptray:	"一時トレーにデータが格納できません。\0"
.epnl_tmptray:	TEXT+pMID+pERR			str_tmptray
		TEXT+pMID+pINE			err_dmy
		PART+pMID+pDEF+pEND		sw_cnfm


---------------------------------------------------------------- パネル内文字列
.BASE = 0L

 
------------------------------------------------------------------ メニュー定義
--
-- メインメニュー
--
-- テーブル定義
.BASE = 0L
.menu_main:	mctab			-- 変換テーブル
		0
		item_0			-- 終了
		item_1			-- 表示
		item_2			-- 編集
		MN_WINDOW		-- ウィンドウ
		MN_TOOL			-- 小物
		0
 
-- 項目リスト
.BASE = 0H
.item_0:	MC_STRKEY1	"Ｅ"	"終了"
		0
.item_1:	MC_STR			"表示"
		MC_STR			"再表示"
		0
.item_2:	MC_STR			"編集"
		MC_STRKEY1	"Ｃ"	"トレーへ複写"
		MC_STRKEY1	"Ｚ"	"トレーから複写"
		MC_STRKEY1	"Ｖ"	"トレーへ移動"
		MC_STRKEY1	"Ｘ"	"トレーから移動"
		MC_STR			"削除"
		MC_LINE
		MC_STR			"初期化"
		0

-- 項目番号変換テーブル
.BASE = 0L
--		実番号	内部	パラメータ
.mctab:		0x0001	0	0	-- [終了]
		0x0101	1	0	-- [表示]-[再表示]
		0x0201	2	1	-- [編集]-[トレーへ複写]
		0x0202	3	1	--       -[トレーから複写]
		0x0203	2	2	--       -[トレーへ移動]
		0x0204	3	2	--       -[トレーから移動]
		0x0205	4	0	--       -[削除]
--		0x0206			--       -[……………]
		0x0207	5	0	--       -[初期化]
		0


------------------------------------------ システムメッセージパネル用文字列定義
.BASE = 0H


---------------------------------------------------------------- ウィンドウ定義
--
-- 主ウィンドウ定義
--

-- 主ウィンドウ定義(構造体)
--typedef struct {
--	UW	wattr;			// ウィンドウ属性
--	RECT	r;			// ウィンドウ枠
--	W	parts;			// パーツテーブル参照用
--	W	pnum;			// 項目個数
--	TC	title[20];		// タイトル(20文字固定)
--} WINDEF;
.BASE = 0H
.def_mainwin:	WA_FIXSIZE+WA_WORK+WA_FMOVE
		{10	50	366	238}
		mainwin_parts:L
		12L
		"パーツ操作"20

-- 主ウィンドウ内の文字列のページの文字列の定義
.BASE = 0H
.str_main_str:	"文字列：\0"
.str_main_color:"色：\0"
.str_main_size:	"大きさ：\0"
.str_main_pitch:"ピッチ：\0"

-- 主ウィンドウ内の文字列のページのパーツの定義
.BASE = 0H
.tb_main_str1:	TB_PARTS
		{0+CU	0	CHSSTD*10+8+8	CHSSTD+8}
		10+1L	OFFSET:L+4
		MC_STR	""
		0

.tb_main_str2:	TB_PARTS
		{0+CU	0	CHSSTD*10+8+8	CHSSTD+8}
		10+1L	OFFSET:L+4
		MC_STR	""
		0

.as_main_enable:AS_PARTS
		{0+CU	0	CHSSTD*3+8	CHSSTD+8}
		1L	OFFSET:L+4
		MC_STR	"有効"
		0

.ss_main_color:	SS_PARTS
		{0+CU	0	CHSSTD*4+8	CHSSTD+1+*3+8}
		1L	OFFSET:L+4
		MC_STR	"黒"		MC_STR	"青"
		MC_STR	"赤"		MC_STR	"紫"
		MC_STR	"緑"		MC_STR	"水色"
		MC_STR	"黄色"		MC_STR	"白"
		0

.vl_main_size:	VL_PARTS+P_HALIGN
		{0+CU	0	CHSSTD*12+8	CHSSTD+4}
		16L	15L	256L	0L
		0L	0L

.sb_main_size:	SB_PARTS
		{0+CU	0	CHSSTD*3+8	CHSSTD+14}
		OFFSET:L+12	1L	OFFSET:L+14
		MC_STR	0x0c03	0	255	0
		16	0

.ws_main_pitch:	WS_PARTS
		{0+CU	0	CHSSTD*4+8	CHSSTD+4*2+8}
		1L	OFFSET:L+4
		MC_STR	"固定"		MC_STR	"比例"
		0

.ms_main_init:	MS_PARTS
		{0+CU	0	CHSSTD*5+8	CHSSTD+8}
		1L	OFFSET:L+4
		MC_STR	"初期化"
		0

-- 主ウィンドウ内の文字列のページの各項目の定義(パーツなどの配置の定義)
.BASE = 0H
.mainwin_parts:
		-- 文字列
		TEXT_ITEM:L	0L
		{8+CU	8	0	0}
		0L	0L	str_main_str:L

		PARTS_ITEM:L	0L
		{8+CU	CHSSTD+2+8	0	0}
		0L	0L	tb_main_str1:L

		PARTS_ITEM:L	0L
		{8+CU	CHSSTD*2+8+8+2+8	0	0}
		0L	0L	tb_main_str2:L

		PARTS_ITEM:L	0L
		{CHSSTD*12+8+4+8+CU	CHSSTD*2+8+8+2+8	0	0}
		0L	0L	as_main_enable:L

		-- 色
		TEXT_ITEM:L	0L
		{CHSSTD*15+8+8+8+4+8+CU	8	0	0}
		0L	0L	str_main_color:L

		PARTS_ITEM:L	0L
		{CHSSTD*15+8+8+8+4+8+CU	CHSSTD+2+8	0	0}
		0L	0L	ss_main_color:L

		-- サイズ
		TEXT_ITEM:L	0L
		{8+CU	CHSSTD*3+8+4+8+8+2+8	0	0}
		0L	0L	str_main_size:L

		PARTS_ITEM:L	0L
		{8+CU	CHSSTD*4+8+4+8+8+8+2+2+4	0	0}
		0L	0L	vl_main_size:L

		PARTS_ITEM:L	0L
		{CHSSTD*12+8+4+8+CU	CHSSTD*4+8+4+8+8+2+2+8	0	0}
		0L	0L	sb_main_size:L

		-- ピッチ
		TEXT_ITEM:L	0L
		{CHSSTD*15+8+8+8+4+8+CU	CHSSTD*3+8+4+8+8+2+8	0	0}
		0L	0L	str_main_pitch:L

		PARTS_ITEM:L	0L
		{CHSSTD*15+8+8+8+4+8+CU	CHSSTD*4+8+4+8+8+2+2+8	0	0}
		0L	0L	ws_main_pitch:L

		-- 雑多
		PARTS_ITEM:L	0L
		{123+8+CU	CHSSTD*6+8+4+8+4+8+8+8+2+2+8	0	0}
		0L	0L	ms_main_init:L

------------------------------------------------------------------------ その他
-- トレー格納時の名称
.BASE = 0H
.tray_name:	"文章データ\0"12


----------------------------------------------------------------- local databox
.BASE = 0L
.local_db:	parsamp_pict
		0

-- ピクトグラム
.BASE = 0H
.parsamp_pict:	PICT_DATA:L
		100L
		parsamp_pict_ed:L-parsamp_pict_st:L

.parsamp_pict_st:
--#include	"pict_parsamp"

.parsamp_pict_ed:
