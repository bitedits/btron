//
//	vs_sel.cc (偽仮身一覧/仮身/付箋群管理系-選択中の仮身の操作)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#include	<basic.h>
#include	<btron/btron.h>
#include	<btron/vobj.h>
#include	<btron/libapp.h>

#include	"cval.h"
#include	"val.h"
#include	"err.h"
#include	"except.h"
#include	"debug.h"

#include	"appl.h"
#include	"guiope.h"
#include	"mainwin.h"
#include	"vobjope.h"
#include	"vobjs.h"
#include	"misc.h"


// ------------------------------------------------------- VOBJS 内 public 関数
//
// 選択中の仮身 ID を取得する
//
// <  0 : 選択なし
// == 0 : 複数選択状態
// >  0 : 選択している仮身(単一選択状態)
//
const	W	VOBJS::get_selvid()
{
	W	vid;

	vid = -1;			// 初期値は選択なし
	if (vobj != NULL) {
		VOBJ*	ptr;

		ptr = vobj;
		do {
			if (ptr->get_sel()) {
				if (vid < 0) {
					// 単一選択かも知れない
					vid = ptr->get_vid();
				} else {
					// 既にあるので複数選択状態
					vid = 0;
					break;
				}
			}
			ptr = const_cast<VOBJ*>(ptr->get_next());
		} while (ptr != vobj);
	}

	return vid;
}


//
// 選択中の仮身の個数を取得する
//
const	W	VOBJS::get_selcnt()
{
	W	cnt;

	cnt = 0;
	if (vobj != NULL) {
		VOBJ*	ptr;

		ptr = vobj;
		do {
			if (ptr->get_sel()) {
				++cnt;
			}
			ptr = const_cast<VOBJ*>(ptr->get_next());
		} while (ptr != vobj);
	}

	return cnt;
}


//
// 範囲内の仮身の選択状態の設定/取得
//
// chk == false の時は選択状態の設定
//	返り値が true なら、範囲内に仮身/付箋があって、設定が行われた。false
//	なら、範囲内に仮身/付箋がなく、設定は行われていない。
//
// chk == true の時は、選択状態の取得
//	返り値が true なら、全部選択している。false なら選択/非選択が混ざっ
//	ている(範囲に仮身がなければ常に false となる)
//	(sel の値は無視される)
//
bool	VOBJS::chg_rectsel(RECT r, bool sel, bool chk)
{
	bool	flg;

	if (vobj != NULL) {
		bool	exist;		// 範囲内に一つでも仮身/付箋があったか
		VOBJ*	ptr;

		exist = false;
		flg = true;
		ptr = vobj;
		do {
			// 頂点の値の取得
			bool	view;	// 検査してもいいかどうか
			PNT	lt;	// 左上
			PNT	rb;	// 右下

			view = true;
			if (ptr->get_type()) {
				// 仮身の場合は隠蔽かを確認する
				const	VLINK*	vlnk = ptr->get_vlnk();

				if ((!(vlnk->attr & V_HIDDEN)) || (hidden)) {
					const	VOBJSEG*	vseg = ptr->get_vobjseg();

					lt = vseg->view.p.lefttop;
					rb = vseg->view.p.rightbot;
				} else {
					// 隠蔽の場合は検査しない
					view = false;
				}
			} else {
				// 付箋
				const	FUSENSEG*	fseg = ptr->get_fusenseg();

				lt = fseg->view.p.lefttop;
				rb = fseg->view.p.rightbot;
			}

			// 範囲の検査
			if ((view) && ((inrect(r, lt)) && (inrect(r, rb)))) {
				exist = true;
				if (chk) {
					// 選択状態の取得
					if (!(ptr->get_sel())) {
						flg = false;
						break;	// do - while を抜ける
					}
				} else {
					// 選択状態の設定
					ptr->set_sel(sel);
				}
			}

			// 次へ
			ptr = const_cast<VOBJ*>(ptr->get_next());
		} while (ptr != vobj);
		if (!(exist)) {
			// 一個もなければ false にする
			flg = false;
		}
	} else {
		flg = false;
	}

	return flg;
}


//
// 選択中の仮身の変形
//
void	VOBJS::rsz_selvobj(SIZE ds, W type)
{
	if (vobj != NULL) {
		RECT	dr;
		VOBJ*	ptr;
		bool	first;

		ptr = vobj;
		first = true;
		do {
			if ((ptr->get_sel()) &&		// 選択状態
			    (!(ptr->get_hold())) &&	// 非固定状態
			    (ptr->get_type())) {	// 仮身(付箋ではない)
				RECT	vr;

				orsz_vob(ptr->get_vid(), &vr, V_CHECK);
				if (first) {
					dr = vr;
					first = false;
				} else {
					orrect(&dr, &dr, &vr);
				}
				if ((type == V_LTHD) || (type == V_LBHD)) {
					// 左辺変動
					vr.c.left -= ds.h;
				} else {
					// 右辺変動
					vr.c.right += ds.h;
				}
				if ((type == V_LTHD) || (type == V_RTHD)) {
					// 上底変動
					vr.c.top -= ds.v;
				} else {
					// 下底変動
					vr.c.bottom += ds.v;
				}
				orsz_vob(ptr->get_vid(), &vr, V_SIZE);
				ptr->upd_mydat(false, true);
				orrect(&dr, &dr, &vr);
			}
			ptr = const_cast<VOBJ*>(ptr->get_next());
		} while (ptr != vobj);

		// 仮身群の再描画
		appl->gui->mwin->redisp(&dr);
	}

	return;
}


