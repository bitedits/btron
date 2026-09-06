//
//	vs_menu.cc (偽仮身一覧/仮身/付箋群管理系-仮身操作メニュー処理系)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#include	<basic.h>
#include	<btron/btron.h>
#include	<btron/vobj.h>
#include	<btron/libapp.h>

#include	<new>
#include	<vector>

#include	"val.h"
#include	"err.h"
#include	"except.h"
#include	"debug.h"

#include	"appl.h"
#include	"editobj.h"
#include	"guiope.h"
#include	"mainwin.h"
#include	"vobjope.h"
#include	"vobjs.h"


// 内部マクロ
#define	LIST_SIZE(n)	((n) * (sizeof(RECT) + sizeof(W)) + sizeof(W))


// ------------------------------------------------------- VOBJS 内 public 関数
//
// 仮身操作系のメニューの実行処理部
//
void	VOBJS::mn_vobj(W par)
{
	// 選択中の仮身リストの生成
	W*	wptr;
	VOBJ*	vptr;
	std::vector<B>	lbuf(LIST_SIZE(get_selcnt()));

	vptr = vobj;
	wptr = reinterpret_cast<W*>(lbuf.begin());
	do {
		if (vptr->get_sel()) {
			if ((par >= -5) && (par <= -1) && (vptr->get_hold())) {
				// [仮身操作] は固定化仮身では対象にしない
				;
			} else {
				*wptr = vptr->get_vid();
				++wptr;
			}
		}
		vptr = const_cast<VOBJ*>(vptr->get_next());
	} while (vptr != vobj);
	*wptr = 0;			// 終端(memory をはみ出す可能性はない)

	// メニューの実行
	W	sel;

	sel = oexe_vmn(0, par, lbuf.begin());
	switch (sel) {
		case VM_OPEN:		// [仮身操作]-[開く]
		case VM_CLOSE:		// [仮身操作]-[閉じる]
		case VM_DISP:		// [仮身操作]-[属性変更]
			appl->eobj->set_editflg(true);	// 更新
			mn_vobj_attr(reinterpret_cast<const OE_RLIST*>(lbuf.begin()));
			appl->gui->mwin->upd_narea();
			break;
		case VM_NEW:		// [実身操作]-[実身複製]
			appl->eobj->set_editflg(true);	// 更新
			mn_vobj_new(reinterpret_cast<const OE_NOBJ*>(lbuf.begin()));
			appl->gui->mwin->upd_narea();
			break;
		case VM_RELN:		// [仮身操作]-[続柄変更]
			appl->eobj->set_saveflg(true);	// 隠匿
			mn_vobj_chgrname(reinterpret_cast<const OE_RIDX*>(lbuf.begin()));
			break;
		case VM_NAME:		// [実身操作]-[実身名変更]
		case VM_DETACH:		// [ディスク操作]-[切り離し]
		case VM_REFMT:		// [ディスク操作]-[フォーマット]
			mn_vobj_disk(reinterpret_cast<const W*>(lbuf.begin()));
			break;
		case VM_EXREQ:		// 付箋起動
			mn_vobj_exec(reinterpret_cast<const W*>(lbuf.begin()));
			break;
#if	0				// 特に関知しない
		case VM_PASTE:		// [実身操作]-[トレーから書込]
			// lbuf には対象の仮身IDと、各仮身への要求結果の list
			// が返ってきている
			break;
#endif	// 0
#if	0				// 普通は何もしなくていい
		case VM_INFO:		// [実身操作]-[管理情報]
		case VM_ATTACH:		// [ディスク操作]-[接続]
		case VM_DISK:		// [ディスク操作]-[状態表示]
		case VM_APLREG:		// アプリケーション登録
		case VM_APLDEL:		// アプリケーション削除
		case VM_GABAGE:		// ディスク整理
		case VM_NONE:		// 変動無し
		default:		// 何か
			break;
#endif	// 0
	}

	return;
}


// ------------------------------------------------------ VOBJS 内 private 関数
//
// [仮身操作]-[開く]/[閉じる]/[属性変更] に対する挙動
//
void	VOBJS::mn_vobj_attr(const OE_RLIST* lst)
{
	// 選択枠を廃棄
	appl->gui->mwin->disp_selfrm(-1);

	// 結果の反映
	W	idx;
	RECT	dr;			// 再描画領域

	dr = lst[0].r;			// 初期値は最初の子
	for (idx = 0; lst[idx].vid != 0; ++idx) {
		VOBJ*	ptr = const_cast<VOBJ*>(srch_vobj(lst[idx].vid));
		const	VOBJSEG*	vseg = ptr->get_vobjseg();

		orrect(&dr, &dr, (RECT*)&lst[idx].r);
		orrect(&dr, &dr, (RECT*)&vseg->view);
		ptr->upd_mydat(true, true);
	}

	// 再描画
	appl->gui->mwin->redisp(&dr);

	// 選択枠の再生成
	appl->gui->mwin->disp_selfrm(-2);
	appl->gui->mwin->disp_selfrm(1);

	return;
}


//
// [実身操作]-[実身複製] に対する挙動
//
void	VOBJS::mn_vobj_new(const OE_NOBJ* lst)
{
	W	idx;
	RECT	dr;
	RECT	vr;

	dr = (RECT){{0, 0, 0, 0}};
	for (idx = 0; lst[idx].vid != 0; ++idx) {
		ins_vobj(lst[idx].nvid);
		orsz_vob(lst[idx].nvid, &vr, V_CHECK);
		if (idx == 0) {
			dr = vr;
		} else {
			orrect(&dr, &dr, &vr);
		}
	}
	dsp_vobj(-1, &dr);

	return;
}


//
// [仮身操作]-[続柄変更] に対する挙動
//
void	VOBJS::mn_vobj_chgrname(const OE_RIDX* lst)
{
	W	idx;
	RECT	dr;
	RECT	vr;

	dr = (RECT){{0, 0, 0, 0}};
	for (idx = 0; lst[idx].vid != 0; ++idx) {
		VOBJ*	ptr = const_cast<VOBJ*>(srch_vobj(lst[idx].vid));

		orsz_vob(lst[idx].vid, &vr, V_CHECK);
		if (idx == 0) {
			dr = vr;
		} else {
			orrect(&dr, &dr, &vr);
		}
		ptr->upd_mydat(true, false);
	}
	dsp_vobj(-1, &dr);

	return;
}


//
// [実身操作]-[実身名変更]/[ディスク操作]-[切り離し]/[フォーマット]に対する挙動
//
void	VOBJS::mn_vobj_disk(const W* lst)
{
	W	idx;
	RECT	dr;
	RECT	vr;

	dr = (RECT){{0, 0, 0, 0}};
	for (idx = 0; lst[idx] != 0; ++idx) {
		orsz_vob(lst[idx], &vr, V_CHECK);
		if (idx == 0) {
			dr = vr;
		} else {
			orrect(&dr, &dr, &vr);
		}
	}
	dsp_vobj(-1, &dr);

	return;
}


//
// 付箋起動 に対する挙動
//
void	VOBJS::mn_vobj_exec(const W* lst)
{
	W	idx;

	for (idx = 0; lst[idx] != 0; ++idx) {
		oexe_apg(lst[idx], 0);
	}

	return;
}
