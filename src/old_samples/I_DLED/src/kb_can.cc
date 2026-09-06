//
//	kb_can.cc (偽仮身一覧/キーボード操作系-取り消し処理)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#include	<basic.h>
#include	<btron/btron.h>

#include	"val.h"
#include	"err.h"
#include	"except.h"

#include	"appl.h"
#include	"guiope.h"
#include	"kb_can.h"
#include	"mainmenu.h"


// ------------------------------------------------------ KB_CAN 内 public 関数
//
// constructor
//
KB_CAN::KB_CAN()
{
	if (appl->gui->undo == NULL) {
		sig_buz(0);		// beep
		throw EXCEPT_KBOPE(I_DLEDERR::KB_NOOPE);
	}
}


//
// destructor
//
KB_CAN::~KB_CAN()
{
}


//
// 実行処理
//
void	KB_CAN::main()
{
	W	rv;

	rv = appl->gui->menu->mn_undo(0);
	if (rv < ER_OK) {
		throw EXCEPT_KBOPE(rv);
	}

	return;
}
