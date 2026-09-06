//
//	kb_can.h (偽仮身一覧/キーボード操作系-取り消し処理
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#ifndef	_I_DLED_KB_CAN_H
#define	_I_DLED_KB_CAN_H

#include	<basic.h>
#include	<btron/btron.h>

#include	"kb_ope.h"


// --------------------------------------------------------------- class KB_CAN
class	KB_CAN : public KB_OPE {
public:
	KB_CAN();			// constructor
	~KB_CAN();			// destructor

	// 実行処理
	void	main();

private:
};

#endif	// _I_DLED_KB_CAN_H
