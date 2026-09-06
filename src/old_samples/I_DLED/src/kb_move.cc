//
//	kb_move.cc (偽仮身一覧/キーボード操作系-仮身移動処理)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#include	<basic.h>
#include	<btron/btron.h>
#include	<keycode.h>

#include	"val.h"
#include	"err.h"
#include	"except.h"

#include	"appl.h"
#include	"editobj.h"
#include	"vobjs.h"
#include	"guiope.h"
#include	"mainwin.h"
#include	"kb_move.h"
#include	"ud_ope.h"


// ----------------------------------------------------- KB_MOVE 内 public 関数
//
// constructor
//
KB_MOVE::KB_MOVE(TC code, UW stat)
{
	dl = (SIZE){0, 0};
	if (!(stat & ES_CMD)) {
		switch (code) {
			case KC_CC_U:	// 通常の caret key
				dl.v = -1;
				break;
			case KC_CC_D:
				dl.v = 1;
				break;
			case KC_CC_R:
				dl.h = 1;
				break;
			case KC_CC_L:
				dl.h = -1;
				break;
		}
	}
	if ((dl.h == 0) && (dl.v == 0)) {
		throw EXCEPT_KBOPE(I_DLEDERR::KB_NOOPE);
	}
}


//
// destructor
//
KB_MOVE::~KB_MOVE()
{
}


//
// 実行処理
//
void	KB_MOVE::main()
{
	if ((dl.h != 0) || (dl.v != 0)) {
		UD_OPE::make_undo(UD_OPE::UT_MOVE);
		appl->eobj->set_editflg(true);  // 更新
		appl->gui->mwin->disp_selfrm(0);
		appl->vobjs.copy_selvobj(false, dl);
		appl->gui->mwin->upd_narea();
		appl->gui->mwin->disp_selfrm(-2);
		appl->gui->mwin->disp_selfrm(1);
	}

	return;
}
