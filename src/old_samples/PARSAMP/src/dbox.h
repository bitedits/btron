//
//	dbox.h (パーツ操作例/databox 参照番号)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#ifndef	_PARSAMP_DBOX_H
#define	_PARSAMP_DBOX_H


// ------------------------------------------------------------- namespace DBOX
namespace	DBOX {
	// 共通定義
	enum COMMON {
		DB_SPNL = 0x8000,	// 単純パネルデータ番号
		DB_PSTR = 0x9000,	// パネル文字列バッファデータ番号
		DB_MENU = 0xa000,	// メニューデータ番号
		DB_SMSG = 0xb000,	// システムメッセージデータ番号
		DB_MISC = 0xf000,	// その他のデータ番号

		PARSAMP_DTYP = 64,	// データタイプ
		PARSAMP_DNUM = 0x2000	// データ番号
        };

	// 通常のパネル
	enum NORMALPNL_NUM {
		PNL_UPDATE = (DB_SPNL + 0)	// 更新終了確認パネル
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
		EPNL_PUSHTRAY = (DB_SPNL + 107),// トレーに複写できない
		EPNL_POPTRAY  = (DB_SPNL + 108),// トレーから複写/移動できない
		EPNL_PASTE    = (DB_SPNL + 109),// ウィンドウへ複写移動できない
		EPNL_TMPTRAY  = (DB_SPNL + 110)	// 一時トレーに格納できない
	};

	// その他のパネル
	enum OTHERPNL_NUM {
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
	};

	// ウィンドウ定義(分類はその他に属する)
	enum DEFWIN_NUM {
		DEF_MAINWIN = (DB_MISC + 0),	// 主ウィンドウ定義
	};

	// その他
	enum OTHERDAT_NUM {
		TRAY_NAME = (DB_MISC + 100),	// トレー格納時の名称
		LOCAL_DB  = (DB_MISC + 255)	// local databox
	};
};

#endif	// _PARSAMP_DBOX_H
