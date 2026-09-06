//
//	appl.cc (画像閲覧/application 基幹部-基底 class)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#include	<basic.h>
#include	<btron/btron.h>
#include	<btron/vobj.h>

#include	"debug.h"

#include	"appl.h"
#include	"imgope.h"
#include	"fusen.h"
#include	"guiope.h"


// ------------------------------------------------ IMGVIEW_APPL 内 public 関数
//
// constructor(default)
//
IMGVIEW_APPL::IMGVIEW_APPL()
	: img(NULL)
{
	DPRINT(("IMGVIEW_APPL(base class) default constructor\n"));

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
IMGVIEW_APPL::IMGVIEW_APPL(const MESSAGE* msg)
{
	DPRINT(("IMGVIEW_APPL(base class) copy constructor\n"));

	mydat.msg.msg = msg;
}


//
// destructor
//
IMGVIEW_APPL::~IMGVIEW_APPL()
{
	DPRINT(("IMGVIEW_APPL destructor\n"));

	delete img;
}


//
// 表示画像の読み込み
//
void	IMGVIEW_APPL::load_img()
{
	img = new IMGOPE(&mydat.lnk);

	return;
}
