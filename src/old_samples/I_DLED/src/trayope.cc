//
//	trayope.cc (偽仮身一覧/トレー操舵系)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#include	<basic.h>
#include	<btron/btron.h>
#include	<btron/hmi.h>
#include	<btron/vobj.h>
#include	<btron/libapp.h>
#include	<tad.h>

#include	<vector>

#include	"val.h"
#include	"cval.h"
#include	"dbox.h"

#include	"appl.h"
#include	"editobj.h"
#include	"vobjope.h"
#include	"vobjs.h"
#include	"guiope.h"
#include	"mainwin.h"
#include	"ud_ope.h"
#include	"trayope.h"
#include	"misc.h"


// 内部構造体
typedef	struct {			// 各 record 毎の内容
	std::vector<B>	dat;		// 保存内容
} TRAYDAT;


// -------------------------------------- TRAYOPE 内 public 関数(static member)
//
// (一時含む)トレーへ複写/移動(仮身専用)
//
// vid >= 0 の場合、内部の仮身情報は見ないで、単一の仮身を操作する
// (これは、quick press による実身複製専用の処理)
//
void	TRAYOPE::push_tray(W vid, bool cut, bool tmp, SIZE gap)
{
	// 格納内容の生成
	W	cnt = ((vid >= 0) ? 1 : (appl->vobjs.get_selcnt()));
	RECT	r;			// 全体の領域
	TR_VOBJREC*	vrec;
	std::vector<TRAYDAT>	tdat(cnt);
	std::vector<TRAYREC>	trec(cnt + 1);

	if (vid >= 0) {
		// 単一の仮身を直接操作(付箋は禁止)
		UW	vsize;

		oget_vob(vid, NULL, NULL, 0, &vsize);
		tdat[0].dat.resize(vsize + sizeof(VLINK));
		vrec = reinterpret_cast<TR_VOBJREC*>(tdat[0].dat.begin());
		oget_vob(vid, &vrec->vlnk, &vrec->vseg, vsize, NULL);

		trec[1].id = TR_VOBJ;
		trec[1].len = tdat[0].dat.size();
		trec[1].dt = tdat[0].dat.begin();
		orsz_vob(vid, &r, V_CHECK);
	} else {
		// 選択中の仮身/付箋を対象にする
		W	idx;
		bool	first;
		VOBJ*	ptr;
		const	VOBJ*	vobj = appl->vobjs.get_vobj();

		ptr = const_cast<VOBJ*>(vobj);
		first = true;
		idx = 1;
		do {
			if (ptr->get_sel()) {
				UW	vsize;
				RECT	vr;

				vid = ptr->get_vid();
				oget_vob(vid, NULL, NULL, 0, &vsize);
				if (ptr->get_type()) {
					// 仮身
					trec[idx].id = TR_VOBJ;
					tdat[idx - 1].dat.resize(vsize + sizeof(VLINK));
					vrec = reinterpret_cast<TR_VOBJREC*>(tdat[idx - 1].dat.begin());
					oget_vob(vid, &vrec->vlnk, &vrec->vseg, vsize, NULL);
				} else {
					// 機能付箋
					trec[idx].id = TS_FFUSEN;
					tdat[idx - 1].dat.resize(vsize);
					oget_vob(vid, NULL, tdat[idx - 1].dat.begin(), vsize, NULL);
				}
				trec[idx].len = tdat[idx - 1].dat.size();
				trec[idx].dt = tdat[idx - 1].dat.begin();
				++idx;

				orsz_vob(vid, &vr, V_CHECK);
				if (first) {
					r = vr;
					first = false;
				} else {
					orrect(&r, &r, &vr);
				}
			}
			ptr = const_cast<VOBJ*>(ptr->get_next());
		} while (ptr != vobj);
	}

	// ヘッダ部分の設定
	FIGSEG	head = {r, r, -120, -120, 0};

	trec[0].id = TS_FIG;
	trec[0].len = sizeof(FIGSEG);
	trec[0].dt = reinterpret_cast<B*>(&head);

	// 方法毎の処理
	if (tmp) {
		// 一時トレーを経由する
		WERR	rv;

		if (wevt.s.wid <= 0) {
			// 複写/移動先として相応しくない
			goto EXIT;
		}
		rv = tset_dat(trec.begin(), trec.size());
		if (rv < ER_OK) {
			// 一時トレーへの格納に失敗
			errpanel(DBOX::EPNL_TMPTRAY, rv);
			goto EXIT;
		}

		// 対象のウィンドウへ要求を送信
		wevt.r.type = EV_REQUEST;
		wevt.r.cmd = W_PASTE;
		wevt.e.time = PNTtoUW(wevt.e.pos);
		wevt.e.pos.x -= gap.h;
		wevt.e.pos.y -= gap.v;
		rv = wsnd_evt(&wevt);
		if (rv < ER_OK) {
			// 要求の送信に失敗
			tset_dat(NULL, 0);
			errpanel(DBOX::EPNL_PASTE, rv);
			goto EXIT;
		}

		// 応答待ち
		rv = wwai_rsp(&wevt, W_PASTE, 60000);	// 60000[ms] = 1[min]
		if (rv == W_ACK) {
			if ((wevt.s.pos.x == static_cast<H>(0x8000)) &&
			    (wevt.s.pos.y == static_cast<H>(0x8000))) {
				// 削除しなくてもいいと言われた
				cut = false;
			}
		} else {
			// 受け入れてもらえなかった
			tset_dat(NULL, 0);
			errpanel(DBOX::EPNL_PASTE, rv);
			goto EXIT;
		}
	} else {
		// トレーを経由する
		WERR	rv;

		rv = tpsh_dat(trec.begin(), trec.size(), (TC*)getdbox(DBOX::TRAY_NAME));
		if (rv < ER_OK) {
			// トレーへの格納に失敗
			errpanel(DBOX::EPNL_PUSHTRAY, rv);
			goto EXIT;
		}
	}

	// 後処理
	if (cut) {
		// 選択中の仮身群を一時削除にする
		UD_OPE::make_undo(UD_OPE::UT_DEL);
		appl->gui->mwin->disp_selfrm(0);
		appl->vobjs.set_alldel(true, true);
		appl->gui->mwin->disp_selfrm(-2);
		appl->gui->mwin->disp_selfrm(1);
		appl->gui->mwin->upd_narea();
		appl->eobj->set_editflg(true);	// 更新
	}
	if (tmp) {
		// ウィンドウを切り替える(一連の処理後でないと駄目)
		wswi_wnd(wevt.s.wid, NULL);
	}

EXIT:
	return;
}


