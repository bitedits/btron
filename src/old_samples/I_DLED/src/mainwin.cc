//
//	mainwin.cc (偽仮身一覧/主ウィンドウ操舵系)
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
#include	"vobjs.h"
#include	"fusen.h"
#include	"evtope.h"
#include	"mainwin.h"
#include	"pd_ope.h"
#include	"pd_nprs.h"
#include	"pd_hprs.h"
#include	"pd_vprs.h"
#include	"pd_vqp.h"
#include	"pd_clk.h"
#include	"pd_dclk.h"
#include	"kb_ope.h"
#include	"kb_can.h"
#include	"kb_del.h"
#include	"kb_move.h"
#include	"kb_scrl.h"
#include	"selfrm.h"
#include	"mainmenu.h"
#include	"ud_ope.h"
#include	"trayope.h"


// ----------------------------------------------------- MAINWIN 内 public 関数
//
// constructor
//
MAINWIN::MAINWIN()
	: wid(-1), gid(-1), selfrm(NULL)
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
	delete selfrm;
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
	// ピクトグラムの取得
	W	pcid;			// ピクトグラム ID

	pcid = (static_cast<W>(appl->eobj->get_atype()) & 0xff);
	if (pcid == 0) {
		pcid = CVAL::I_DLED_PICT;
	}

	// ウィンドウタイトルの生成
	TC	tit[L_FNM + 1 + 1 + 1];

	tc_strset(tit, TNULL, L_FNM + 1 + 1 + 1);
	tc_strncpy(tit, appl->eobj->get_fname(), L_FNM);
	tit[tc_strlen(tit)] = TK_COLN;
	tc_strncat(tit, wdef.title, 1);

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
	wid = wopn_wnd(atr, pwid, &vrect, (RECT*)pr, pcid, tit, &wbgpat, NULL);
	if (wid < ER_OK) {
		throw EXCEPT_MAINWIN(wid);
	}
	DPRINT(("wid : %d\n", wid));
	wget_bar(wid, &bar[0], &bar[1], NULL);

	// 表示周りの初期化

	gid = wget_gid(wid);
	gset_vis(gid, vrect);

	// 初期表示位置の反映
	const	PNT	vp = appl->fsn->get_vpos();

	wscr_wnd(wid, NULL, -vp.x, -vp.y, W_SCRL);
	gget_vis(gid, &vrect);

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
		redisp(NULL);
	}

	return;
}


//
// 選択枠関連
//
// type <  0 : 選択枠の廃棄(特に -2 の場合は選択枠の生成)
//      == 0 : 消灯
//      == 1 : 点灯(自 window が active であること)
//      >  1 : 点滅(自 window が active であること)
//
void	MAINWIN::disp_selfrm(W type)
{
	if (type < 0) {
		// 選択枠の廃棄
		delete selfrm;
		selfrm = NULL;

		if (type == -2) {
			// 選択枠の生成
			try {
				selfrm = new SELFRM(gid);
			} catch (EXCEPT_SELFRM) {
				;
			} catch (std::bad_alloc) {
				errpanel(DBOX::EPNL_MEMORY, ER_NOMEM);
			} catch (...) {
				;
			}
		}
	} else if (selfrm != NULL) {
		// 選択枠の表示操作
		if (type == 0) {
			// 消灯
			selfrm->off();
		} else if (wget_act(NULL) == wid) {
			// 自 window が active でないと、表示はしない
			if (type == 1) {
				// 点灯
				selfrm->on();
			} else {
				// 点滅
				selfrm->blink();
			}
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
	set_narea();			// 調節は先に行う
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
// 実作業領域とスクロールバーの設定
//
void	MAINWIN::upd_narea()
{
	set_narea();
	sbar_setup();

	return;
}


//
// (一時含む)トレー経由でのトレーから複写/移動の実行
//
// 返り値は error code か取り出した仮身/付箋の個数
//
W	MAINWIN::do_poptray(bool tmp, bool cut)
{
	W	rv;

	rv = 0;
	try {
		PNT	p;

		if (tmp) {
			p = ppos;
		} else {
			// menu 経由の場合は PD 位置が絶対座標になっている
			p = wevt.s.pos;
			gcnv_rel(gid, &p);
		}
		rv = TRAYOPE::pop_tray(false, tmp, cut, true, p);
		if ((rv <= 0) && (!(tmp))) {
			// error code は特殊形式のもの
			errpanel(DBOX::EPNL_POPTRAY, -10001);
		}
	} catch (EXCEPT_VOBJ& err) {
		rv = err.get_err();
		DPRINT(("%s : %d(%d)\n", err.what(), rv, rv >> 16));
		if ((rv == ER_NOMEM) || (rv == ER_NOSPC)) {
			errpanel(DBOX::EPNL_NEWVOBJ, rv);
			rv = 0;
		} else if (rv == ER_LIMIT) {
			errpanel(DBOX::EPNL_VOBJSKIP, rv);
			rv = 0;
		}
	} catch (EXCEPT_VOBJS& err) {
		rv = err.get_err();
		DPRINT(("%s : %d(%d)\n", err.what(), rv, rv >> 16));
		if ((rv == ER_NOMEM) || (rv == ER_NOSPC)) {
			errpanel(DBOX::EPNL_NEWVOBJ, rv);
			rv = 0;
		}
	} catch (std::bad_alloc) {
		DPRINT(("memory allocaiton error.\n"));
		errpanel(DBOX::EPNL_MEMORY, ER_NOMEM);
	} catch (...) {
		DPRINT(("other exception.\n"));
		rv = ER_SYS;
	}
	ppos = (PNT){0, 0};

	if (rv > 0) {
		// 選択枠の更新
		disp_selfrm(-2);
		disp_selfrm(1);
		upd_narea();
		appl->eobj->set_editflg(true);  // 更新
		if (tmp) {
			UD_OPE::make_undo(UD_OPE::UT_ADD);
		} else {
			UD_OPE::make_undo((cut) ? UD_OPE::UT_TRAYADD : UD_OPE::UT_ADD);
		}
	}

	return rv;
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
	appl->vobjs.dsp_vobj(-1, r);

	gset_vis(gid, vr);

	return;
}


//
// 定常処理(window event)
//
void	MAINWIN::idle_fn(bool pdflg)
{
	if (pdflg) {
		// PD 形状の設定
		W	pdtype;
		const	W	vid = appl->vobjs.fnd_posvobj(wevt.s.pos);

		if (wevt.s.stat & (ES_LSHFT | ES_RSHFT)) {
			// 修正選択指
			pdtype = PS_MODIFY;
		} else if (vid >= 0) {
			VOBJ*	vobj = const_cast<VOBJ*>(appl->vobjs.srch_vobj(vid));

			if (vobj->get_sel()) {
				// 選択状態
				W	vtype = ofnd_vob(-vid,wevt.s.pos,NULL);

				if (vobj->get_back()) {
					// 選択指(背景状態の上にある)
					// (背景状態は常に非選択なので、ここ
					//  にはこないはず)
					pdtype = PS_SELECT;
				} else if (vtype == V_PICT) {
					// 選択指(ピクトグラム上にある)
					pdtype = PS_SELECT;
				} else if (vobj->get_hold()) {
					// 移動手(固定状態の上にある)
					pdtype = PS_MOVE;
				} else if ((vtype == V_LTHD) ||
					   (vtype == V_RTHD) ||
					   (vtype == V_LBHD) ||
					   (vtype == V_RBHD)) {
					// 変形手(可変状態のハンドル上にある)
					pdtype = PS_RSIZ;
				} else {
					// 移動手(ハンドル外の仮身内)
					pdtype = PS_MOVE;
				}
			} else {
				// 選択指(非選択状態)
				pdtype = PS_SELECT;
			}
		} else {
			// 選択指
			pdtype = PS_SELECT;
		}

		// 形状の反映
		setpointer(pdtype, NULL);
	}

	// 選択枠のちらつき
	disp_selfrm(2);

	return;
}


//
// 表示処理(window event)
//
void	MAINWIN::disp_fn(W mode, RECT* newr)
{
	if (mode < 0) {
		// 初期化処理
		set_narea();
		redisp(NULL);
		sbar_setup();
		appl->vobjs.do_autoexec();	// 自動起動の実行
	} else {
		// その他の再描画処理
		disp_selfrm(0);
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
				redisp(NULL);
			} else {
				vrect = *newr;
			}
			gset_vis(gid, vrect);
			set_narea();
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
		disp_selfrm(1);
	}

	return;
}


//
// キー入力処理(window event)
//
W	MAINWIN::key_fn()
{
	W	rv;

	rv = 0;
	try {
		std::auto_ptr<KB_OPE>	ope;

		if (wevt.e.data.key.code == KC_CAN) {
			// [取り消し] 相当とする
			ope = std::auto_ptr<KB_OPE>(new KB_CAN());
		} else if (wevt.e.data.key.code == KC_DEL) {
			// [削除] 相当とする
			ope = std::auto_ptr<KB_OPE>(new KB_DEL());
		} else if ((appl->vobjs.get_selvid() >= 0) &&
			   ((wevt.s.stat & (ES_BUT2 | ES_CMD)) == 0x0000) &&
			   ((wevt.e.data.key.code == KC_CC_U) ||
			    (wevt.e.data.key.code == KC_CC_D) ||
			    (wevt.e.data.key.code == KC_CC_L) ||
			    (wevt.e.data.key.code == KC_CC_R))) {
			// 選択中の仮身の移動
			ope = std::auto_ptr<KB_OPE>(new KB_MOVE(wevt.e.data.key.code, wevt.s.stat));
		} else {
			// 表示のスクロール
			ope = std::auto_ptr<KB_OPE>(new KB_SCRL(wevt.e.data.key.code, wevt.s.stat));
		}

		// 操作の実行
		ope->main();
	} catch (EXCEPT_KBOPE& err) {
		rv = err.get_err();
		DPRINT(("%s : %d(%d)\n", err.what(), rv, rv >> 16));
		if (rv == I_DLEDERR::KB_NOOPE) {
			// 操作は実行されなかった
			rv = 0;
		}
	} catch (std::bad_alloc) {
		errpanel(DBOX::EPNL_MEMORY, ER_NOMEM);
	} catch (...) {
		rv = ER_SYS;
	}

	return rv;
}


//
// 主ボタンプレス処理(window event)
//
W	MAINWIN::press_fn()
{
	W	rv;

	rv = 0;
	try {
		// PD 位置・操作方法に応じた、操作系 object の生成
		std::auto_ptr<PD_OPE>	ope;
		const	W	vid = appl->vobjs.fnd_posvobj(wevt.s.pos);

		if ((vid >= 0) &&
		    ((wevt.s.stat & (ES_LSHFT | ES_RSHFT)) == 0x0000)) {
			// 修正選択状態ではなく、且つ仮身上のどこか
			VOBJ*	vobj = const_cast<VOBJ*>(appl->vobjs.srch_vobj(vid));

			if ((!(vobj->get_back())) && (!(vobj->get_sel()))) {
				// 背景化されていないなら選択状態を先に設定
				disp_selfrm(0);
				appl->vobjs.set_allsel(false, false);
				vobj->set_sel(true);
				disp_selfrm(-2);
				disp_selfrm(1);
			}

			// PD 操作種別の取得/分岐
			W	type;

			type = wchk_dck(wevt.s.time);
			if ((type == W_PRESS) || (type == W_QPRESS)) {
				// press か quick press
				if (vobj->get_back()) {
					DPRINT(("background vobj press.\n"));
					ope = std::auto_ptr<PD_OPE>(new PD_NPRS());
				} else {
					W	vtype = ofnd_vob(-vid, wevt.s.pos, NULL);

					if ((!(vobj->get_hold())) &&
					    ((vtype == V_LTHD) ||
					     (vtype == V_RTHD) ||
					     (vtype == V_LBHD) ||
					     (vtype == V_RBHD))) {
						// handle をつかんでいる
						DPRINT(("handle press.\n"));
						ope = std::auto_ptr<PD_OPE>(new PD_HPRS(vid, vtype));
					} else if (type == W_PRESS) {
						DPRINT(("vobj press.\n"));
						// press でつかんでいる
						ope = std::auto_ptr<PD_OPE>(new PD_VPRS(vid));
					} else {
						// quick press でつかんでいる
						DPRINT(("vobj quick press.\n"));
						ope = std::auto_ptr<PD_OPE>(new PD_VQP(vid));
					}
				}
			} else if (type == W_CLICK) {
				// click
				DPRINT(("click.\n"));
				ope = std::auto_ptr<PD_OPE>(new PD_CLK());
			} else if (type == W_DCLICK) {
				// double click
				DPRINT(("double click.\n"));
				ope = std::auto_ptr<PD_OPE>(new PD_DCLK());
			} else {
				// 不定(操作は発生しない)
				throw EXCEPT_MAINWIN(0);	// 下に投げる
			}
		} else {
			// 仮身ではない(ウィンドウの地の部分)か、修正選択
			W	type;

			type = wchk_dck(wevt.s.time);
			if ((type == W_PRESS) || (type == W_QPRESS)) {
				DPRINT(("workarea press.\n"));
				ope = std::auto_ptr<PD_OPE>(new PD_NPRS());
			} else if (type == W_CLICK) {
				// click
				DPRINT(("click.\n"));
				ope = std::auto_ptr<PD_OPE>(new PD_CLK());
			} else if (type == W_DCLICK) {
				// double click
				DPRINT(("double click.\n"));
				ope = std::auto_ptr<PD_OPE>(new PD_DCLK());
			} else {
				// 不定(操作は発生しない)
				throw EXCEPT_MAINWIN(0);	// 下に投げる
			}
		}

		// 操作の実行
		ope->main();
	} catch (EXCEPT_PDOPE& err) {
		rv = err.get_err();
		DPRINT(("%s : %d(%d)\n", err.what(), rv, rv >> 16));
		if (rv == I_DLEDERR::PD_NOOPE) {
			// 操作は実行されなかった
			rv = 0;
		}
	} catch (EXCEPT_MAINWIN) {
		;			// 何もしない
	} catch (EXCEPT_VOBJ& err) {
		rv = err.get_err();
		DPRINT(("%s : %d(%d)\n", err.what(), rv, rv >> 16));
		if (rv == ER_LIMIT) {
			// 制限抵触
			errpanel(DBOX::EPNL_NEWOBJ, rv);
			rv = ER_OK;
		}
	} catch (EXCEPT_VOBJS& err) {
		rv = err.get_err();
		DPRINT(("%s : %d(%d)\n", err.what(), rv, rv >> 16));
		if (rv == ER_LIMIT) {
			// 制限抵触
			errpanel(DBOX::EPNL_NEWVOBJ, rv);
			rv = ER_OK;
		}
	} catch (std::bad_alloc) {
		errpanel(DBOX::EPNL_MEMORY, ER_NOMEM);
	} catch (...) {
		rv = ER_SYS;
	}

	return rv;
}


//
// 貼り込み要求処理(window event)
//
W	MAINWIN::paste_fn(PNT pos)
{
	W	rv;
	PNT	p;

	rv = W_NAK;
	p = wevt.s.pos;

	try {
		if (TRAYOPE::pop_tray(true, true, false, false, p) > 0) {
			ppos = p;	// 貼り込み位置
			rv = W_ACK;
		}
	} catch (...) {
		;			// 失敗したら W_NAK 応答
	}

	return rv;
}


//
// スクロール処理(window event)
//
void	MAINWIN::scroll_fn(W type, W diff)
{
	W	diff2;
	PNT	p;
	const	RECT	varea = appl->fsn->get_area();
	 
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
	p = chk_scroll(type, diff, diff2, vrect, varea);

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
	const	RECT	varea = appl->fsn->get_area();

	// 右スクロールバー(垂直位置移動用)の設定
	val[0] = vrect.c.top;
	val[1] = vrect.c.bottom;
	val[2] = 0;
	val[3] = rectheight(varea);
	if (val[1] > val[3]) {
		val[1] = val[3];
	}
	cset_val(bar[0], 4, val);

	// 下スクロールバー(水平位置移動用)の設定
	val[0] = vrect.c.right;
	val[1] = vrect.c.left;
	val[2] = rectwidth(varea);
	val[3] = 0;
	if (val[0] > val[2]) {
		val[0] = val[2];
	}
	cset_val(bar[1], 4, val);

	return;
}


//
// 仮身の配置領域と実作業領域の調節
//
void	MAINWIN::set_narea()
{
	RECT	narea = appl->fsn->get_area();
	const	RECT	varea = appl->vobjs.get_allarea();

	gget_vis(gid, &vrect);
	narea.c.right = (vrect.c.right > varea.c.right) ? vrect.c.right : varea.c.right;
	narea.c.bottom = (vrect.c.bottom > varea.c.bottom) ? vrect.c.bottom : varea.c.bottom;
	appl->fsn->set_area(narea);

	return;
}
