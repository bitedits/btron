//
//	mainwin.cc (簡易文字列編集/主ウィンドウ操舵系)
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
#include	"editobj.h"
#include	"strope.h"
#include	"fusen.h"
#include	"evtope.h"
#include	"mainwin.h"
#include	"textin.h"
#include	"caretope.h"
#include	"mainmenu.h"


// ----------------------------------------------------- MAINWIN 内 public 関数
//
// constructor
//
MAINWIN::MAINWIN()
	: wid(-1), gid(-1)
{
	// ウィンドウ定義データの取り出し
	ConvEndianStruct(&wdef, getdbox(DBOX::DEF_MAINWIN), WINDEF_STRUCT, sizeof(wdef));

	// その他の初期化処理
	ppos = (PNT){0, 0};
	genrectlist(4, rlist);
}


//
// destructor
//
MAINWIN::~MAINWIN()
{
	if (wid >= 0) {
		wcls_wnd(wid, CLR);
	}
	EVTOPE::winfo[CVAL::WIDX_MAINWIN].wid = 0;
}


//
// ウィンドウを開ける
//
void	MAINWIN::open_win(const W pwid, const RECT* pr)
{
	// ピクトグラムの取得
	W	pcid;			// ピクトグラム ID

	pcid = (static_cast<W>(appl->eobj->get_atype()) & 0xff);
	if (pcid == 0) {
		pcid = CVAL::T_TXEDIT_PICT;
	}

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
	set_wbgpat(false);
	wid = wopn_wnd(atr, pwid, &vrect, (RECT*)pr, pcid, (TC*)appl->eobj->get_fname(), &wbgpat, NULL);
	if (wid < ER_OK) {
		throw EXCEPT_MAINWIN(wid);
	}
	DPRINT(("wid : %d\n", wid));
	wget_bar(wid, &bar[0], &bar[1], NULL);

	// 表示周りの初期化
	gid = wget_gid(wid);
	gset_vis(gid, vrect);
	set_char();

	// 初期表示位置の反映
	const	PNT	vp = appl->fsn->get_vpos();

	wscr_wnd(wid, NULL, -vp.x, -vp.y, W_SCRL);
	gget_vis(gid, &vrect);

	// 追従スクロールにする
	cchg_par(bar[0], P_DRAGBREAK);
	cchg_par(bar[1], P_DRAGBREAK);

	// 文字列入力周りの初期化
	try {
		tin = std::auto_ptr<TEXTIN>(new TEXTIN(wid, CVAL::TIP_MODE));
		car = std::auto_ptr<CARETOPE>(new CARETOPE(tin->get_tport()));
	} catch (EXCEPT_TEXTIN& err) {
		DPRINT(("%s : %d(%d)\n", err.what(), err.get_err(), err.get_err() >> 16));
		throw EXCEPT_MAINWIN(err.get_err());
	} catch (std::bad_alloc) {
		DPRINT(("memory allocation error.\n"));
		throw EXCEPT_MAINWIN(ER_NOMEM);
	} catch (...) {
		DPRINT(("other exception.\n"));
		throw EXCEPT_MAINWIN(ER_SYS);
	}

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
// ウィンドウ背景色を設定する
//
void	MAINWIN::set_wbgpat(bool set)
{
	// 基本部分の設定
	wbgpat.spat.kind = 0;
	wbgpat.spat.hsize = 16;
	wbgpat.spat.vsize = 16;
	wbgpat.spat.bgcol = CVAL::RGB_WHITE;

	// 付箋固有データ中の内容の反映
	const	WINCOL	wcol = appl->fsn->get_wcol();

	if (wcol.mask == 0) {
		// 仮身背景色を適応
		wbgpat.spat.fgcol = appl->get_bgcol();
		wbgpat.spat.mask = FILL100;
	} else {
		// 付箋固有データ内の値を反映
		wbgpat.spat.fgcol = wcol.col;
		wbgpat.spat.mask = reinterpret_cast<UB*>(wcol.mask & 0x0007);
	}

	if (set) {
		// すぐに反映させる
		wset_bgp(wid, &wbgpat);
		set_char();
		redisp(NULL);
	}

	return;
}


//
// 描画書体に関する設定
//
void	MAINWIN::set_char()
{
	const	WINATTR	wattr = appl->fsn->get_wattr();
	COLOR	fgcol;			// 文字前景色
	COLOR	bgcol;			// 文字背景色
	FSSPEC	fspec = {
			{TNULL}, FTC_DEFAULT, 0x0000, {CHSSTD, CHSSTD}
		};

	fgcol = CVAL::RGB_BLACK;
	if (wattr.gray) {
		bgcol = CVAL::COL_GRAY | wbgpat.spat.fgcol;
		fspec.attr |= FT_GRAYSCALE;
	} else {
		bgcol = CVAL::COL_TRANS;
	}

	gset_fon(gid, &fspec);
	gset_chc(gid, fgcol, bgcol);

	return;
}


//
// caret 関連
//
// type == 0 : 消灯
//	== 1 : 点灯(自 window が active であること)
//	>  1 : 点滅(自 window が active であること)
//
void	MAINWIN::disp_caret(W type)
{
	if (type == 0) {
		// 消灯
		car->off();
	} else if (wget_act(NULL) == wid) {
		// 自 window が active でないと、表示はしない
		if (type == 1) {
			// 点灯
			car->on();
		} else {
			// 点滅
			car->blink();
		}
	}

	return;
}


//
// 表示をスクロールさせる
//
void	MAINWIN::scroll_work(PNT p)
{
	W	rv;

	rv = wscr_wnd(wid, NULL, p.x, p.y, W_SCRL | W_RDSET);
	gget_vis(gid, &vrect);		// scroll に伴う移動の取り出し
	if ((rv >= ER_OK) && (rv & W_RDSET)) {
		// scroll に伴う再描画
		disp_fn(0, NULL);
	}

	// 表示位置を付箋に覚える
	appl->fsn->set_vpos(vrect.p.lefttop);

	// scroll bar の更新
	sbar_setup();

	return;
}


//
// 再描画処理
//
void	MAINWIN::redisp(RECT* r)
{
	RECT	vr;

	gget_vis(gid, &vr);

	// 再描画
	wera_wnd(wid, r);
	if (r != NULL) {
		gset_vis(gid, *r);
	}
	appl->estr.disp_text(gid, vrect, -1);

	gset_vis(gid, vr);

	return;
}


//
// 定常処理(window event)
//
void	MAINWIN::idle_fn(bool pdflg)
{
	if (pdflg) {
		// 形状の設定(常に選択指)
		setpointer(PS_SELECT, NULL);
	}

	// caret のちらつき
	disp_caret(2);

	return;
}


//
// 表示処理(window event)
//
void	MAINWIN::disp_fn(W mode, RECT* newr)
{
	if (mode < 0) {
		// 初期化処理
		appl->estr.cnv_pdpos(appl->fsn->get_vpos());
		car->move(appl->estr.cnv_crpos(), false);
		car_scroll(false);
		redisp(NULL);
		sbar_setup();
	} else {
		// その他の再描画処理
		disp_caret(0);
		if (newr != NULL) {
			// サイズが変わった
			if ((newr->c.left < CVAL::LIMIT_RECT.c.left) ||
			    (newr->c.top < CVAL::LIMIT_RECT.c.top)) {
				// 左上が制限以下になる
				vrect.c.right = rectwidth(*newr);
				vrect.c.bottom = rectheight(*newr);
				vrect.p.lefttop = (PNT){0, 0};
				wset_wrk(wid, &vrect);
				do {
					wsta_dsp(wid, NULL, NULL);
				} while (wend_dsp(wid) > 0);
				gset_vis(gid, vrect);
				redisp(NULL);
			} else {
				vrect = *newr;
				gset_vis(gid, vrect);
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
		appl->fsn->set_vpos(vrect.p.lefttop);
		sbar_setup();
		disp_caret(1);
	}

	return;
}


//
// キー入力処理(window event)
//
W	MAINWIN::key_fn()
{
	W	rv;
	W	sts;			// text_in の動作結果
	PNT	pepos;
	TEXTPORT*	tport = tin->get_tport();

	rv = 0;

	// かな漢字変換を行う時の表示座標の設定
	tport->left = vrect.c.left;
	tport->right = vrect.c.right;
	tport->spos = appl->estr.cnv_crpos();
	pepos = tport->spos;

	// text_in の実行
	do {
		// text_in に event を渡す
		sts = read_text(tport, &wevt);

		// 動作結果の判定
		if (sts & TXT_NOIMG) {
			// 再描画が必要になった
			RECT	dr;

			if (tport->epos.x < pepos.x) {
				dr.c.left = tport->epos.x;
				dr.c.right = pepos.x;
			} else {
				dr.c.left = pepos.x;
				dr.c.right = tport->epos.x;
			}
			if (tport->epos.y < pepos.y) {
				dr.c.top = tport->epos.y;
				dr.c.bottom = pepos.y;
			} else {
				dr.c.top = pepos.y;
				dr.c.bottom = tport->epos.y;
			}
			redisp(&dr);
			if (sts & TXT_CNV) {
				redisp_text(tport);
			}
		}
		pepos = tport->epos;

		if (sts & TXT_EVT) {
			// その他の event を受けたので差し戻し
			// (text_in は確定済みに移行している)
			unget_wevt();
		}

		if (sts & TXT_OUT) {
			// 確定文字列が発生した
			ins_str(tport->cnv, tport->n_cnv);
		} else if (sts & TXT_SCRL) {
			// スクロール要求が発生した
			kcnv_scroll(tport);
		}

		if (sts & TXT_KEY) {
			// その他のキー入力が行われた
			// (text_in は確定済みに移行している)
			TC	code;

			code = wevt.e.data.key.code;
			if ((wevt.s.stat & ES_CMD) && (code > KC_SPACE)) {
				// [命令] 系なら差し戻し
				unget_wevt();
			} else if ((code == KC_CC_L) || (code == KC_CC_R) ||
			           (code == KC_CC_U) || (code == KC_CC_D)) {
				act_KC_CC(code);
			} else if (code == KC_BS) {
				act_KC_BS();
			} else if ((code == KC_NL) || (code == KC_CR)) {
				ins_str(&code, 1);
			}
		}
	} while (sts & TXT_CNV);

	return rv;
}


//
// 主ボタンプレス処理(window event)
//
W	MAINWIN::press_fn()
{
	appl->estr.cnv_pdpos(wevt.s.pos);
	car->move(appl->estr.cnv_crpos(), true);
	car_scroll(true);

	return 0;			// 常時 0
}


//
// スクロール処理(window event)
//
void	MAINWIN::scroll_fn(W type, W diff)
{
	W	diff2;
	PNT	p;
	const	SIZE	lsize = appl->estr.get_layarea();
	 
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
	p = chk_scroll(type, diff, diff2, vrect, (RECT){{0, 0, lsize.h, lsize.v}});

	// 移動量が 0 の時はなにもしない
	if ((p.x != 0) || (p.y != 0)) {
		scroll_work(p);
	}

	return;
}


// ---------------------------------------------------- MAINWIN 内 private 関数
//
// スクロールバーの値を設定する
//
void	MAINWIN::sbar_setup()
{
	W	val[4];
	const	SIZE	lsize = appl->estr.get_layarea();

	// 右スクロールバー(垂直位置移動用)の設定
	val[0] = vrect.c.top;
	val[1] = vrect.c.bottom;
	val[2] = 0;
	val[3] = lsize.v;
	if (val[1] > val[3]) {
		val[1] = val[3];
	}
	cset_val(bar[0], 4, val);

	// 下スクロールバー(水平位置移動用)の設定
	val[0] = vrect.c.right;
	val[1] = vrect.c.left;
	val[2] = lsize.h;
	val[3] = 0;
	if (val[0] > val[2]) {
		val[0] = val[2];
	}
	cset_val(bar[1], 4, val);

	return;
}


//
// caret が見えるように scroll する
//
void	MAINWIN::car_scroll(bool hscrl)
{
	// caret 表示(疑似)矩形枠の算出
	RECT	cr;
	const	PNT	cp = appl->estr.cnv_crpos();

	cr.c.left = cp.x - CVAL::CH_HGAP;
	cr.c.top = cp.y - CHSSTD - CVAL::CH_VGAP;
	cr.c.right = cp.x + CVAL::CH_HGAP;
	cr.c.bottom = cp.y + CVAL::CH_VGAP;

	// 各移動量の算出
	PNT	sp;

	if (hscrl) {
		// 水平移動量の算出
		if (cr.c.left < vrect.c.left) {
			sp.x = vrect.c.left - cr.c.left + CVAL::CH_HGAP;
		} else if (cr.c.right > vrect.c.right) {
			sp.x = vrect.c.right - cr.c.right - CVAL::CH_HGAP;
		} else {
			sp.x = 0;
		}
	} else {
		sp.x = 0;
	}

	// 垂直移動量の算出
	if (cr.c.top < vrect.c.top) {
		sp.y = vrect.c.top - cr.c.top + CVAL::CH_VGAP;
	} else if (cr.c.bottom > vrect.c.bottom) {
		sp.y = vrect.c.bottom - cr.c.bottom - CVAL::CH_VGAP;
	} else {
		sp.y = 0;
	}

	// scroll の発行
	if ((sp.x != 0) || (sp.y != 0)) {
		scroll_work(sp);
	}

	return;
}


//
// かな漢字変換中での scroll 要求
//
void	MAINWIN::kcnv_scroll(TEXTPORT* tport)
{
	// scroll 量の算出
	PNT	sp;

	if (tport->car->pos.x < vrect.c.left) {
		sp.x = vrect.c.left - tport->car->pos.x + CVAL::CH_HGAP;
	} else if (tport->car->pos.x > vrect.c.right) {
		sp.x = vrect.c.right - tport->car->pos.x - CVAL::CH_HGAP;
	} else {
		sp.x = 0;
	}
	if (tport->car->pos.y < vrect.c.top) {
		sp.y = vrect.c.top - tport->car->pos.y + CVAL::CH_VGAP;
	} else if (tport->car->pos.y > vrect.c.bottom) {
		sp.y = vrect.c.bottom - tport->car->pos.y - CVAL::CH_VGAP;
	} else {
		sp.y = 0;
	}
	scroll_work(sp);

	return;
}


//
// 確定文字列の挿入
//
void	MAINWIN::ins_str(TC* str, W len)
{
	// 入力文字列の取得
	W	l;
	TC*	ptr;
	TLANG	lng;
	std::vector<WTC>	istr;

	lng = TSC_SYS;
	for (l = 0; l < len; ++l) {
		TC	ch;

		ch = *str;
		if ((ch & TC_SPEC) == TC_LANG) {
			// 言語/スクリプト指定の取得
			lng = isTLANG(str, l - len, &str);
		} else if ((ch == TC_NL) || (ch == TC_CR) || (ch >= 0x2121)) {
			// 一般文字列/改段落/改行
			istr.push_back(towtc(lng, ch));
			++str;
		}
	}

	// 文字列の挿入
	PNT	cp1, cp2;	// 挿入前後のcaret位置
	RECT	cr;

	cr = vrect;
	cp1 = appl->estr.cnv_crpos();
	appl->estr.ins_text(istr.begin(), istr.size());
	cp2 = appl->estr.cnv_crpos();
	cr.c.top = cp2.y - CHSSTD;
	car->off();
	if (cp1.y != cp2.y) {
		// 一つ上の行から
		cr.c.top = cp1.y - CHSSTD;
		wera_wnd(wid, &cr);
		appl->estr.cnv_pdpos(cp1);	// 前の場所に戻る
		appl->estr.disp_text(gid, vrect, 1);
		appl->estr.cnv_pdpos(cp2);	// 挿入後の場所に戻す
	} else {
		// 行のみ
		cr.c.bottom = cp2.y + CVAL::CH_VGAP;
		wera_wnd(wid, &cr);
		appl->estr.disp_text(gid, vrect, 0);
	}

	// caret 位置の更新
	car->move(appl->estr.cnv_crpos(), true);
	car_scroll(true);

	appl->eobj->set_editflg(true);	// 更新

	return;
}


//
// KC_CC_L/R/U/D の挙動(ES_CMD 同時押し含まず)
//
void	MAINWIN::act_KC_CC(TC code)
{
	W	type;

	switch (code) {
		case KC_CC_L:
			type = 0;
			break;
		case KC_CC_R:
			type = 1;
			break;
		case KC_CC_U:
			type = 2;
			break;
		case KC_CC_D:
			type = 3;
			break;
		default:
			type = -1;
			break;
	}

	if (type >= 0) {
		// caret の移動
		appl->estr.move_cidx(type);
		car->move(appl->estr.cnv_crpos(), true);
		car_scroll(true);
	}

	return;
}


//
// KC_BS の挙動
//
void	MAINWIN::act_KC_BS()
{
	PNT	cp1, cp2;	// 消去前後のcaret位置
	RECT	cr;

	cr = vrect;
	cp1 = appl->estr.cnv_crpos();
	appl->estr.del_text();
	cp2 = appl->estr.cnv_crpos();
	cr.c.top = cp2.y - CHSSTD;
	car->off();
	if (cp1.y != cp2.y) {
		// 以降全体
		wera_wnd(wid, &cr);
		appl->estr.disp_text(gid, vrect, 1);
	} else {
		// 行のみ
		cr.c.bottom = cp2.y + CVAL::CH_VGAP;
		wera_wnd(wid, &cr);
		appl->estr.disp_text(gid, vrect, 0);
	}

	// caret 位置の更新
	car->move(appl->estr.cnv_crpos(), true);
	car_scroll(true);

	appl->eobj->set_editflg(true);	// 更新

	return;
}