//
// (一時含む)トレーから複写/移動
//
// chk == true : 内容を調べるだけ
// 返り値は取り出した仮身/付箋数
//
W	TRAYOPE::pop_tray(bool chk, bool tmp, bool cut, bool disp, PNT pos)
{
	W	cnt;

	cnt = 0;

	if (pos.x < 0) {
		pos.x = 0;
	}
	if (pos.y < 0) {
		pos.y = 0;
	}

	// 操舵関数の設定
	FUNCP	tfunc;			// 用いる tray 操舵関数

	if (tmp) {
		tfunc = reinterpret_cast<FUNCP>(tget_dat);
	} else {
		tfunc = reinterpret_cast<FUNCP>(tpop_dat);
	}

	// record の走査
	W	rec;
	bool	txt;
	RECT	dr;

	dr = (RECT){{0, 0, 0, 0}};
	for (rec = 1, txt = true; ; ++rec) {
		// レコード種別/長さの取得
		W	size;
		W	type;

		type = (*tfunc)(NULL, 0, &size, rec, NULL);
		if (type <= 0) {
			// record の終端か error code
			break;
		} else if ((type == TS_TEXT) && (cnt <= 0)) {
			// 文章開始セグメント
			txt = true;
		} else if ((type == TS_FIG) && (cnt <= 0)) {
			// 図形開始セグメント
			FIGSEG	fig;

			(*tfunc)(&fig, sizeof(FIGSEG), NULL, rec, NULL);
			pos.x -= fig.draw.c.left;
			pos.y -= fig.draw.c.top;
			txt = false;
		} else if ((type == TR_VOBJ) || (type == TS_FFUSEN)) {
			// 仮身セグメント/機能付箋
			++cnt;
			if (chk) {
				// 個数の計数のみ
				break;
			}

			// 内容の取得
			W	vlen;
			VLINK*	vlnk;
			VOBJSEG*	vseg;
			std::vector<B>	buf(size);

			(*tfunc)(buf.begin(), buf.size(), NULL, rec, NULL);
			if (type == TR_VOBJ) {
				vlnk = reinterpret_cast<VLINK*>(buf.begin());
				vseg = reinterpret_cast<VOBJSEG*>(&buf[sizeof(VLINK)]);
				vlen = buf.size() - sizeof(VLINK);
			} else {
				vlnk = NULL;
				vseg = reinterpret_cast<VOBJSEG*>(buf.begin());
				vlen = buf.size();
			}

			// 位置の補正
			if (txt) {
				posrect(&vseg->view, pos);
			} else {
				moverect(&vseg->view, pos.x, pos.y);
			}
			MISC::cliprect(vseg->view, CVAL::LIMIT_RECT);

			// 追加
			appl->vobjs.add_vobj(vlnk, vseg, vlen);

			// 選択状態の設定
			if (cnt == 1) {
				// 最初の一個目の場合は全選択解除
				if (disp) {
					appl->gui->mwin->disp_selfrm(-1);
				}
				appl->vobjs.set_allsel(false, false);
				dr = vseg->view;
			}
			(const_cast<VOBJ*>((const_cast<VOBJ*>(appl->vobjs.get_vobj()))->get_prev()))->set_sel(true);
			orrect(&dr, &dr, &vseg->view);
		}
	}

	if (cnt > 0) {
		if (disp) {
			appl->gui->mwin->redisp(&dr);
		}
		if ((!(chk)) && (cut)) {
			// 取り出したレコードを削除
			if (tmp) {
				tset_dat(NULL, 0);
			} else {
				tdel_dat();
			}
		}
	}


	return cnt;
}
