//
//	kb_move.h (偽仮身一覧/キーボード操作系-仮身移動処理)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#ifndef	_I_DLED_KB_MOVE_H
#define	_I_DLED_KB_MOVE_H

#include	<basic.h>
#include	<btron/btron.h>

#include	"kb_ope.h"


// -------------------------------------------------------------- class KB_MOVE
class	KB_MOVE : public KB_OPE {
public:
	KB_MOVE(TC code, UW stat);	// constructor
	~KB_MOVE();			// destructor

	// 実行処理
	void	main();

private:
	SIZE	dl;
};

#endif	// _I_DLDE_KB_MOVE_H
