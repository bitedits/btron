//
//	dbox.h (壁紙変更/databox 参照番号)
//
//	(C) Copyright 2001 by Personal Media Corporation.
//

#ifndef	_WALLCHG_DBOX_H
#define	_WALLCHG_DBOX_H


// ------------------------------------------------------------- namespace DBOX
namespace	DBOX {
	// 共通定義
	enum COMMON {
		DB_SPNL = 0x8000,	// 単純パネルデータ番号
		DB_PSTR = 0x9000,	// パネル文字列バッファデータ番号
		DB_MENU = 0xa000,	// メニューデータ番号
		DB_SMSG = 0xb000,	// システムメッセージデータ番号
		DB_MISC = 0xf000,	// その他のデータ番号

		WALLCHG_DTYP = 64,	// データタイプ
		WALLCHG_DNUM = 0x2000	// データ番号
        };

	// 通常のパネル
	enum NORMALPNL_NUM {
	};

	// エラーパネル
	enum ERRPNL_NUM {
		EPNL_SYSTEM  = (DB_SPNL + 50),	// システムに異常
		EPNL_EXECMSG = (DB_SPNL + 51),	// 起動方法不正
		EPNL_MEMORY  = (DB_SPNL + 52),	// メモリ不足
		EPNL_FREAD   = (DB_SPNL + 53),	// 付箋固有データが読み込めない
		EPNL_FWRITE  = (DB_SPNL + 54),	// 付箋固有データが書き込めない
		EPNL_MAINTSK = (DB_SPNL + 55),	// 主幹部での障害
		EPNL_SUBTSK  = (DB_SPNL + 56),	// subtask での障害
		EPNL_WALLCHG = (DB_SPNL + 57),	// 壁紙変更での障害
		EPNL_SETUP   = (DB_SPNL + 58),	// 設定パネルでの障害
		EPNL_REGFEP  = (DB_SPNL + 59)	// fep になれない
	};

	// その他のパネル
	enum OTHERPNL_NUM {
		OPNL_SETUP = (DB_SPNL + 100)	// 設定パネル
	};

	// メニュー
	enum MENU_NUM {
	};

	// システムメッセージパネル
	enum SYSMSG_NUM {
	};

	// ウィンドウ定義(分類はその他に属する)
	enum DEFWIN_NUM {
	};

	// その他
	enum OTHERDAT_NUM {
		GLOBAL_NM = (DB_MISC + 50),	// global 名
		BGS_PATH  = (DB_MISC + 51)	// $$BGSCREEN.BOX への PATH
	};
};

#endif	// _WALLCHG_DBOX_H
