//
//	mainwin.cc (画像閲覧/主ウィンドウ操舵系)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#include	<basic.h>
#include	<btron/btron.h>
#include	<btron/dp.h>
#include	<btron/hmi.h>
#include	<btron/vobj.h>
#include	<btron/libapp.h>
#include	<btron/cnvend.h>
#include	<bstring.h>
#include	<errcode.h>
#include	<keycode.h>
#include	<tstring.h>
#include	<tcode.h>

#include	<new>
#include	<memory>
#include	<vector>

#include	"val.h"
#include	"cval.h"
#include	"dbox.h"
#include	"struct.h"
#include	"err.h"
#include	"except.h"
#include	"debug.h"

#include	"appl.h"
#include	"imgope.h"
#include	"fusen.h"
#include	"evtope.h"
#include	"mainwin.h"
#include	"mainmenu.h"
#include	"misc.h"


// ----------------------------------------------------- MAINWIN 内 public 関数
//
// constructor
//
MAINWIN::MAINWIN()
	: wid(-1), gid(-1), bgcol(CVAL::COL_TRANS)
{
	// ウィンドウ定義データの取り出し
	ConvEndianStruct(&wdef, getdbox(DBOX::DEF_MAINWIN), WINDEF_STRUCT, sizeof(wdef));

	// その他の初期化処理
	genrectlist(4, rlist);
}


//
// destructor
//
MAINWIN::~MAINWIN()
{
	if (wid >= 0) {
		odel_vob(-wid, CLR);
		wcls_wnd(wid, CLR);
	}
	EVTOPE::winfo[CVAL::WIDX_MAINWIN].wid = 0;
}


//
// ウィンドウを開ける
//
void	MAINWIN::open_win(const W pwid, const RECT* pr)
{
	// ウィンドウの場所を設定
	const	WINATTR	wattr = appl->fsn->get_wattr();

	vrect = wattr.rect;
	if ((vrect.c.left >= vrect.c.right)||(vrect.c.top >= vrect.c.bottom)) {
		vrect = wdef.r;
		centering(&vrect);
	}

	// ウィンドウを開ける
	UW	atr;

	atr = wdef.attr | ((wattr.fwindow) ? WA_FULL : 0);
	wid = wopn_wnd(atr, pwid, &vrect, (RECT*)pr, CVAL::IMGVIEW_PICT, wdef.title, WHITE0, NULL);
	if (wid < ER_OK) {
		throw EXCEPT_MAINWIN(wid);
	}
	DPRINT(("wid : %d\n", wid));
	wget_bar(wid, &bar[0], &bar[1], NULL);

	// 表示周りの初期化
	gid = wget_gid(wid);
	gset_vis(gid, vrect);

	// 追従スクロールにする
	cchg_par(bar[0], P_DRAGBREAK);
	cchg_par(bar[1], P_DRAGBREAK);

	// ウィンドウ情報レコードに登録
	EVTOPE::winfo[CVAL::WIDX_MAINWIN].wid = wid;
	EVTOPE::winfo[CVAL::WIDX_MAINWIN].chgmod = 0x10;
	EVTOPE::winfo[CVAL::WIDX_MAINWIN].limit = (RECT){{0, 0, 0, 0}};
	EVTOPE::winfo[CVAL::WIDX_MAINWIN].pid[0] = bar[0];
	EVTOPE::winfo[CVAL::WIDX_MAINWIN].pid[1] = bar[1];
	EVTOPE::winfo[CVAL::WIDX_MAINWIN].pid[2] = -1;

	return;
}


//
// ウィンドウを閉じる
//
void	MAINWIN::close_win()
{
	wcls_wnd(wid, CLR);
	wid = -1;
	EVTOPE::winfo[CVAL::WIDX_MAINWIN].wid = 0;

	return;
}


//
// 再描画処理
//
void	MAINWIN::redisp(RECT* r)
{
	RECT	vr;
	RECT	dr;

	gget_vis(gid, &vr);

	// 再描画
	wera_wnd(wid, r);
	if (r != NULL) {
		gset_vis(gid, *r);
		dr = *r;
	} else {
		dr = vrect;
	}
	disp_img(dr);

	gset_vis(gid, vr);

	return;
}


//
// 倍率の変動に伴う処理
//
void	MAINWIN::chg_zfact()
{
	redisp(NULL);
	sbar_setup();
	set_wintit();

	return;
}


//
// 定常処理(window event)
//
void	MAINWIN::idle_fn()
{
	// バーツや選択枠もないので、特にやることもない

	return;
}


//
// 表示処理(window event)
//
void	MAINWIN::disp_fn(W mode, RECT* newr)
{
	if (mode < 0) {
		// 初期化処理
		UW	zfact = static_cast<UW>(appl->fsn->get_zfact());
 
		MISC::zfact_to_tbl(zfact);
		appl->fsn->set_zfact(zfact);
		try {
			appl->load_img();
		} catch (EXCEPT_IMGOPE& err) {
			DPRINT(("%s : %d(%d)\n", err.what(), err.get_err(), err.get_err() >> 16));
			errpanel(DBOX::EPNL_IMGLOAD, err.get_err());
		} catch (std::bad_alloc) {
			DPRINT(("can't load(memory allocation error).\n"));
			errpanel(DBOX::EPNL_IMGLOAD, ER_NOMEM);
		} catch (...) {
			DPRINT(("can't load.\n"));
			errpanel(DBOX::EPNL_IMGLOAD, ER_SYS);
		}
		redisp(NULL);
		sbar_setup();
		set_wintit();
	} else {
		// その他の再描画処理
		if (newr != NULL) {
			// サイズが変わった
			RECT	vrectF;

			vrectF = vrect;
			vrect = *newr;
			gset_vis(gid, vrect);
			if ((newr->c.left != 0) || (newr->c.top != 0)) {
				// 左上は必ず (0, 0) にする
				vrect.p.lefttop = (PNT){0, 0};
				wset_wrk(wid, &vrect);
				do {
					wsta_dsp(wid, NULL, NULL);
				} while (wend_dsp(wid) > 0);
				redisp(NULL);
			} else if ((newr->c.right <= vrectF.c.right) &&
				   (newr->c.bottom <= vrectF.c.bottom)) {
				// 右下が小さくなっただけなら内容の再生成
				disp_img((RECT){{0, 0, 0, 0}});
			}
		}

		// 再描画要求の処分
		do {
			W	i;
			RECT	r;

			i = wsta_dsp(wid, &r, rlist);
			if (i <= 0) break;
			if (i > 4) {
				redisp(&r);
			} else {
				while (i--) {
					redisp(&rlist[i].rcomp);
				}
			}
		} while (wend_dsp(wid) > 0);

		// 付箋固有データへの反映
		WINATTR	wattr = static_cast<WINATTR>(appl->fsn->get_wattr());

		if (mode == 5) {
			// 全画面モードになった
			wattr.fwindow = true;
		} else if ((newr != NULL) || (mode == 1)) {
			// サイズが変わったか位置が移動した
			WDSTAT	wstat;

			wget_sts(wid, &wstat, NULL);
			wattr.rect = wstat.r;
			wattr.fwindow = false;
		}
		appl->fsn->set_wattr(wattr);
		sbar_setup();
	}

	return;
}


//
// キー入力処理(window event)
//
W	MAINWIN::key_fn()
{
	W	type;
	W	diff;
	const	RECT	srect = appl->img->get_srect();
 
	type = 0x0000;
	diff = 0x0000;
	switch (wevt.e.data.key.code) {
		case KC_CC_U:		// 通常の caret key か ES_CMD 併用
			if (wevt.s.stat & ES_CMD) {
				type = TYPE_RBAR | SCRL_AREA | DRCT_UP;
				diff = rectheight(srect);
			} else {
				type = TYPE_RBAR | SCRL_SMTH | DRCT_UP;
				diff = 1;
			}
			break;
		case KC_CC_D:
			if (wevt.s.stat & ES_CMD) {
				type = TYPE_RBAR | SCRL_AREA | DRCT_DOWN;
				diff = rectheight(srect);
			} else {
				type = TYPE_RBAR | SCRL_SMTH | DRCT_DOWN;
				diff = 1;
			}
			break;
		case KC_CC_R:
			if (wevt.s.stat & ES_CMD) {
				type = TYPE_BBAR | SCRL_AREA | DRCT_RIGHT;
				diff = rectwidth(srect);
			} else {
				type = TYPE_BBAR | SCRL_SMTH | DRCT_RIGHT;
				diff = 1;
			}
			break;
		case KC_CC_L:
			if (wevt.s.stat & ES_CMD) {
				type = TYPE_BBAR | SCRL_AREA | DRCT_LEFT;
				diff = rectwidth(srect);
			} else {
				type = TYPE_BBAR | SCRL_SMTH | DRCT_LEFT;
				diff = 1;
			}
			break;
		case KC_SC_U:		// 主に [ES_EXT] 併用
			type = TYPE_RBAR | SCRL_JUMP | DRCT_UP;
			diff = rectheight(srect);
			break;
		case KC_SC_D:
			type = TYPE_RBAR | SCRL_JUMP | DRCT_DOWN;
			diff = -rectheight(srect);
			if (diff > 0) diff = 0;
			break;
		case KC_SC_R:
			type = TYPE_BBAR | SCRL_JUMP | DRCT_RIGHT;
			diff = -rectwidth(srect);
			if (diff > 0) diff = 0;
			break;
		case KC_SC_L:
			type = TYPE_BBAR | SCRL_JUMP | DRCT_LEFT;
			diff = rectwidth(srect);
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
			diff = rectheight(srect);
			break;
		case KC_PG_D:
			type = TYPE_RBAR | SCRL_AREA | DRCT_DOWN;
			diff = rectheight(srect);
			break;
		case KC_PG_R:
			type = TYPE_BBAR | SCRL_AREA | DRCT_RIGHT;
			diff = rectwidth(srect);
			break;
		case KC_PG_L:
			type = TYPE_BBAR | SCRL_AREA | DRCT_LEFT;
			diff = rectwidth(srect);
			break;
	}
	if ((type != 0) || (diff != 0)) {
		scroll_fn(type, diff);
	}

	return 0;			// 常時 0
}

