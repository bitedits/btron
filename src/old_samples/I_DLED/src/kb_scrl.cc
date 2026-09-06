//
//	kb_scrl.cc (偽仮身一覧/キーボード操作系-スクロール処理)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#include	<basic.h>
#include	<btron/btron.h>
#include	<btron/libapp.h>
#include	<keycode.h>

#include	"val.h"
#include	"err.h"
#include	"except.h"

#include	"appl.h"
#include	"fusen.h"
#include	"guiope.h"
#include	"mainwin.h"
#include	"kb_scrl.h"


// ----------------------------------------------------- KB_SCRL 内 public 関数
//
// constructor
//
KB_SCRL::KB_SCRL(TC code, UW stat)
	: type(0), diff(0),
	  varea(appl->fsn->get_area()), vrect(appl->gui->mwin->vrect)
{
	switch (code) {
		case KC_CC_U:		// 通常の caret key か ES_CMD 併用
			if (stat & ES_CMD) {
				type = TYPE_RBAR | SCRL_AREA | DRCT_UP;
				diff = rectheight(vrect);
			} else {
				type = TYPE_RBAR | SCRL_SMTH | DRCT_UP;
				diff = 1;
			}
			break;
		case KC_CC_D:
			if (stat & ES_CMD) {
				type = TYPE_RBAR | SCRL_AREA | DRCT_DOWN;
				diff = rectheight(vrect);
			} else {
				type = TYPE_RBAR | SCRL_SMTH | DRCT_DOWN;
				diff = 1;
			}
			break;
		case KC_CC_R:
			if (stat & ES_CMD) {
				type = TYPE_BBAR | SCRL_AREA | DRCT_RIGHT;
				diff = rectwidth(vrect);
			} else {
				type = TYPE_BBAR | SCRL_SMTH | DRCT_RIGHT;
				diff = 1;
			}
			break;
		case KC_CC_L:
			if (stat & ES_CMD) {
				type = TYPE_BBAR | SCRL_AREA | DRCT_LEFT;
				diff = rectwidth(vrect);
			} else {
				type = TYPE_BBAR | SCRL_SMTH | DRCT_LEFT;
				diff = 1;
			}
			break;
		case KC_SC_U:		// 主に [ES_EXT] 併用
			type = TYPE_RBAR | SCRL_JUMP | DRCT_UP;
			diff = rectheight(varea);
			break;
		case KC_SC_D:
			type = TYPE_RBAR | SCRL_JUMP | DRCT_DOWN;
			diff = -rectheight(varea);
			if (diff > 0) diff = 0;
			break;
		case KC_SC_R:
			type = TYPE_BBAR | SCRL_JUMP | DRCT_RIGHT;
			diff = -rectwidth(varea);
			if (diff > 0) diff = 0;
			break;
		case KC_SC_L:
			type = TYPE_BBAR | SCRL_JUMP | DRCT_LEFT;
			diff = rectwidth(varea);
			break;
		case KC_SS_U:		// 主に wheel の回転
			type = TYPE_RBAR | SCRL_SMTH | DRCT_UP;
			diff = 1;
			break;
		case KC_SS_D:
			type = TYPE_RBAR | SCRL_SMTH | DRCT_DOWN;
			diff = 1;
			break;
		case KC_SS_R:
			type = TYPE_BBAR | SCRL_SMTH | DRCT_RIGHT;
			diff = 1;
			break;
		case KC_SS_L:
			type = TYPE_BBAR | SCRL_SMTH | DRCT_LEFT;
			diff = 1;
			break;
		case KC_PG_U:		// page scroll
			type = TYPE_RBAR | SCRL_AREA | DRCT_UP;
			diff = rectheight(vrect);
			break;
		case KC_PG_D:
			type = TYPE_RBAR | SCRL_AREA | DRCT_DOWN;
			diff = rectheight(vrect);
			break;
		case KC_PG_R:
			type = TYPE_BBAR | SCRL_AREA | DRCT_RIGHT;
			diff = rectwidth(vrect);
			break;
		case KC_PG_L:
			type = TYPE_BBAR | SCRL_AREA | DRCT_LEFT;
			diff = rectwidth(vrect);
			break;
	}
	if ((type == 0) && (diff == 0)) {
		throw EXCEPT_KBOPE(I_DLEDERR::KB_NOOPE);
	}
}


//
// destructor
//
KB_SCRL::~KB_SCRL()
{
}


//
// 実行処理
//
void	KB_SCRL::main()
{
	if ((type != 0) || (diff != 0)) {
		appl->gui->mwin->scroll_fn(type, diff);
	}

	return;
}
