--
--	imgview.d (画像閲覧/databox)
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

	-- エラーパネル
	SPNL+100	epnl_system	-- システムに異常
	SPNL+101	epnl_execmsg	-- 起動方法不正
	SPNL+102	epnl_memory	-- メモリ不足
	SPNL+103	epnl_fread	-- 付箋固有データが読み込めない
	SPNL+104	epnl_fwrite	-- 付箋固有データが書き込めない
	SPNL+105	epnl_window	-- ウィンドウが開けられない
	SPNL+106	epnl_menu	-- メインメニューが開けられない
	SPNL+107	epnl_imgload	-- 画像が読み込めない
	SPNL+108	epnl_pushtray	-- トレーへ複写できない

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
		{0+CU	0	16*5+8	16+8}
		0L	OFFSET:L+4
		MC_STR	"確認\0"


------------------------------------------------------------ 通常のパネルの定義
.BASE = 0L


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
 
.str_imgload:	"画像が読み込めませんでした。\0"
.epnl_imgload:	TEXT+pMID+pERR			str_imgload
		TEXT+pMID+pINE			err_dmy
		PART+pMID+pDEF+pEND		sw_cnfm
 
.str_pushtray:	"トレーにデータが格納できません。\0"
.epnl_pushtray:	TEXT+pMID+pERR			str_pushtray
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
		item_3			-- 倍率
		MN_WINDOW		-- ウィンドウ
		MN_TOOL			-- 小物
		0
 
-- 項目リスト
.BASE = 0H
.item_0:	MC_STRKEY1	"Ｅ"	"終了"
		0
.item_1:	MC_STR			"表示"
		MC_INDKEY1	"Ｌ"	"全画面表示"
		MC_STR			"再表示"
		0
.item_2:	MC_STR			"編集"
		MC_STRKEY1	"Ｃ"	"トレーへ複写"
		0
.item_3:	MC_STR			"倍率"
		MC_STRKEY1	"Ｍ"	"標準"
		MC_STRKEY1	"Ｚ"	"拡大"
		MC_STRKEY1	"Ｒ"	"縮小"
		0

-- 項目番号変換テーブル
.BASE = 0L
--		実番号	内部	パラメータ
.mctab:		0x0001	0	0	-- [終了]
		0x0101	1	0	-- [表示]-[全画面表示]
		0x0102	2	0	--       -[再表示]
		0x0201	3	0	-- [編集]-[トレーへ複写]
		0x0301	4	1	-- [倍率]-[標準]
		0x0302	4	2	--       -[拡大]
		0x0303	4	3	--       -[縮小]
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
--	W	pnum;			// パーツ個数
--	TC	title[20];		// タイトル(20文字固定)
--} WINDEF;
.BASE = 0H
.def_mainwin:	WA_NORMAL+WA_WORK+WA_FMOVE
		{0	0	382	170}
		mainwin_parts:L
		0L
		"画像閲覧"20

-- 主ウィンドウ内の文字列のページの文字列の定義
.BASE = 0H

-- 主ウィンドウ内の文字列のページのパーツの定義
.BASE = 0H

-- 主ウィンドウ内の文字列のページの各項目の定義(パーツなどの配置の定義)
.BASE = 0H
.mainwin_parts:


------------------------------------------------------------------------ その他
-- トレー格納時の名称
.BASE = 0H
.tray_name:	"図形データ\0"12


----------------------------------------------------------------- local databox
.BASE = 0L
.local_db:	imgview_pict
		0

-- ピクトグラム
.BASE = 0H
.imgview_pict:	PICT_DATA:L
		100L
		imgview_pict_ed:L-imgview_pict_st:L

.imgview_pict_st:
--#include	"pict_imgview"

.imgview_pict_ed:
