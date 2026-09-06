//
//	appl.cc (簡易文字列編集/application 基幹部-基底 class)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#include	<basic.h>
#include	<btron/btron.h>
#include	<btron/vobj.h>

#include	"cval.h"
#include	"debug.h"

#include	"appl.h"
#include	"editobj.h"
#include	"strope.h"
#include	"fusen.h"
#include	"guiope.h"


// ----------------------------------------------- T_TXEDIT_APPL 内 public 関数
//
// constructor(default)
//
T_TXEDIT_APPL::T_TXEDIT_APPL()
{
	DPRINT(("T_TXEDIT_APPL(base class) default constructor\n"));

	// 情報の初期化
	mydat.pid = prc_sts(0, NULL, NULL);
	mydat.vid = -1;
	mydat.pwid = -1;
	mydat.pr = (RECT){{0, 0, 0, 0}};
	mydat.lnk = (LINK){{TNULL}, 0x0000, 0, 0, 0, 0, 0};
	prc_inf(0, PI_LINK, &mydat.alnk, sizeof(LINK));
	mydat.bgcol = CVAL::RGB_WHITE;
}


//
// constructor(copy)
//
T_TXEDIT_APPL::T_TXEDIT_APPL(const MESSAGE* msg)
{
	DPRINT(("T_TXEDIT_APPL(base class) copy constructor\n"));

	mydat.msg.msg = msg;
}


//
// destructor
//
T_TXEDIT_APPL::~T_TXEDIT_APPL()
{
	DPRINT(("T_TXEDIT_APPL destructor\n"));
}
