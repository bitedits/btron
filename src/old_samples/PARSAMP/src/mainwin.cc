//
//	mainwin.cc (パーツ操作例/主ウィンドウ操舵系)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//
//	-DSB_CEXE_USE : SB_PARTS の実行に cexe_par() を使用する
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
#include	<tlang.h>
#include	<tstring.h>

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
#include	"fusen.h"
#include	"evtope.h"
#include	"mainwin.h"
#include	"mainmenu.h"
#include	"trayope.h"


// ----------------------------------------------------- MAINWIN 内 public 関数
//
// constructor
//
MAINWIN::MAINWIN()
	: wid(-1), gid(-1), actbox(PTIDX_TB_STR1), pactbox(-1)
{
	// ウィンドウ定義データの取り出し
	ConvEndianStruct(&wdef, getdbox(DBOX::DEF_MAINWIN), WINDEF_STRUCT, sizeof(wdef));

	// パーツ定義データの取り出し(登録はまだしない)
	W	l;
	PNL_ITEM*	ptr;

	ptr = reinterpret_cast<PNL_ITEM*>(ptrdbox(wdef.parts));
	for (l = 0; l < wdef.pnum; ++l) {
		ConvEndianStruct(&parts[l], &ptr[l], PNL_ITEM_STRUCT, sizeof(PNL_ITEM));
		parts[l].desc = 0;
		if (parts[l].ptr != 0) {
			parts[l].ptr = reinterpret_cast<H*>(ptrdbox((W)parts[l].ptr));
		}
		adjscalr(&parts[l].ir);	// 座標変換は先に行う
	}

        // パーツ操舵関数テーブルの初期化
	par_fn[PTIDX_STR_STR] = NULL;
	par_fn[PTIDX_TB_STR1] = &MAINWIN::p_tb_str1_fn;
	par_fn[PTIDX_TB_STR2] = &MAINWIN::p_tb_str2_fn;
	par_fn[PTIDX_AS_ENABLE] = &MAINWIN::p_as_enable_fn;
	par_fn[PTIDX_STR_COLOR] = NULL;
	par_fn[PTIDX_SS_COLOR] = &MAINWIN::p_ss_color_fn;
	par_fn[PTIDX_STR_SIZE] = NULL;
	par_fn[PTIDX_VL_SIZE] = &MAINWIN::p_vl_size_fn;
	par_fn[PTIDX_SB_SIZE] = &MAINWIN::p_sb_size_fn;
	par_fn[PTIDX_STR_PITCH] = NULL;
	par_fn[PTIDX_WS_PITCH] = &MAINWIN::p_ws_pitch_fn;
	par_fn[PTIDX_MS_INIT] = &MAINWIN::p_ms_init_fn;

	// その他の初期化処理
	genrectlist(4, rlist);
	ppos = (PNT){0, 0};
}


