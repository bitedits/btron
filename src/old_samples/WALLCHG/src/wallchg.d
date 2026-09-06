--
--	wallchg_jpn.d (壁紙変更/databox)
--
--	(C) Copyright 2001 by Personal Media Corporation.
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
	SPNL+50		epnl_system	-- システムに異常
	SPNL+51		epnl_execmsg	-- 起動方法不正
	SPNL+52		epnl_memory	-- メモリ不足
	SPNL+53		epnl_fread	-- 付箋固有データが読み込めない
	SPNL+54		epnl_fwrite	-- 付箋固有データが書き込めない
	SPNL+55		epnl_maintsk	-- 主幹部での障害
	SPNL+56		epnl_subtsk	-- subtask での障害
	SPNL+57		epnl_wallchg	-- 壁紙変更での障害
	SPNL+58		epnl_setup	-- 設定パネルでの障害
	SPNL+59		epnl_regfep	-- fep になれない
	SPNL+60		epnl_nostop	-- 前のが止められない

	-- その他のパネル
	SPNL+100	opnl_setup	-- 設定パネル

	-- メニュー

	-- システムメッセージパネル

	-- ウィンドウ定義

	-- その他
	MISC+50		global_nm	-- global 名
	MISC+51		bgs_path	-- $$BGSCREEN.BOX への絶対 PATH
--	MISC+255	local_db	-- local databox

#include	<libapp.i>

.DATA_END:


--------------------------------------------------------------- libapp 用の定義
#include	<libapp.d>


-------------------------------------------------------------- 共通定義(その他)
-- 特殊文字コードの定義
.BASE = 0H
.TK_TAB		= 0x0009
.TC_FDLM	= 0xff21		-- パスの区切り
.TNULL		= 0x0000
.TSC_SYS	= 0x0021		-- system script

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

.sw_exit:	MS_PARTS
		{0+CU	0	16*5+8	16+8}
		0L	OFFSET:L+4
		MC_STR	"終了\0"

.sw_set:	MS_PARTS
		{0+CU	0	16*5+8	16+8}
		0L	OFFSET:L+4
		MC_STR	"常駐\0"


------------------------------------------------------------ 通常のパネルの定義
.BASE = 0L


------------------------------------------------------------ エラーパネルの定義
.BASE = 0L
.str_system:	"異常が発生しましたので終了します。\0"
.epnl_system:	TEXT+pMID+pERR				str_system
		TEXT+pMID+pINE				err_dmy
		PART+pMID+pDEF+pEND			sw_cnfm

.str_execmsg:	"起動方法が正しくありません。\0"
.epnl_execmsg:	TEXT+pMID+pERR				str_execmsg
		PART+pMID+pDEF+pEND			sw_cnfm

.str_memory:	"メモリが不足しましたので処理を中断します。\0"
.epnl_memory:	TEXT+pMID+pERR				str_memory
		TEXT+pMID+pINE				err_dmy
		PART+pMID+pDEF+pEND			sw_cnfm

.str_fread:	"付箋固有データが読み込めません。\0"
.epnl_fread:	TEXT+pMID+pERR				str_fread
		TEXT+pMID+pINE				err_dmy
		PART+pMID+pDEF+pEND			sw_cnfm

.str_fwrite:	"付箋固有データが書き込めません。\0"
.epnl_fwrite:	TEXT+pMID+pERR				str_fwrite
		TEXT+pMID+pINE				err_dmy
		PART+pMID+pDEF+pEND			sw_cnfm

.str_maintsk:	"メインタスクで異常が発生しました。\0"
.epnl_maintsk:	TEXT+pMID+pERR				str_maintsk
		TEXT+pMID+pINE				err_dmy
		PART+pMID+pDEF+pEND			sw_cnfm

.str_subtsk:	"時間待ちサブタスクで異常が発生しました。\0"
.epnl_subtsk:	TEXT+pMID+pERR				str_subtsk
		TEXT+pMID+pINE				err_dmy
		PART+pMID+pDEF+pEND			sw_cnfm

