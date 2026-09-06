//
//	dbox.h (偽仮身一覧/databox 参照番号)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#ifndef	_I_DLED_DBOX_H
#define	_I_DLED_DBOX_H


// ------------------------------------------------------------- namespace DBOX
namespace	DBOX {
	// 共通定義
	enum COMMON {
		DB_SPNL = 0x8000,	// 単純パネルデータ番号
		DB_PSTR = 0x9000,	// パネル文字列バッファデータ番号
		DB_MENU = 0xa000,	// メニューデータ番号
		DB_SMSG = 0xb000,	// システムメッセージデータ番号
		DB_MISC = 0xf000,	// その他のデータ番号

		I_DLED_DTYP = 64,	// データタイプ
		I_DLED_DNUM = 0x2000	// データ番号
        };

	// 通常のパネル
	enum NORMALPNL_NUM {
		PNL_UPDEXIT  = (DB_SPNL + 0),	// 更新終了確認パネル
		PNL_UPDATE   = (DB_SPNL + 1),	// 更新確認パネル
		PNL_OTHUPD   = (DB_SPNL + 2),	// 他より更新時の更新確認パネル
		PNL_ERRSAVE  = (DB_SPNL + 3),	// 障害時の新規保存確認パネル
		PNL_OTHEXIT  = (DB_SPNL + 4),	// 他より更新時の終了確認パネル
		PNL_UPDEXIT2 = (DB_SPNL + 5),	// 更新終了確認パネル(更新禁止)
		PNL_UPESKIP  = (DB_SPNL + 6),	// 更新終了確認パネル(読飛ばし)
		PNL_UPDSKIP  = (DB_SPNL + 7)	// 更新確認パネル(読み飛ばし)
	};

	// エラーパネル
	enum ERRPNL_NUM {
		EPNL_SYSTEM   = (DB_SPNL + 100),// システムに異常
		EPNL_EXECMSG  = (DB_SPNL + 101),// 起動方法不正
		EPNL_MEMORY   = (DB_SPNL + 102),// メモリ不足
		EPNL_FREAD    = (DB_SPNL + 103),// 付箋固有データが読み込めない
		EPNL_FWRITE   = (DB_SPNL + 104),// 付箋固有データが書き込めない
		EPNL_WINDOW   = (DB_SPNL + 105),// ウィンドウが開けられない
		EPNL_MENU     = (DB_SPNL + 106),// メニューが開けられない
		EPNL_PUSHTRAY = (DB_SPNL + 107),// トレーに複写/移動できない
		EPNL_POPTRAY  = (DB_SPNL + 108),// トレーから複写/移動できない
		EPNL_PASTE    = (DB_SPNL + 109),// ウィンドウへ複写移動できない
		EPNL_TMPTRAY  = (DB_SPNL + 110),// 一時トレーに格納できない
		EPNL_FSNEXEC  = (DB_SPNL + 111),// 付箋起動は禁止
		EPNL_LOADSKIP = (DB_SPNL + 112),// TAD の読み飛ばしあり
		EPNL_VOBJSKIP = (DB_SPNL + 113),// 仮身の読み飛ばしあり
		EPNL_READ     = (DB_SPNL + 114),// 実身からの読み込みに失敗
		EPNL_WRITE    = (DB_SPNL + 115),// 実身への書き込みに失敗
		EPNL_NEWOBJ   = (DB_SPNL + 116),// 実身生成失敗
		EPNL_NEWVOBJ  = (DB_SPNL + 117),// 仮身生成失敗
		EPNL_UNDOMAKE = (DB_SPNL + 118)	// 取り消し生成失敗
	};

	// その他のパネル
	enum OTHERPNL_NUM {
		OPNL_TIDY = (DB_SPNL + 200)	// 整頓設定パネル
	};

	// パネル内文字列
	enum PSTR_NUM {
	};

	// メニュー
	enum MENU_NUM {
		MENU_MAIN = (DB_MENU + 0)	// メインメニューの定義
	};

	// システムメッセージパネル
	enum SYSMSG_NUM {
		SMSG_LOAD = (DB_SMSG + 0),	// 読み込み中
		SMSG_SAVE = (DB_SMSG + 1)	// 書き込み中
	};

	// ウィンドウ定義(分類はその他に属する)
	enum DEFWIN_NUM {
		DEF_MAINWIN = (DB_MISC + 0)	// 主ウィンドウ定義
	};

	// その他
	enum OTHERDAT_NUM {
		TRAY_NAME = (DB_MISC + 100),	// トレー格納時の名称
		LOCAL_DB  = (DB_MISC + 255)	// local databox
	};
};

#endif	// _I_DLED_DBOX_H
