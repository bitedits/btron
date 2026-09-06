//
//	caretope.cc (簡易文字列編集/CARET 管理系)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#include	<basic.h>
#include	<btron/btron.h>
#include	<btron/libapp.h>

#include	"caretope.h"


// ---------------------------------------------------- CARETOPE 内 public 関数
//
// constructor
//
CARETOPE::CARETOPE(const TEXTPORT* port)
	: tport(port)
{
}


//
// destructor
//
CARETOPE::~CARETOPE()
{
}


//
// caret の移動
//
void	CARETOPE::move(PNT p, bool disp)
{
	if (disp) {
		// 消去
		off();
	}
	tport->car->height = CHSSTD;
	tport->car->pos = p;
	if (disp) {
		// 表示
		on();
	}

	return;
}