.str_wallchg:	"壁紙の変更に失敗しました。\0"
.epnl_wallchg:	TEXT+pMID+pERR				str_wallchg
		TEXT+pMID+pINE				err_dmy
		PART+pMID+pDEF+pEND			sw_cnfm

.str_setup:	"設定パネルでの設定ができません。\0"
.epnl_setup:	TEXT+pMID+pERR				str_setup
		TEXT+pMID+pINE				err_dmy
		PART+pMID+pDEF+pEND			sw_cnfm

.str_regfep:	"フロントエンドプロセスとして登録できません。\0"
.epnl_regfep:	TEXT+pMID+pERR				str_regfep
		TEXT+pMID+pINE				err_dmy
		PART+pMID+pDEF+pEND			sw_cnfm

.str_nostop:	"以前のプロセスが停止できませんので終了します。\0"
.epnl_nostop:	TEXT+pMID+pERR				str_nostop
		TEXT+pMID+pINE				err_dmy
		PART+pMID+pDEF+pEND			sw_cnfm


-------------------------------------------------------------------- 設定パネル
.BASE = 0H
.tit_setup:	"壁紙変更\0"
.str_setup_time:"待機時間：\0"
.str_setup_min:	"分\0"
.str_setup_sec:	"秒\0"

.sb_setup_min:	SB_PARTS
		{0+CU	0	16*2+10	16+14}
		OFFSET:L+12	1L	OFFSET:L+14
		MC_STR	0x0c02	0	59	0
		0	0
.sb_setup_sec:	SB_PARTS
		{0+CU	0	16*2+10	16+14}
		OFFSET:L+12	1L	OFFSET:L+14
		MC_STR	0x0c02	0	59	0
		0	0
.as_setup_pnl:	AS_PARTS
		{0+CU	0	16*8+8	16+8}
		1L	OFFSET:L+4
		MC_STR	"起動時のパネル\0"
.as_setup_chg:	AS_PARTS
		{0+CU	0	16*10+8	16+8}
		0L	OFFSET:L+4
		MC_STR	"起動時にも壁紙変更\0"

.BASE = 0L
.opnl_setup:	FTXT+pMID				tit_setup

		-- 待機時間
		Vpos					6
		FTXT+pFIL+pNXT				str_setup_time
		PART+pIN3+pNXT+pFIL			sb_setup_min
		Vpos					6
		FTXT+pFIL+pNXT				str_setup_min
		PART+pIN4+pNXT+pFIL			sb_setup_sec
		Vpos					6
		FTXT					str_setup_sec

		-- [起動時のパネル]
		PART+pIN5+pNXT				as_setup_pnl

		-- [起動時にも壁紙変更]
		PART+pIN6				as_setup_chg

		-- [終了][設定]
		PART+pNXT+pIN1				sw_exit
		PART+pRIT+pIN2+pDEF+pEND		sw_set

 
------------------------------------------------------------------ メニュー定義
--
-- メインメニュー
--
-- テーブル定義
.BASE = 0L
 
-- 項目リスト
.BASE = 0H

-- 項目番号変換テーブル
.BASE = 0L
--	実番号	内部番号	パラメータ


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
--	TC	tit[20];		// タイトル(20文字固定)
--} WINDEF;
.BASE = 0H

-- 主ウィンドウ内の文字列の定義
.BASE = 0H

-- 主ウィンドウ内のパーツの定義
.BASE = 0H

-- 主ウィンドウ内の各項目の定義
.BASE = 0H


------------------------------------------------------------------------ その他
-- global name
.BASE = 0H
.global_nm:	"ＷＡＬＬＣＨＧ"8

-- $$BGSCREEN.BOX への絶対 PATH
.BASE = 0H
.bgs_path:	TC_FDLM	"ＳＹＳ"
		TC_FDLM	"＄＄ＢＧＳＣＲＥＥＮ．ＢＯＸ"
		TNULL


----------------------------------------------------------------- local databox
