//
//	appl.cc (パーツ操作例/application 基幹部-基底 class)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#include	<basic.h>
#include	<btron/btron.h>
#include	<btron/vobj.h>

#include	"debug.h"

#include	"appl.h"
#include	"fusen.h"
#include	"guiope.h"


// ------------------------------------------------ PARSAMP_APPL 内 public 関数
//
// constructor(default)
//
PARSAMP_APPL::PARSAMP_APPL()
{
	DPRINT(("PARSAMP_APPL(base class) default constructor\n"));

	// 情報の初期化
	mydat.pid = prc_sts(0, NULL, NULL);
	mydat.vid = -1;
	mydat.pwid = -1;
	mydat.pr = (RECT){{0, 0, 0, 0}};
	mydat.lnk = (LINK){{TNULL}, 0x0000, 0, 0, 0, 0, 0};
	prc_inf(0, PI_LINK, &mydat.alnk, sizeof(LINK));
}


//
// constructor(copy)
//
PARSAMP_APPL::PARSAMP_APPL(const MESSAGE* msg)
{
	DPRINT(("PARSAMP_APPL(base class) copy constructor\n"));

	mydat.msg.msg = msg;
}


//
// destructor
//
PARSAMP_APPL::~PARSAMP_APPL()
{
	DPRINT(("PARSAMP_APPL destructor\n"));
}
