//
//	kb_del.cc (偽仮身一覧/キーボード処理系-削除処理)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#include	<basic.h>
#include	<btron/btron.h>
#include	<btron/vobj.h>

#include	"val.h"
#include	"err.h"
#include	"except.h"

#include	"appl.h"
#include	"vobjope.h"
#include	"vobjs.h"
#include	"guiope.h"
#include	"kb_del.h"
#include	"mainmenu.h"


// ------------------------------------------------------ KB_DEL 内 public 関数
//
// constructor
//
KB_DEL::KB_DEL()
{
	bool 	non;
	const	VOBJ*	vobj = appl->vobjs.get_vobj();

	non = true;
	if (vobj != NULL) {
		VOBJ*	ptr = const_cast<VOBJ*>(vobj);
 
		do {
                        const   VLINK*  vlnk = ptr->get_vlnk();
 
			if (ptr->get_sel()) {
				// 選択状態
				if (!(ptr->get_hold())) {
					// 選択/非固定状態
					non = false;
					break;
				}
			}
			ptr = const_cast<VOBJ*>(ptr->get_next());
		} while (ptr != vobj);
	}

	if (non) {
		throw EXCEPT_KBOPE(I_DLEDERR::KB_NOOPE);
	}
}


//
// destructor
//
KB_DEL::~KB_DEL()
{
}


//
// 実行処理
//
void	KB_DEL::main()
{
	W	rv;

	rv = appl->gui->menu->mn_delete(0);
	if (rv < ER_OK) {
		throw EXCEPT_KBOPE(rv);
	}

	return;
}
