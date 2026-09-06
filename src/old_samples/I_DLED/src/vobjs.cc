//
//	vobjs.cc (偽仮身一覧/仮身/付箋群管理系)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#include	<basic.h>
#include	<btron/btron.h>
#include	<btron/vobj.h>
#include	<btron/libapp.h>

#include	<new>

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


// ------------------------------------------------------- VOBJS 内 public 関数
//
// constructor
//
VOBJS::VOBJS()
	: rid(-1), hidden(false), vobj(NULL)
{
}


//
// destructor
//
VOBJS::~VOBJS()
{
	del_allvobj();
}


//
// 仮身を追加する(list は環状)
//
void	VOBJS::add_vobj(const VLINK* lnk, const VP dat, W len)
{
	try {
		if (vobj == NULL) {
			// 第一要素の生成
			vobj = new VOBJ(lnk, dat, len, rid, NULL);
			vobj->set_prev(vobj);
			vobj->set_next(vobj);
		} else {
			// 第二要素以降の生成
			vobj->set_prev(new VOBJ(lnk, dat, len, rid, vobj->get_prev()));
			(const_cast<VOBJ*>((const_cast<VOBJ*>(vobj->get_prev()))->get_prev()))->set_next(vobj->get_prev());
			(const_cast<VOBJ*>(vobj->get_prev()))->set_next(vobj);
		}
	} catch (std::bad_alloc) {
		throw EXCEPT_VOBJS(ER_NOMEM);
	} catch (...) {
		throw;
	}

	return;
}


//
// 外部からの仮身を追加する
//
void	VOBJS::ins_vobj(W nvid)
{
	try {
		W	type;
		UW	len;

		type = oget_vob(nvid, NULL, NULL, 0, &len);
		if (vobj == NULL) {
			// 第一要素の生成
			vobj = new VOBJ(nvid, (type == 0), len, rid, NULL);
			vobj->set_prev(vobj);
			vobj->set_next(vobj);
		} else {
			// 第二要素以降の生成
			vobj->set_prev(new VOBJ(nvid, (type == 0), len, rid, vobj->get_prev()));
			(const_cast<VOBJ*>((const_cast<VOBJ*>(vobj->get_prev()))->get_prev()))->set_next(vobj->get_prev());
			(const_cast<VOBJ*>(vobj->get_prev()))->set_next(vobj);
		}
	} catch (std::bad_alloc) {
		throw EXCEPT_VOBJS(ER_NOMEM);
	} catch (...) {
		throw;
	}

	return;
}

 
//
// 全仮身の登録解除
//
void	VOBJS::del_allvobj() throw()
{
	if (vobj != NULL) {
		VOBJ*	ptr;

		ptr = vobj;
		do {
			VOBJ*	dptr = ptr;

			ptr = const_cast<VOBJ*>(ptr->get_next());
			delete dptr;
		} while (ptr != vobj);
	}
	vobj = NULL;

	return;
}


//
// 仮身 ID から仮身を探す
//
const	VOBJ*	VOBJS::srch_vobj(W vid) throw()
{
	VOBJ*	rptr;

	rptr = NULL;
	if (vobj != NULL) {
		VOBJ*	ptr;

		ptr = vobj;
		do {
			if (ptr->get_vid() == vid) {
				// 見つかったのでおしまい
				rptr = ptr;
				break;
			} else {
				// 該当しないので、次へ
				ptr = const_cast<VOBJ*>(ptr->get_next());
			}
		} while (ptr != vobj);
	}

	return rptr;
}


//
// 一時削除状態の仮身を抹消する
//
void	VOBJS::dest_delvobj() throw()
{
	if (vobj != NULL) {
		VOBJ*	ptr;

		ptr = vobj;
		do {
NEXT:
			if (ptr->get_del()) {
				// 一時削除のものを消す
				VOBJ*	dptr = ptr;

				ptr = const_cast<VOBJ*>(ptr->get_next());
				ptr->set_prev(dptr->get_prev());
				const_cast<VOBJ*>(dptr->get_prev())->set_next(ptr);
				if (dptr == vobj) {
					// 環の根を削除
					if(ptr->get_prev() == ptr->get_next()){
						// 他に仮身が無くなる
						vobj = NULL;
					} else {
						vobj = ptr;
					}
					delete dptr;
					if (vobj == NULL) {
						break;
					} else {
						goto NEXT;	// 次へ
					}
				} else {
					// 他の部分を削除
					delete dptr;
				}
			} else {
				// 次の仮身情報へ
				ptr = const_cast<VOBJ*>(ptr->get_next());
			}
		} while (ptr != vobj);
	}

	// 削除候補の実身群の抹消
	delgabage();

	return;
}


//
// 総数を取得する
//
// del == true : 一時削除のものは含めない個数
//
const	UW	VOBJS::get_cnt(bool del)
{
	UW	cnt;

	cnt = 0;

	if (vobj != NULL) {
		VOBJ*	ptr;

		ptr = vobj;
		do {
			if ((!(del)) || (!(ptr->get_del()))) {
				++cnt;
			}
			ptr = const_cast<VOBJ*>(ptr->get_next());
		} while (ptr != vobj);
	}

	return cnt;
}


