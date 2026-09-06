//
//	ud_rsz.h (偽仮身一覧/取り消し動作-形状変更)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#ifndef	_I_DLED_UD_RSZ_H
#define	_I_DLED_UD_RSZ_H

#include	<basic.h>
#include	<btron/btron.h>

#include	<stack>

#include	"ud_ope.h"


// ------------------------------------------------------------ class UD_RESIZE
class	UD_RESIZE : public UD_OPE {
	typedef	struct {		// 取り消し内容(1仮身当たり)
		W	vid;		// 仮身 ID
		bool	sel;		// 選択状態
		RECT	r;		// 仮身の位置
	} UDATA;

public:
	UD_RESIZE(UW ptype);		// constructor
	~UD_RESIZE();			// destructor

	// 内容の生成
	void	make();

	// 取り消しの実行
	void	exec();

private:
	std::stack<UDATA>	udat;
};

#endif	// _I_DLED_UD_RSZ_H
