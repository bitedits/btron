//
//	appl.cc (偽仮身一覧/application 基幹部-基底 class)
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
#include	"fusen.h"
#include	"guiope.h"


// ------------------------------------------------- I_DLED_APPL 内 public 関数
//
// constructor(default)
//
I_DLED_APPL::I_DLED_APPL()
{
	DPRINT(("I_DLED_APPL(base class) default constructor\n"));

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
I_DLED_APPL::I_DLED_APPL(const MESSAGE* msg)
{
	DPRINT(("I_DLED_APPL(base class) copy constructor\n"));

	mydat.msg.msg = msg;
}


//
// destructor
//
I_DLED_APPL::~I_DLED_APPL()
{
	DPRINT(("I_DLED_APPL destructor\n"));
}
