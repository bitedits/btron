//
//	tidypnl.h (偽仮身一覧/整頓パネル管理系ヘッダ)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#ifndef	_I_DLED_TIDYPNL_H
#define	_I_DLED_TIDYPNL_H

#include	<basic.h>
#include	<btron/btron.h>

#include	"struct.h"


// -------------------------------------------------------------- class TIDYPNL
class	TIDYPNL {
public:
	enum TSPIDX {			// パネル内パーツ番号
		TSPIDX_MS_CANCEL = 1,	// [取り消し]
		TSPIDX_MS_DO     = 2,	// [実行]
		TSPIDX_WS_H      = 3,	// 横設定用 WS_PARTS
		TSPIDX_WS_V      = 4,	// 縦設定用 WS_PARTS
		TSPIDX_WS_L      = 5	// 長さ設定用 WS_PARTS
	};

	TIDYPNL(bool ponly);		// constructor
	~TIDYPNL();			// destructor

	// パネルの実行
	W	exec();

private:
	W	pnid;			// panel ID
	bool	only;			// true : 単一選択
	TIDYDAT	tdat;			// 設定内容
};

#endif	// _I_DLED_TIDY_PNL_H