//
// destructor
//
MAINWIN::~MAINWIN()
{
	if (wid >= 0) {
		cdel_pwd(wid, CLR);
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
	// ウィンドウの場所を設定する
	const	PNT	p = appl->fsn->get_wpos();

	vrect.c.left = wdef.r.c.left + p.x;
	vrect.c.top = wdef.r.c.top + p.y;
	vrect.c.right = wdef.r.c.right + p.x;
	vrect.c.bottom = wdef.r.c.bottom + p.y;

	// ウィンドウを開ける
	wid = wopn_wnd(wdef.attr, pwid, &vrect, (RECT*)pr, CVAL::PARSAMP_PICT, wdef.title, (PAT*)WI_PANELBACK, NULL);
	if (wid < ER_OK) {
		throw EXCEPT_MAINWIN(wid);
	}
	DPRINT(("wid : %d\n", wid));

	// 表示周りの初期化
	COLOR	bgcol = CVAL::COL_TRANS;

	gid = wget_gid(wid);
	gset_vis(gid, vrect);
	wget_inf(WI_GSBGC(WI_PANELBACK), &bgcol, sizeof(bgcol));
	gset_chc(gid, CVAL::RGB_BLACK, bgcol);

	// ウィンドウ情報レコードに登録
	EVTOPE::winfo[CVAL::WIDX_MAINWIN].wid = wid;
	EVTOPE::winfo[CVAL::WIDX_MAINWIN].chgmod = 0x20;
	EVTOPE::winfo[CVAL::WIDX_MAINWIN].limit = (RECT){{0, 0, 0, 0}};
	EVTOPE::winfo[CVAL::WIDX_MAINWIN].pid[0] = -1;
	EVTOPE::winfo[CVAL::WIDX_MAINWIN].pid[1] = -1;
	EVTOPE::winfo[CVAL::WIDX_MAINWIN].pid[2] = -1;

	return;
}


//
// ウィンドウを閉じる
//
void	MAINWIN::close_win()
{
	cdel_pwd(wid, CLR);
	wcls_wnd(wid, CLR);
	wid = -1;
	EVTOPE::winfo[CVAL::WIDX_MAINWIN].wid = 0;

	return;
}


//
// パーツを生成する
//
void	MAINWIN::par_create()
{
	W	l;
	SWSEL*	sw;
 
	for (l = 0; l < wdef.pnum; ++l) {
		// PARTS_ITEM 以外は無視
		if ((parts[l].itype & 0x000f) != PARTS_ITEM) {
			continue;
		}
 
		// パーツの登録
		sw = reinterpret_cast<SWSEL*>(parts[l].ptr);
		switch (sw->type & P_TYPE) {
			case 4:		// SB_PARTS
				offtoptr((UW*)&(((SERBOX*)sw)->fmt));
				offtoptr((UW*)&(((SERBOX*)sw)->cv));
				break;
			case 1:		// TB_PARTS
			case 2:		// XB_PARTS
			case 5:		// AS_PARTS
			case 6:		// MS_PARTS
			case 9:		// WS_PARTS
			case 10:	// SS_PARTS
				offtoptr((UW*)&(sw->name));
				break;
#if	0				// 変換不要
			case 3:		// NB_PARTS
			case 7;		// PA_PARTS
			case 8:		// PM_PARTS
			case 11:	// VL_PARTS
				break;
#endif	// 0
		}
		adjscalr(&sw->r);
		parts[l].desc = ccre_par(wid, (PARTS*)sw);
		if (parts[l].desc < ER_OK) {
			throw EXCEPT_MAINWIN(parts[l].desc);
		}
		DPRINT(("parts[%d] : %d\n", l, parts[l].desc));
 
		// 位置を移動
		parts[l].ir.c.right = parts[l].ir.c.left + rectwidth(sw->r);
		parts[l].ir.c.bottom = parts[l].ir.c.top + rectheight(sw->r);
		cset_pos(parts[l].desc, &parts[l].ir);

		// VL_PARTS を P_DRAGBREAK にしておく
		if ((sw->type & P_TYPE) == VL_PARTS) {
			cchg_par(parts[l].desc, P_DRAGBREAK);
		}
	}

	return;
}


//
// パーツを廃棄する
//
void	MAINWIN::par_destroy()
{
	W	l;

	for (l = 0; l < wdef.pnum; ++l) {
		if (parts[l].desc >= 0) {
			cdel_par(parts[l].desc, CLR);
			parts[l].desc = 0;
		}
	}

	return;
}


//
// 設定を初期値に戻す
//
void	MAINWIN::init_data()
{
	const	PNT	wp = appl->fsn->get_wpos();

	appl->fsn->init_fusen();
	appl->fsn->set_wpos(wp);
	par_setup(PTMSK_ALL);

	return;
}


//
// トレーに複写/移動できる内容があるか確認する
//
bool	MAINWIN::chk_totray()
{
	W	len;

	len = 0;
	if ((actbox == PTIDX_TB_STR1) || (actbox == PTIDX_TB_STR2)) {
		len = ccut_txt(parts[actbox].desc, 0, NULL, 0);
	}

	return (len > 0);
}


//
// 選択文字列を削除する
//
void	MAINWIN::del_string()
{
	if ((actbox == PTIDX_TB_STR1) || (actbox == PTIDX_TB_STR2)) {
		TC	dmy;

		ccut_txt(parts[actbox].desc, 1, &dmy, 1);
	}

	return;
}


//
// (一時含む)トレー経由でのトレーへ複写/移動の実行
//
void	MAINWIN::do_pushtray(bool tmp, bool cut)
{
	if ((actbox == PTIDX_TB_STR1) || (actbox == PTIDX_TB_STR2)) {
		TRAYOPE::push_tray(parts[actbox].desc, tmp, cut);
	}

	return;
}


//
// (一時含む)トレー経由でのトレーから複写/移動の実行
//
void	MAINWIN::do_poptray(bool tmp, bool cut)
{
	if ((pactbox == PTIDX_TB_STR1) || (pactbox == PTIDX_TB_STR2)) {
		// 一時トレー経由の状態だったので、候補を取り出す
		actbox = pactbox;
	}

	if ((actbox == PTIDX_TB_STR1) || (actbox == PTIDX_TB_STR2)) {
		W	len;

		len = TRAYOPE::pop_tray(parts[actbox].desc, tmp, cut, ppos);
		if ((len <= 0) && (!(tmp))) {
			errpanel(DBOX::EPNL_POPTRAY, 0);
		}
	}

	// 一時トレー経由時の情報は初期化しておく
	ppos = (PNT){0, 0};
	pactbox = -1;

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
	cdsp_pwd(wid, r, P_RDISP);
	disp_string();

	gset_vis(gid, vr);

	return;
}


//
// 定常処理(window event)
//
void	MAINWIN::idle_fn()
{
	// 常時実行されているべきボックス系パーツを実行する
	if (actbox >= 0) {
		if (this->*par_fn[actbox] != NULL) {
			(this->*par_fn[actbox])(parts[actbox].desc, actbox);
		}
	}

	return;
}


//
// 表示処理(window event)
//
void	MAINWIN::disp_fn(W mode, RECT* newr)
{
	if (mode < 0) {
		// 初期化処理
		par_setup(PTMSK_ALL);
		redisp(NULL);
	} else {
		// その他の再描画処理
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
		if ((mode == 1) || (mode == 8) || (mode == 9)) {
			// 位置が移動した
			WDSTAT  wstat;

			wget_sts(wid, &wstat, NULL);
			appl->fsn->set_wpos(wstat.r.p.lefttop);
		}
	}

	return;
}


//
// キー入力処理(window event)
//
W	MAINWIN::key_fn()
#ifdef	SB_CEXE_USE			// cexe_par() を使用する
{
	W	rv;

	rv = ER_OK;
	if (actbox == PTIDX_SB_SIZE) {
		rv = (this->*par_fn[actbox])(parts[actbox].desc, actbox);
	}

	return rv;
}
#else	// SB_CEXE_USE			// cexe_par() を使用しない
{
	// 特になにもしない

	return 0;			// 常時 0
}
#endif	// SB_CEXE_USE


//
// 主ボタンプレス処理(window event)
//
W	MAINWIN::press_fn()
{
	W	rv;
	W	pid;
 
	rv = ER_OK;
	cfnd_par(wid, wevt.s.pos, &pid);
	if (pid != 0) {
		// 何かのパーツ上
		W	l;

		for (l = 0; l < wdef.pnum; ++l) {
			if (pid == parts[l].desc) {
				if (this->*par_fn[l] != NULL) {
					rv = (this->*par_fn[l])(pid, l);
				}
				break;
			}
		}
	}

        return rv;
}
 
 
//
// 貼り込み要求処理(window event)
//
W	MAINWIN::paste_fn(PNT pos)
{
	W	rv;
	W	pid;
	PNT	p;

        rv = W_NAK;
	p = UWtoPNT(wevt.s.time);	// libapp 上での規約の方を使う
	pactbox = -1;

	pid = 0;
	cfnd_par(wid, p, &pid);
	if (pid == parts[PTIDX_TB_STR1].desc) {
		pactbox = PTIDX_TB_STR1;
	} else if ((pid == parts[PTIDX_TB_STR2].desc) &&
		   (appl->fsn->get_enable())) {
		pactbox = PTIDX_TB_STR2;
	}

	if (pactbox >= 0) {
		// TB_PARTS 上なら内容の評価をしてから受け入れを表明
		if (TRAYOPE::pop_tray(-1, true, false, p) > 0) {
			ppos = p;
			wevt.s.pos = toPNT(0x8000, 0x8000);
			rv = W_ACK;
		}
	}

	return rv;
}


// ---------------------------------------------------- MAINWIN 内 private 関数
//
// 文字列の再描画
//
void	MAINWIN::disp_string()
{
	W	l;

	for (l = 0; l < wdef.pnum; ++l) {
		// TEXT_ITEM 以外は表示対象にしない
		if (((parts[l].itype & 0x000f) != TEXT_ITEM) ||
		    (parts[l].ptr == NULL)) {
			continue;
		}

		gset_scr(gid, TSC_SYS);
		gdra_stp(gid,
			parts[l].ir.c.left, parts[l].ir.c.top + CHSSTD,
			(TC*)parts[l].ptr, tc_strlen((TC*)parts[l].ptr),
			G_STORE);
	}

	// 傍線の描画(は本当は文字列ではないけど)
	PNT	p1, p2;

	p1.x = parts[PTIDX_TB_STR2].ir.c.right;
	p1.y = parts[PTIDX_TB_STR2].ir.c.top + (rectheight(parts[PTIDX_TB_STR2].ir) >> 1);
	p2.x = parts[PTIDX_AS_ENABLE].ir.c.left - 1;
	p2.y = p1.y;
	gdra_lin(gid, p1, p2, 0x0001, BLACK100, G_STORE);

        return;  
}


//
// 自ウィンドウ内での文字列の移動/複写
//
void	MAINWIN::str_move(W pid, bool cut)
{
	W	dpid;

	dpid = 0;
	cfnd_par(wid, wevt.s.pos, &dpid);
	if (pid != dpid) {
		WERR	rv;
		W	abox;		// 次の実行パーツの候補
		TC	buf[CVAL::STR_LEN + 1];

		rv = ER_OK;
		ccut_txt(pid, CVAL::STR_LEN + 1, buf, 0);
		if (dpid == parts[PTIDX_TB_STR1].desc) {
			// 文字列1 に対して
			rv = cins_txt(dpid, wevt.s.pos, buf);
			abox = PTIDX_TB_STR1;
		} else if ((dpid == parts[PTIDX_TB_STR2].desc) && 
			   (appl->fsn->get_enable())) {
			// 文字列2 に対して
			rv = cins_txt(dpid, wevt.s.pos, buf);
			abox = PTIDX_TB_STR2;
		}

		// 挿入に成功した
		if (rv > 0) {
			if (cut) {
				ccut_txt(pid, 1, buf, 1);
			}
			actbox = abox;
		}
	}

	return;
}


//
// パーツ状態の初期化(付箋固有データの内容を反映)
//
void	MAINWIN::par_setup(UW msk)
{
	W	val[4];			// 汎用(パーツの値の設定用)

	// 文字列1 の設定
	if (msk & PTMSK_TB_STR1) {
		cset_val(parts[PTIDX_TB_STR1].desc, CVAL::STR_LEN, (W*)appl->fsn->get_str1());
	}

	// 文字列2 の設定
	if (msk & PTMSK_TB_STR2) {
		cset_val(parts[PTIDX_TB_STR2].desc, CVAL::STR_LEN, (W*)appl->fsn->get_str2());
		cchg_par(parts[PTIDX_TB_STR2].desc, (appl->fsn->get_enable()) ? P_ENABLE : P_DISABLE);
	}

	// 有効/不能の設定
	if (msk & PTMSK_AS_ENABLE) {
		val[0] = (appl->fsn->get_enable()) ? 1 : 0;
		cset_val(parts[PTIDX_AS_ENABLE].desc, 1, val);
	}

	// 色の設定
	if (msk & PTMSK_SS_COLOR) {
		val[0] = appl->fsn->get_color() + 1;
		cset_val(parts[PTIDX_SS_COLOR].desc, 1, val);
	}

	// サイズの設定(VL_PARTS)
	if (msk & PTMSK_VL_SIZE) {
		val[1] = appl->fsn->get_size();
		val[0] = val[1] + 1;
		cset_val(parts[PTIDX_VL_SIZE].desc, 2, val);
	}

	// サイズの設定(SB_PARTS)
	if (msk & PTMSK_SB_SIZE) {
		val[0] = appl->fsn->get_size();
		cset_val(parts[PTIDX_SB_SIZE].desc, 1, val);
	}

	// ピッチの設定
	if (msk & PTMSK_WS_PITCH) {
		val[0] = (appl->fsn->get_pitch()) ? 2 : 1;
		cset_val(parts[PTIDX_WS_PITCH].desc, 1, val);
	}

	return;
}



//
// パーツの状態を取得(付箋固有データに内容を設定)
//
void	MAINWIN::par_getdat(UW msk)
{
	W	val[4];			// 汎用(パーツの値の取得用)
	TC	str[CVAL::STR_LEN + 1];

	// 文字列1 の取得
	if (msk & PTMSK_TB_STR1) {
		cget_val(parts[PTIDX_TB_STR1].desc, CVAL::STR_LEN + 1, (W*)str);
		appl->fsn->set_str1(str);
	}

	// 文字列2 の取得
	if (msk & PTMSK_TB_STR2) {
		cget_val(parts[PTIDX_TB_STR2].desc, CVAL::STR_LEN + 1, (W*)str);
		appl->fsn->set_str2(str);
	}

	// 有効/不能の取得
	if (msk & PTMSK_AS_ENABLE) {
		cget_val(parts[PTIDX_AS_ENABLE].desc, 1, val);
		appl->fsn->set_enable(val[0] == 1);
	}

	// 色の取得
	if (msk & PTMSK_SS_COLOR) {
		cget_val(parts[PTIDX_SS_COLOR].desc, 1, val);
		appl->fsn->set_color(val[0] - 1);
	}

	// サイズの取得(VL_PARTS)
	if (msk & PTMSK_VL_SIZE) {
		cget_val(parts[PTIDX_VL_SIZE].desc, 2, val);
		appl->fsn->set_size(val[1]);
	}

	// サイズの取得(SB_PARTS)
	if (msk & PTMSK_SB_SIZE) {
		cget_val(parts[PTIDX_SB_SIZE].desc, 1, val);
		appl->fsn->set_size(val[0]);
	}

	// ピッチの取得
	if (msk & PTMSK_WS_PITCH) {
		cget_val(parts[PTIDX_WS_PITCH].desc, 1, val);
		appl->fsn->set_pitch(val[0] == 2);
	}

	return;
}


//
// 文字列入力用 TB_PARTS (その1) の動作関数
//
ERR	MAINWIN::p_tb_str1_fn(W pid, W pidx)
{
	ERR	er;
	W	sts;

	er = ER_OK;
	actbox = pidx;
	sts = cact_par(pid, &wevt);
	if (sts <= ER_OK) {	
		er = sts;
	} else if ((sts == P_EVENT) || (sts == P_MENU)) {
		par_getdat(PTMASK(pidx));
		unget_wevt();
	} else {
		par_getdat(PTMASK(pidx));

		// 終了の契機の確認
		sts &= 0x0007;
		if ((sts == P_MOVE) || (sts == P_COPY)) {
			// 選択範囲の drag
			if (wevt.s.wid == wid) {
				// 自ウィンドウ内での授受
				str_move(pid, (sts == P_MOVE));
			} else  {
				// 他のウィンドウへ一時トレー経由で移動/複写
				do_pushtray(true, (sts == P_MOVE));
			}
		} else if (sts == P_NL) {
			// 改段落の入力
			;		// 特に何もしない
		} else if (sts == P_TAB) {
			// TAB の入力(次のボックス系パーツに focus を移動)
			actbox = (appl->fsn->get_enable()) ? PTIDX_TB_STR2 : PTIDX_SB_SIZE;
		} else if (sts == P_BUT) {
			// パーツ領域外の click
			unget_wevt();
		}
	}

	return er;
}


//
// 文字列入力用 TB_PARTS (その2) の動作関数
//
ERR	MAINWIN::p_tb_str2_fn(W pid, W pidx)
{
	ERR	er;
	W	sts;

	er = ER_OK;
	actbox = pidx;
	sts = cact_par(pid, &wevt);
	if (sts <= ER_OK) {	
		er = sts;
	} else if ((sts == P_EVENT) || (sts == P_MENU)) {
		par_getdat(PTMASK(pidx));
		unget_wevt();
	} else {
		par_getdat(PTMASK(pidx));

		// 終了の契機の確認
		sts &= 0x0007;
		if ((sts == P_MOVE) || (sts == P_COPY)) {
			// 選択範囲の drag
			if (wevt.s.wid == wid) {
				// 自ウィンドウ内での授受
				str_move(pid, (sts == P_MOVE));
			} else  {
				// 他のウィンドウへ一時トレー経由で移動/複写
				do_pushtray(true, (sts == P_MOVE));
			}
		} else if (sts == P_NL) {
			// 改段落の入力
			;		// 特に何もしない
		} else if (sts == P_TAB) {
			// TAB の入力(次のボックス系パーツに focus を移動)
			actbox = PTIDX_SB_SIZE;
		} else if (sts == P_BUT) {
			// パーツ領域外の click
			unget_wevt();
		}
	}

	return er;
}


//
// 有効・不能の切り替え用 AS_PARTS の動作関数
//
ERR	MAINWIN::p_as_enable_fn(W pid, W pidx)
{
	ERR	er;
	W	sts;

	er = ER_OK;
	sts = cact_par(pid, &wevt);
	if (sts <= ER_OK) {
		er = sts;
	} else if (sts & P_CHANGE) {
		// click されたので状態を設定する
		par_getdat(PTMASK(pidx));
		par_setup(PTMASK(PTIDX_TB_STR2));

		// ボックス系パーツの有効状態の切り替え
		if (appl->fsn->get_enable()) {
			// 有効になったので 文字列2 側を有効にする
			actbox = PTIDX_TB_STR2;
		} else if (actbox == PTIDX_TB_STR2) {
			// 文字列2 側が有効だったのなら遷移する
			actbox = PTIDX_SB_SIZE;
		}
	}

	return er;
}


//
// 色選択用の SS_PARTS の動作関数
//
ERR	MAINWIN::p_ss_color_fn(W pid, W pidx)
{
	ERR	er;
	W	sts;

	er = ER_OK;
	sts = cact_par(pid, &wevt);
	if (sts <= ER_OK) {
		er = sts;
	} else if (sts & P_CHANGE) {
		// いづれかの項目が選択された
		par_getdat(PTMASK(pidx));
	}

	return er;
}


//
// サイズ設定用 VL_PARTS の動作関数
//
ERR	MAINWIN::p_vl_size_fn(W pid, W pidx)
{
	ERR	er;

	er = ER_OK;
	for ( ; ; ) {
		// volume を実行させる
		W	sts;

		sts = cact_par(pid, &wevt);
		if (sts <= ER_OK) {
			// 特に変化無しが異常が発生
			er = sts;
			break;
		} else if ((sts & 0x7000) == 0x4000) {
			// 特に変化がない
			break;
		}

		// 変量を設定する
		W	fact;		// 変位量
		W	val[4];

		fact = 0;
		cget_val(pid, 4, val);
		switch (sts & 0x000f) {
			case (P_SMOOTH | P_LEFT):
				// 左にスムース移動
				fact = -1;
				break;
			case (P_SMOOTH | P_RIGHT):
				// 右にスムース移動
				fact = 1;
				break;
			case (P_AREA | P_LEFT):
				// 左にエリア移動
				fact = -16;
				break;
			case (P_AREA | P_RIGHT):
				// 右にエリア移動
				fact = 16;
				break;
		}

		if (fact == 0) {
			// 変位なし(追従動作時)
			;	// 特になにもしない
		} else {
			val[1] += fact;
			if (val[1] < val[3]) {
				val[1] = val[3];
			} else if (val[1] > (val[2] - 1)) {
				val[1] = val[2] - 1;
			}
			val[0] = val[1] + 1;
			cset_val(pid, 2, val);
		}

		if (!(sts & P_BREAK)) {
			// 追従状態でなければ終了
			break;
		}

		// 現状を SB_PARTS 側にも反映する
		par_getdat(PTMASK(pidx));
		par_setup(PTMASK(PTIDX_SB_SIZE));
	}

	if (er >= ER_OK) {
		// 正常終了したのなら、状態の反映
		par_getdat(PTMASK(pidx));
		par_setup(PTMASK(PTIDX_SB_SIZE));
		actbox = PTIDX_SB_SIZE;
	}

	return er;
}


//
// サイズ設定用 SB_PARTS の動作関数
//
ERR	MAINWIN::p_sb_size_fn(W pid, W pidx)
#ifdef	SB_CEXE_USE			// cexe_par() を使用する
{
	ERR	er;

	er = ER_OK;
	actbox = pidx;
	if ((wevt.s.type == EV_KEYDWN) || (wevt.s.type == EV_AUTKEY)) {
		// KC_TAB の時は事前に取り出して処理をすます
		if (wevt.e.data.key.code == KC_TAB) {
			actbox = PTIDX_TB_STR1;
			goto EXIT;
		}
	}

	// parts の実行
	W	sts;

	sts = cexe_par(pid, &wevt);
	if (sts <= ER_OK) {
		// 特に操作されていない
		er = sts;
	} else {
		// 何か
		W	val;

		cget_val(pid, 1, &val);
		if (val != appl->fsn->get_size()) {
			// 値が変わっていれば反映させる
			appl->fsn->set_size(val);
			par_setup(PTMASK(PTIDX_VL_SIZE));
		}
	}

EXIT:
	return er;
}
#else	// SB_CEXE_USE			// cexe_par() を使用しない
{
	ERR	er;
	W	sts;

	er = ER_OK;
	actbox = pidx;
	sts = cact_par(pid, &wevt);
	if (sts <= ER_OK) {	
		er = sts;
	} else if ((sts == P_EVENT) || (sts == P_MENU)) {
		par_getdat(PTMASK(pidx));
		par_setup(PTMASK(PTIDX_VL_SIZE));
		unget_wevt();
	} else {
		par_getdat(PTMASK(pidx));
		par_setup(PTMASK(PTIDX_VL_SIZE));

		// 終了の契機の確認
		sts &= 0x0007;
		if (sts == P_NL) {
			// 改段落の入力
			;		// 特に何もしない
		} else if (sts == P_TAB) {
			// TAB の入力(次のボックス系パーツに focus を移動)
			actbox = PTIDX_TB_STR1;
		} else if (sts == P_BUT) {
			// パーツ領域外の click
			unget_wevt();
		}
	}

	return er;
}
#endif	// SB_CEXE_USE


//
// ピッチ選択用の WS_PARTS の動作関数
//
ERR	MAINWIN::p_ws_pitch_fn(W pid, W pidx)
{

	ERR	er;
	W	sts;

	er = ER_OK;
	sts = cact_par(pid, &wevt);
	if (sts <= ER_OK) {
		er = sts;
	} else if (sts & P_CHANGE) {
		// いづれかの項目が選択された
		par_getdat(PTMASK(pidx));
	}

	return er;
}


//
// 初期化の MS_PARTS の動作関数
//
ERR	MAINWIN::p_ms_init_fn(W pid, W pidx)
{
	ERR	er;
	W	sts;

	er = ER_OK;
	sts = cact_par(pid, &wevt);
	if (sts <= ER_OK) {
		er = sts;
	} else if (sts & P_CHANGE) {
		// click されたので値を初期値に戻す
		init_data();
	}

	return er;
}