//
// 全仮身の固定化状態の設定
//
// sel == true : 選択状態のみ
//
void	VOBJS::set_allhold(bool flg, bool sel)
{
	if (vobj != NULL) {
		VOBJ*	ptr;

		ptr = vobj;
		do {
			if ((!(sel)) || (ptr->get_sel())) {
				if (ptr->get_type()) {
					// 仮身の場合は属性を確認する
					const	VLINK*	vlnk = ptr->get_vlnk();

					if ((!(vlnk->attr & V_HIDDEN)) ||
					    (hidden)) {
						ptr->set_hold(flg);
					}
				} else {
					// 付箋の時はそのまま
					ptr->set_hold(flg);
				}
			}
			ptr = const_cast<VOBJ*>(ptr->get_next());
		} while (ptr != vobj);
	}

	return;
}


//
// 全仮身の背景化状態の設定
//
// sel == true : 選択状態のみ
//
void	VOBJS::set_allback(bool flg, bool sel)
{
	if (vobj != NULL) {
		VOBJ*	ptr;
		RECT	dr;
		bool	first;

		ptr = vobj;
		dr = (RECT){{0, 0, 0, 0}};
		first = true;
		do {
			if ((!(sel)) || (ptr->get_sel())) {
				// 再描画範囲の取得
				RECT	r;

				orsz_vob(ptr->get_vid(), &r, V_CHECK);
				if (first) {
					dr = r;
					first = false;
				} else {
					orrect(&dr, &dr, &r);
				}

				if (ptr->get_type()) {
					// 仮身の場合は属性を確認する
					const	VLINK*	vlnk = ptr->get_vlnk();

					if ((!(vlnk->attr & V_HIDDEN)) ||
					    (hidden)) {
						ptr->set_back(flg);
					}
				} else {
					// 付箋の時はそのまま
					ptr->set_back(flg);
				}
			}
			ptr = const_cast<VOBJ*>(ptr->get_next());
		} while (ptr != vobj);

		// 再描画の発行
		appl->gui->mwin->redisp(&dr);
	}

	return;
}


//
// 全仮身の一時削除状態の設定
//
// sel == true : 選択状態のみ
//
void	VOBJS::set_alldel(bool flg, bool sel)
{
	if (vobj != NULL) {
		VOBJ*	ptr;
		RECT	dr;
		bool	first;

		ptr = vobj;
		dr = (RECT){{0, 0, 0, 0}};
		first = true;
		do {
			if ((!(sel)) || (ptr->get_sel())) {
				// 再描画範囲の取得
				RECT	r;

				orsz_vob(ptr->get_vid(), &r, V_CHECK);
				if (first) {
					dr = r;
					first = false;
				} else {
					orrect(&dr, &dr, &r);
				}

				// flag の反映
				if (ptr->get_type()) {
					// 仮身の場合は属性を確認する
					const	VLINK*	vlnk = ptr->get_vlnk();

					if ((!(vlnk->attr & V_HIDDEN)) ||
					    (hidden)) {
						ptr->set_del(flg);
					}
				} else {
					// 付箋の時はそのまま
					ptr->set_del(flg);
				}
			}
			ptr = const_cast<VOBJ*>(ptr->get_next());
		} while (ptr != vobj);

		// 再描画の発行
		appl->gui->mwin->redisp(&dr);
	}
	

	return;
}


//
// 全仮身の選択状態の設定
//
// sel == true : 選択状態のみ
//
void	VOBJS::set_allsel(bool flg, bool sel)
{
	if (vobj != NULL) {
		VOBJ*	ptr;

		ptr = vobj;
		do {
			if ((!(sel)) || (ptr->get_sel())) {
				if ((ptr->get_type()) && (flg)) {
					// 選択状態設定時は仮身の場合は属性
					// を確認する
					const	VLINK*	vlnk = ptr->get_vlnk();

					if ((!(vlnk->attr & V_HIDDEN)) ||
					    (hidden)) {
						ptr->set_sel(flg);
					}
				} else {
					// 解除時か付箋の時はそのまま
					ptr->set_sel(flg);
				}
			}
			ptr = const_cast<VOBJ*>(ptr->get_next());
		} while (ptr != vobj);
	}

	return;
}


