//
//	textin.cc (簡易文字列編集/text_in 系処理)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#include	<basic.h>
#include	<btron/btron.h>
#include	<btron/libapp.h>
#include	<errcode.h>

#include	"cval.h"
#include	"err.h"
#include	"except.h"
#include	"debug.h"

#include	"textin.h"


// ------------------------------------------------------ TEXTIN 内 public 関数
//
// constructor
//
TEXTIN::TEXTIN(W wid, W pmode)
	: tid(-1), mode(pmode), tport(NULL)
{
	tid = open_text(wid, pmode, &tport);
	DPRINT(("tid : %d(%d)\n", tid, tid >> 16));
	if (tid < ER_OK) {
		throw EXCEPT_TEXTIN(tid);
	}

	// open 後の設定
	tport->mode = TXT_WAIT | TXT_POS | TXT_KEY;
	tport->lpitch = CVAL::CH_VGAP;
	tport->frcol = CVAL::RGB_BLACK;
	tport->car->height = CHSSTD;
}


//
// destructor
//
TEXTIN::~TEXTIN()
{
	close_text(tport);
}


//
// 再 open
//
void	TEXTIN::reopen(W pmode)
{
	if (pmode != mode) {
		W	id;

		mode = pmode;
		id = reopen_text(tport, mode);
		if (id < ER_OK) {
			throw EXCEPT_TEXTIN(tid);
		}
		tid = id;
	}

	return;
}
