//
//	setuppnl.h (壁紙変更/設定パネル操舵系ヘッダ)
//
//	(C) Copyright 2001 by Personal Media Corporation.
//

#ifndef	_WALLCHG_SETUPPNL_H
#define	_WALLCHG_SETUPPNL_H

#include	<basic.h>
#include	<btron/btron.h>


// ------------------------------------------------------------- class SETUPPNL
class	SETUPPNL {
public:
	enum STPIDX {			// パネル内パーツ内部番号
		STPIDX_MS_EXIT   = 1,	// [終了]
		STPIDX_MS_SET    = 2,	// [常駐]
		STPIDX_SB_MIN    = 3,	// 分設定
		STPIDX_SB_SEC    = 4,	// 秒設定
		STPIDX_AS_PANEL  = 5,	// [起動時のパネル]
		STPIDX_AS_CHANGE = 6	// [起動時にも壁紙変更]
	};

	SETUPPNL();			// constructor
	~SETUPPNL();			// destructor

	// 各設定値の取得
	void	get_setdat();

	// パネルの実行
	WERR	exec();

private:
	W	pnid;			// panel ID
};

#endif	// _WALLCHG_SETUPPNL_H
