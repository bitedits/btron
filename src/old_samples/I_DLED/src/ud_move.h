//
//	ud_move.h (偽仮身一覧/取り消し動作-位置移動)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#ifndef	_I_DLED_UD_MOVE_H
#define	_I_DLED_UD_MOVE_H

#include	<basic.h>
#include	<btron/btron.h>

#include	<stack>

#include	"ud_ope.h"


// -------------------------------------------------------------- class UD_MOVE
class	UD_MOVE : public UD_OPE {
	typedef	struct {		// 取り消し内容(1仮身当たり)
		W	vid;		// 仮身 ID
		bool	sel;		// 選択状態
		RECT	r;		// 仮身の位置
	} UDATA;

public:
	UD_MOVE(UW ptype);		// constructor
	~UD_MOVE();			// destructor

	// 内容の生成
	void	make();

	// 取り消しの実行
	void	exec();

private:
	std::stack<UDATA>	udat;
};

#endif	// _I_DLED_UD_MOVE_H
