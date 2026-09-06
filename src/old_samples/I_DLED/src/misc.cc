//
//	misc.cc (偽仮身一覧/雑多関数群)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#include	<basic.h>
#include	<btron/btron.h>
#include	<btron/hmi.h>
#include	<btron/libapp.h>
#include	<errcode.h>

#include	"misc.h"


// ----------------------------------------- MISC 内 public 関数(static member)
//
// 補正をかけた wget_drg()
//
// wget_evt(wevt, NOMSG) の状態に近い内容を返します。
//
WERR	MISC::wget_drg_C(PNT* p, WEVENT* evt)
{
	WERR	type;
	W	wid;

	type = wget_drg(p, evt);
	if (type >= ER_OK) {
		PNT	pbuf;

		get_etm(&evt->s.time);
		get_pdp(&pbuf);

		// WEVENT.s.wid は H 型であり、wfnd_wnd() は W 型で返すので、
		// wfnd_wnd(,, &WEVENT.s.wid) はできない
		evt->s.cmd = wfnd_wnd(&pbuf, &evt->s.pos, &wid);
		evt->s.wid = wid;
	}

	return type;
}


//
// 制限範囲内に矩形枠を収める
//
void	MISC::cliprect(RECT& r, const RECT limit)
{
	SIZE	s;

	s = (SIZE){rectwidth(r), rectheight(r)};

	if (r.c.left < limit.c.left) {
		r.c.left = 0;
		r.c.right = s.h;
	} else if (r.c.right >= limit.c.right) {
		r.c.right = limit.c.right;
		r.c.left = r.c.right - s.h;
	}
	if (r.c.top < limit.c.top) {
		r.c.top = 0;
		r.c.bottom = s.v;
	} else if (r.c.bottom >= limit.c.bottom) {
		r.c.bottom = limit.c.bottom;
		r.c.top = r.c.bottom - s.v;
	}

	return;
}
