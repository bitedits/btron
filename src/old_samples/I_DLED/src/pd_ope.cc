//
//	pd_ope.cc (偽仮身一覧/ポインタ操作基底部)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#include	<basic.h>
#include	<btron/btron.h>
#include	<btron/libapp.h>

#include	"val.h"
#include	"cval.h"

#include	"appl.h"
#include	"guiope.h"
#include	"mainwin.h"
#include	"pd_ope.h"

#include	<bstdio.h>


// ------------------------------------------------------ PD_OPE 内 public 関数
//
// constructor
//
PD_OPE::PD_OPE()
	: wid(appl->gui->mwin->get_wid()), dgid(-1), dir(0),
	  vrect(appl->gui->mwin->vrect),
	  hjmp(rectwidth(vrect) >> 2), vjmp(rectheight(vrect) >> 2),
	  sp(wevt.s.pos), vp(vrect.p.lefttop), vpN(vp),
	  in(true), on(false), lock(false),
	  mod((wevt.s.stat & (ES_LSHFT | ES_RSHFT)) != 0x0000)
{
	// 他の変数の設定
	lrect.p.lefttop = CVAL::LIMIT_RECT.p.lefttop;
	lrect.c.right = CVAL::LIMIT_RECT.c.right + CHSSTD - rectwidth(vrect);
	lrect.c.bottom = CVAL::LIMIT_RECT.c.bottom + CHSSTD -rectheight(vrect);
	srgn.rgn.r.p.lefttop = sp;
	srgn.sts = 0x0000;
}


// ----------------------------------------------------- PD_OPE 内 private 関数
//
// ウィンドウ枠上でのスクロール位置の設定
//
void	PD_OPE::set_spos(PNT p)
{
	// スクロール量の反映
	if (p.x < vrect.c.left) {
		vp.x -= hjmp;
	} else if (p.x >= vrect.c.right) {
		vp.x += hjmp;
	}
	if (p.y < vrect.c.top) {
		vp.y -= vjmp;
	} else if (p.y >= vrect.c.bottom) {
		vp.y += vjmp;
	}
 
	// 制限内に収める
	if (vp.x < lrect.c.left) {
		vp.x = lrect.c.left;
	} else if (vp.x > lrect.c.right) {
		vp.x = lrect.c.right;
	}
	if (vp.y < lrect.c.top) {
		vp.y = lrect.c.top;
	} else if (vp.y > lrect.c.bottom) {
		vp.y = lrect.c.bottom;
	}
 
	return;
}


//
// 選択枠の生成
//
void	PD_OPE::gen_selfrm(PNT p1, PNT p2)
{
	// 左辺/右辺の設定
	if (p1.x <= p2.x) {
		srgn.rgn.r.c.left = p1.x;
		srgn.rgn.r.c.right = p2.x;
	} else {
		srgn.rgn.r.c.left = p2.x;
		srgn.rgn.r.c.right = p1.x;
	}
 
	// 上底/下底の設定
	if (p1.y <= p2.y) {
		srgn.rgn.r.c.top = p1.y;
		srgn.rgn.r.c.bottom = p2.y;
	} else {
		srgn.rgn.r.c.top = p2.y;
		srgn.rgn.r.c.bottom = p1.y;
	}
 
	return;
}


//
// 選択枠を移動する
//
void	PD_OPE::move_selfrm(RECT r, PNT p, SIZE gap)
{
	// 選択枠を消す
	adsp_sel(dgid, &srgn, 0);

	// 方向束縛の確認(自ウィンドウ内の時のみ)
	W	ptype;

	ptype = PS_GRIP;		// 握り
	if (in) {
		if ((wevt.s.stat & (ES_LSHFT | ES_RSHFT)) != 0x0000) {
			// 修正選択状態
			if (dir == 1) {
				// 水平移動
				p.y = sp.y;
				ptype = PS_HGRIP;	// 握り(横方向)
			} else if (dir == 2) {
				// 垂直移動
				p.x = sp.x;
				ptype = PS_VGRIP;	// 握り(縦方向)
			} else {
				// 新規(方法の設定)
				SIZE    dp;

				dp.h = ((sp.x > p.x) ? -1 : 1) * (p.x - sp.x);
				dp.v = ((sp.y > p.y) ? -1 : 1) * (p.y - sp.y);
				dir = (dp.h > dp.v) ? 1 : 2;
			}
		} else {
			dir = 0;
		}
	}
	setpointer(ptype, NULL);

	// 選択枠の設定
	srgn.rgn.r.c.left = p.x - gap.h;
	srgn.rgn.r.c.top = p.y - gap.v;
	if (in) {
		// 自ウィンドウ内の時は制限を確認する
		if (srgn.rgn.r.c.left < CVAL::LIMIT_RECT.c.left) {
			srgn.rgn.r.c.left = CVAL::LIMIT_RECT.c.left;
		}
		if (srgn.rgn.r.c.top < CVAL::LIMIT_RECT.c.top) {
			srgn.rgn.r.c.top = CVAL::LIMIT_RECT.c.top;
		}
		if (srgn.rgn.r.c.left + rectwidth(r) > CVAL::LIMIT_RECT.c.right) {
			srgn.rgn.r.c.left = CVAL::LIMIT_RECT.c.right - rectwidth(r);
		}
		if (srgn.rgn.r.c.top + rectheight(r) > CVAL::LIMIT_RECT.c.bottom) {
			srgn.rgn.r.c.top = CVAL::LIMIT_RECT.c.bottom - rectheight(r);
		}
	}
	srgn.rgn.r.c.right = srgn.rgn.r.c.left + rectwidth(r);
	srgn.rgn.r.c.bottom = srgn.rgn.r.c.top + rectheight(r);

	// 選択枠を描く
	adsp_sel(dgid, &srgn, 1);

        return;
}