//
// 全仮身の矩形範囲を取得する
//
const	RECT	VOBJS::get_allarea()
{
	RECT	r;

	r = (RECT){{0, 0, 0, 0}};
	if (vobj != NULL) {
		VOBJ*	ptr;

		ptr = vobj;
		do {
			RECT	vr;

			if (orsz_vob(ptr->get_vid(), &vr, V_CHECK) >= ER_OK) {
				// 取得できたものの間でのみ反映させていく
				if (vr.c.right > r.c.right) {
					r.c.right = vr.c.right;
				}
				if (vr.c.bottom > r.c.bottom) {
					r.c.bottom = vr.c.bottom;
				}
			}
			ptr = const_cast<VOBJ*>(ptr->get_next());
		} while (ptr != vobj);
	}

	// 右・下には間隙を設ける
	r.c.right += CHSSTD;
	r.c.bottom += CHSSTD;

	// 制限を超えないように
	if (r.c.right > CVAL::LIMIT_RECT.c.right + CHSSTD) {
		r.c.right = CVAL::LIMIT_RECT.c.right + CHSSTD;
	}
	if (r.c.bottom > CVAL::LIMIT_RECT.c.bottom + CHSSTD) {
		r.c.bottom = CVAL::LIMIT_RECT.c.bottom + CHSSTD;
	}

	return r;
}


//
// PD 位置の対応する仮身を探す
//
// vid < 0 : 対象無し
//
const	W	VOBJS::fnd_posvobj(PNT p)
{
	W	vid;

	vid = -1;
	if (vobj != NULL) {
		VOBJ*	ptr;

		// 選択中のものから探す
		vid = 0;
		ptr = vobj;
		do {
			ptr = const_cast<VOBJ*>(ptr->get_prev());
			if (ptr->get_sel()) {
				RECT	vr;

				orsz_vob(ptr->get_vid(), &vr, V_CHECK);
				if (inrect(vr, p)) {
					vid = ptr->get_vid();
					break;
				}
			}
		} while ((vid == 0) && (ptr != vobj));
		if (vid != 0) {
			goto EXIT;
		}

		// 選択中の仮身中でなければ、全体から探す
		ptr = vobj;
		do {
			ptr = const_cast<VOBJ*>(ptr->get_prev());

			if (ptr->get_type()) {
				// 仮身の場合は属性を確認
				const	VLINK*	vlnk = ptr->get_vlnk();

				if ((!(vlnk->attr & V_HIDDEN)) || (hidden)) {
					ofnd_vob(-ptr->get_vid(), p, &vid);
				}
			} else {
				// 付箋の場合はそのまま調査
				ofnd_vob(-ptr->get_vid(), p, &vid);
			}
		} while ((vid == 0) && (ptr != vobj));
		if (vid == 0) {
			vid = -1;
		}
	}

EXIT:
	return vid;
}


//
// 仮身の再表示
//
// vid < 0 : 全て
//
void	VOBJS::dsp_vobj(W vid, const RECT* r)
{
	if (vobj != NULL) {
		UW	atr;
		VOBJ*	ptr;

		atr = V_DISPALL | ((rid < 0) ? V_NOFRAME : 0);

		// 背景化されている分だけを先に表示
		ptr = vobj;
		do {
			if (((vid < 0) || (vid == ptr->get_vid())) &&
			    (ptr->get_back())) {
				if (ptr->get_type()) {
					// 仮身の場合は属性を確認する
					const	VLINK*	vlnk = ptr->get_vlnk();

					if ((!(vlnk->attr & V_HIDDEN)) || (hidden)) {
						odsp_vob(ptr->get_vid(), (RECT*)r, atr);
					}
				} else {
					// 付箋の場合はそのまま表示
					odsp_vob(ptr->get_vid(), (RECT*)r,atr);
				}
				if (vid >= 0) {
					break;
				}
			}
			ptr = const_cast<VOBJ*>(ptr->get_next());
		} while (ptr != vobj);

		// 普通に配置されている分(背景化されている分は除く)
		ptr = vobj;
		do {
			if (((vid < 0) || (vid == ptr->get_vid())) &&
			    (!(ptr->get_back()))) {
				if (ptr->get_type()) {
					// 仮身の場合は属性を確認する
					const	VLINK*	vlnk = ptr->get_vlnk();

					if ((!(vlnk->attr & V_HIDDEN)) || (hidden)) {
						odsp_vob(ptr->get_vid(), (RECT*)r, atr);
					}
				} else {
					// 付箋の場合はそのまま表示
					odsp_vob(ptr->get_vid(), (RECT*)r,atr);
				}
				if (vid >= 0) {
					break;
				}
			}
			ptr = const_cast<VOBJ*>(ptr->get_next());
		} while (ptr != vobj);
	}

	return;
}


//
// 自動起動の実行
//
void	VOBJS::do_autoexec()
{
	if (vobj != NULL) {
		VOBJ*	ptr;

		ptr = vobj;
		do {
			if (ptr->get_type()) {
				// 仮身のみ自動起動可能
				const	VLINK*	vlnk = ptr->get_vlnk();

				if (((!(vlnk->attr & V_HIDDEN)) || (hidden)) &&
				    (vlnk->attr & V_AUTEXE)) {
					ptr->exec_vobj();
				}
			}
			ptr = const_cast<VOBJ*>(ptr->get_next());
		} while (ptr != vobj);
	}

	return;
}