//
// 主ボタンプレス処理(window event)
//
W	MAINWIN::press_fn()
{
	// パーツがないので特になにもしない
	W	rv;
 
	rv = ER_OK;

	return rv;
}


//
// スクロール処理(window event)
//
void	MAINWIN::scroll_fn(W type, W diff)
{
	if (appl->img != NULL) {
		W	diff2;
		PNT	p;
		const	RECT	sr = appl->img->get_srect();
		const	RECT	dr = appl->img->get_drect();
	 
		if ((type & 0x0c) == 0) {
			// smooth scroll 時は DLED と同じ加速度をつける
			W       max;
	 
			diff2 = CHSSTD * ((diff >> 1) + 1);
			max = ((type & 2) ? rectwidth(vrect) : rectheight(vrect)) >> 1;
			if (diff2 > max) {
				diff2 = max;
			}
		} else {
			diff2 = 0;
		}
		p = chk_scroll(type, diff, diff2, dr, sr);

		// 移動量が 0 の時はなにもしない
		if ((p.x != 0) || (p.y != 0)) {
			PNT	vp = static_cast<PNT>(appl->fsn->get_vpos());

			appl->fsn->set_vpos((PNT){vp.x - p.x, vp.y - p.y});
			if ((p.x < -rectwidth(vrect)) ||
			    (p.x > rectwidth(vrect)) ||
			    (p.y < -rectheight(vrect)) ||
			    (p.y > rectheight(vrect))) {
				// 移動範囲がウィンドウ範囲を超える
				redisp(NULL);
			} else {
				// 移動範囲はウィンドウの表示範囲内
				PNT	zp;
				const	W	zfact = static_cast<const W>(appl->fsn->get_zfact());

				zp.x = (p.x * zfact) / 1000;
				zp.y = (p.y * zfact) / 1000;
				wscr_wnd(wid, NULL,zp.x,zp.y,W_MOVE | W_RDSET);
				disp_fn(0, NULL);
			}
			sbar_setup();
		}
	}

	return;
}


// ---------------------------------------------------- MAINWIN 内 private 関数
//
// 画像の表示
//
void	MAINWIN::disp_img(RECT dr)
{
	if (appl->img != NULL) {
		W	dgid;

		dgid = ((dr.c.left >= dr.c.right) || (dr.c.top >= dr.c.bottom)) ? -1 : gid;
		try {
			appl->img->disp_grph(dgid, vrect, dr, appl->fsn->get_vpos(), appl->fsn->get_zfact());
		} catch (...) {
			;		// 特に何もしない
		}
	}

	return;
}


//
// スクロールバーの値を設定する
//
void	MAINWIN::sbar_setup()
{
	if (appl->img != NULL) {
		W	val[4];
		const	RECT	sr = appl->img->get_srect();
		const	RECT	dr = appl->img->get_drect();

		// 右スクロールバー(垂直位置移動用)の設定
		val[0] = dr.c.top;
		val[1] = dr.c.bottom;
		val[2] = 0;
		val[3] = rectheight(sr);
		if (val[1] > val[3]) {
			val[1] = val[3];
		}
		cset_val(bar[0], 4, val);

		// 下スクロールバー(水平位置移動用)の設定
		val[0] = dr.c.right;
		val[1] = dr.c.left;
		val[2] = rectwidth(sr);
		val[3] = 0;
		if (val[0] > val[2]) {
			val[0] = val[2];
		}
		cset_val(bar[1], 4, val);
	}

	return;
}


//
// ウィンドウタイトルの設定
//
void	MAINWIN::set_wintit()
{
	if (appl->img != NULL) {
		// 基本タイトルの設定
		std::vector<TC>	tit(tc_strlen(wdef.title) + 1 + L_FNM + 6 + 1);

		tc_strset(tit.begin(), TNULL, tit.size());
		tc_strcpy(tit.begin(), wdef.title);

		// 実身名の付加
		TC	fname[L_FNM + 1];

		tit[tc_strlen(tit.begin())] = TK_COLN;
		fil_sts((LINK*)appl->get_link(), fname, NULL, NULL);
		tc_strncat(tit.begin(), fname, L_FNM);

		// 倍率の付加
		tit[tc_strlen(tit.begin())] = TK_LPAR;
		MISC::tc_numtostr(&tit[tc_strlen(tit.begin())], appl->fsn->get_zfact() / 10, 100);
		tit[tc_strlen(tit.begin())] = TK_PCNT;
		tit[tc_strlen(tit.begin())] = TK_RPAR;

		// 変更の反映
		wset_tit(wid, -1, tit.begin(), 0);
	}
 
	return;
}