//
// 選択中の仮身の複製/移動処理
//
// copy == true : 複製    == false : 移動
//
void	VOBJS::copy_selvobj(bool copy, SIZE dl)
{
	if (vobj != NULL) {
		ERR	er;
		RECT	dr;
		VOBJ*	ptr;
		bool	first;
		const	VOBJ*	evobj = vobj->get_prev();

		er = ER_OK;
		first = true;
		for (ptr = vobj; ; ptr = const_cast<VOBJ*>(ptr->get_next())) {
			if ((ptr->get_sel()) &&
			    ((copy) || (!(ptr->get_hold())))) {
				W	vid;
				RECT	vr;

				vid = ptr->get_vid();
				orsz_vob(vid, &vr, V_CHECK);
				if (first) {
					dr = vr;
					first = false;
				} else {
					orrect(&dr, &dr, &vr);
				}
				moverect(&vr, dl.h, dl.v);
				MISC::cliprect(vr, CVAL::LIMIT_RECT);
				if (copy) {
					// 複製
					W	nvid;

					nvid = odup_vob(vid);
					if (nvid < ER_OK) {
						er = nvid;
						break;
					}
					omov_vob(nvid, 0, &vr, V_NODISP);
					ins_vobj(nvid);
					ptr->set_sel(false);
					set_sel(nvid, true);
					orrect(&dr, &dr, &vr);
				} else {
					// 移動
					omov_vob(vid, 0, &vr, V_NODISP);
					ptr->upd_mydat(false, true);
				}
				orrect(&dr, &dr, &vr);
			}
			if (ptr == evobj) {
				break;
			}
		}

		// 仮身群の再描画
		appl->gui->mwin->redisp(&dr);

		if (er < ER_OK) {
			// 複製の過程で error があったら、後で通知
			throw EXCEPT_VOBJS(er);
		}
	}

	return;
}
 

//
// 仮身の順位移動
//
// top == true  : いちばん前へ(環では末尾へ)
//     == false : いちばん後ろへ(環では先頭へ)
//
void	VOBJS::renum_selvobj(bool top)
{
	if (vobj != NULL) {
		bool	first;
		RECT	dr;
		VOBJ*	ptr;
		VOBJ*	nptr;
		const	VOBJ*	evobj = ((top) ? vobj->get_prev() : vobj);

		dr = (RECT){{0, 0, 0, 0}};
		first = true;
		for (ptr = vobj; ; ) {
			nptr = const_cast<VOBJ*>(ptr->get_next());
			if (ptr->get_sel()) {
				// 再描画範囲の取得
				RECT	vr;

				orsz_vob(ptr->get_vid(), &vr, V_CHECK);
				if (first) {
					dr = vr;
					first = false;
				} else {
					orrect(&dr, &dr, &vr);
				}

				// 前後の取得
				VOBJ*	prev;	// ptr の前
				VOBJ*	next;	// ptr の後
				VOBJ*	vprev;	// vobj の前
				VOBJ*	vnext;	// vobj の後

				prev = const_cast<VOBJ*>(ptr->get_prev());
				next = const_cast<VOBJ*>(ptr->get_next());
				vprev = const_cast<VOBJ*>(vobj->get_prev());
				vnext = const_cast<VOBJ*>(vobj->get_next());
				if (ptr == vobj) {
					// 環の起点が移動対象
					if (top) {
						// いちばん前へ(環の末尾へ)
						vobj = next;
					} else {
						// いちばん後ろへ(環の先頭へ)
						;	// 何もしない
					}
				} else if (ptr == vprev) {
					// 環の末端が移動対象
					if (top) {
						// いちばん前へ(環の末尾へ)
						;	// 何もしない
					} else {
						// いちばん後ろへ(環の先頭へ)
						vobj = ptr;
					}
				} else {
					// 環のどこかが移動対象
					prev->set_next(next);	// 環から切断
					next->set_prev(prev);
					ptr->set_prev(vprev);	// 前後の設定
					ptr->set_next(vobj);
					vobj->set_prev(ptr);	// 環に挿入
					vprev->set_next(ptr);
					if (top) {
						// いちばん前へ(環の末尾へ)
						;	// 何もしない
					} else {
						// いちばん後ろへ(環の先頭へ)
						vobj = ptr;
					}
				}
			}
			if (nptr == evobj) {
				break;
			} else {
				ptr = nptr;
			}
		}

		// 仮身群の再描画
		appl->gui->mwin->redisp(&dr);
	}

	return;
}
