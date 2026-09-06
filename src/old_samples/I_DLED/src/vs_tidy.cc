//
//	vs_tidy.cc (偽仮身一覧/仮身/付箋群管理系-自動配置/整頓処理系)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#include	<basic.h>
#include	<btron/btron.h>
#include	<btron/vobj.h>
#include	<btron/libapp.h>

#include	<vector>

#include	"val.h"
#include	"cval.h"
#include	"struct.h"
#include	"debug.h"

#include	"appl.h"
#include	"fusen.h"
#include	"vobjope.h"
#include	"vobjs.h"


// 内部変数
LOCAL	W	ADJ_TBL[] = {		// 整頓時の長さ調整方法
			V_NODISP | V_ADJUST,
			V_NODISP | V_ADJUST1,
			V_NODISP | V_ADJUST2,
			V_NODISP | V_ADJUST3
		};


// ------------------------------------------------------- VOBJS 内 public 関数
//
// 自動配置の実行(DLED 準互換)
//
void	VOBJS::do_autotidy(const RECT vrect)
{
	// 仮身がなければ実行しない
	if (vobj == NULL) {
		goto EXIT;
	}

	// 右辺間隙の算出
	W	r_gap;

	r_gap = CVAL::AT_WIDTH * (CVAL::AT_LIMIT >> 8)/(CVAL::AT_LIMIT & 0xff);

	// 水平間隙の算出
	W	h_gap;
	W	h_clamp;

	h_gap = CVAL::AT_HGAP;
	h_clamp = ((h_gap + CVAL::AT_WIDTH) & 7);
	if (h_clamp != 0) {
		h_gap -= h_clamp;
		if ((h_gap < 0) || (h_clamp >= 4)) {
			h_gap += 8;
		}
	}

	// 仮配置(水平数の算出用)
	W	h_cnt;			// 水平仮身数
	W	v_height;		// 基準仮身高さ
	PNT	dp;			// 配置原点
	bool	t_line;			// true : 最初の行のまま
	SIZE	wsize;			// 作業領域
	VOBJ*	ptr;

	dp = (PNT){CVAL::AT_LGAP, CVAL::AT_TGAP};
	wsize = (SIZE){rectwidth(vrect), rectheight(vrect)};
	v_height = 0;

	h_cnt = 0;
	t_line = true;
	ptr = vobj;
	do {
		// 非処理対象の検査
		if ((ptr->get_hold()) || (ptr->get_back())) {
			// 固定化/背景化時は対象外
			goto DMY_NEXT;
		} else if (ptr->get_type()) {
			// 仮身で隠蔽時は対象外(隠蔽は非表示状態)
			const	VLINK*	vlnk = ptr->get_vlnk();

			if ((vlnk->attr & V_HIDDEN) && (!(hidden))) {
				goto DMY_NEXT;
			}
		}

		// 開いた仮身を閉じる
		RECT	vr;

		orsz_vob(ptr->get_vid(), &vr, V_CLOSE | V_NODISP);

		// 位置の調節
		if ((wsize.h - dp.x) < CVAL::AT_WIDTH) {
			// ウィンドウ幅をはみだすので、次の行に配置
			t_line = false;
			dp.x = CVAL::AT_LGAP;
			dp.y += v_height + CVAL::AT_VGAP;
			if (dp.y >= wsize.v) {
				break;
			}
		}
		if (t_line) {
			// 最初の行なら、水平仮身数の算出
			++h_cnt;
		}
		if (v_height < rectwidth(vr)) {
			v_height = rectheight(vr);
		}
		dp.x += CVAL::AT_WIDTH + CVAL::AT_HGAP;

DMY_NEXT:
		// 次へ
		ptr = const_cast<VOBJ*>(ptr->get_next());
	} while (ptr != vobj);

	// 水平配置幅の決定
	W	buf;			// 実配置時の右の空き

	buf = wsize.h - CVAL::AT_LGAP - h_cnt * (CVAL::AT_WIDTH+CVAL::AT_HGAP);
	if ((h_cnt == 0) ||
	    (((dp.y + (v_height >> 1)) > wsize.h) && (buf >= r_gap))) {
		// 配置できそうなので、幅を拡張
		wsize.h += CVAL::AT_WIDTH;
	}

	// 実配置
	W	bot;			// 配置範囲の下底
	PNT	np;			// 実配置点

	np = (PNT){CVAL::AT_LGAP, CVAL::AT_TGAP};
	bot = CVAL::AT_TGAP;

	ptr = vobj;
	do {
		const	W	vid = ptr->get_vid();

		// 非処理対象の検査
		if ((ptr->get_hold()) || (ptr->get_back())) {
			// 固定化/背景化時は対象外
			goto NTV_NEXT;
		} else if (ptr->get_type()) {
			// 仮身で隠蔽時は対象外(隠蔽は非表示状態)
			const	VLINK*	vlnk = ptr->get_vlnk();

			if ((vlnk->attr & V_HIDDEN) && (!(hidden))) {
				goto NTV_NEXT;
			}
		}

		// 仮身サイズの設定
		RECT	nr;

		orsz_vob(vid, &nr, V_CHECK);
		nr.c.right = nr.c.left + CVAL::AT_WIDTH;
		nr.c.bottom = nr.c.top + 1;	// omgr 標準高さを要求
		orsz_vob(vid, &nr, V_SIZE | V_NODISP);

		// 仮身位置の移動
		if ((wsize.h - np.x) < CVAL::AT_WIDTH) {
			// 水平幅をはみだすので、次の行へ
			np.x = CVAL::AT_LGAP;
			np.y = bot + CVAL::AT_VGAP;
		}
		nr.p.lefttop = np;
		omov_vob(vid, 0, &nr, V_NODISP);

		// 次の位置の設定
		if (bot < nr.c.bottom) {
			bot = nr.c.bottom;
		}
		np.x += CVAL::AT_WIDTH + CVAL::AT_HGAP;

		// 内部情報の更新
		ptr->upd_mydat(false, true);

NTV_NEXT:
		// 次へ
		ptr = const_cast<VOBJ*>(ptr->get_next());
	} while (ptr != vobj);

EXIT:
	return;
}


//
// 整頓の実行(DLED 準互換)
//
void	VOBJS::do_selftidy()
{
	// 整頓候補の整列
	std::vector<TLIST>	tlst;
	const	TIDYDAT	tdat = appl->fsn->get_tidy();

	gen_tidylist(tlst);
	if (tlst.empty()) {
		goto EXIT;
	}

	// 整頓の実行
	W	l;
	W	v_gap;
	bool	first;
	RECT	lr, lr2;

	first = true;
	lr = tlst[0].r;
	lr2 = (tlst.size() > 1) ? tlst[1].r : lr;
	for (l = 0; l < tlst.size(); ++l) {
		// 固定化はその場所に居る事に意味がある
		if ((const_cast<VOBJ*>(tlst[l].ptr))->get_hold()) {
			continue;
		}

		// 長さの変更
		RECT	vr;		// 操作対象の仮身の領域

		orsz_vob(tlst[l].vid, &vr, V_CHECK);
		switch (tdat.ltype) {
			case 1:		// 先頭の長さにそろえる
				vr.c.right += (rectwidth(tlst[0].r) - rectwidth(vr));
				orsz_vob(tlst[l].vid, &vr, V_NODISP | V_SIZE);
				break;
			case 2:		// 全体を表示する長さ
			case 3:		// 名前のみ表示する長さ
			case 4:		// 続柄まで表示する長さ
			case 5:		// 日付を表示しない長さ
				orsz_vob(tlst[l].vid, &vr, ADJ_TBL[tdat.ltype - 2]);
				break;
		}

		// 水平位置の移動
		W	h_r;

		switch (tdat.htype) {
			case 1:		// 先頭の左側にそろえる
				vr.c.left = lr.c.left;
				break;
			case 2:		// 先頭の右側にそろえる
				h_r = vr.c.right - lr.c.right;
				if (h_r > vr.c.left) {
					h_r = vr.c.left;
				}
				vr.c.left -= h_r;
				break;
		}

		// 垂直位置の移動
		W	v_r;

		if (tdat.vtype != 0) {
			if (first) {
				if (l == 0) {
					goto POS_SET;
				}
				switch (tdat.vtype) {
					case 1:	// 間隔を詰める
						v_gap = CVAL::MT_VGAP;
						break;
					case 2:	// 先頭２つの間隔にそろえる
						v_gap = lr2.c.top -lr.c.bottom;
						break;
				}
				first = false;
			}
			v_r = lr.c.bottom + v_gap - vr.c.top;
			vr.c.top += v_r;
			vr.c.bottom += v_r;
			lr.c.bottom = vr.c.bottom;
		}

POS_SET:
		// 移動の反映
		omov_vob(tlst[l].vid, 0, &vr, V_NODISP);

		// 内部情報の更新
		(const_cast<VOBJ*>(tlst[l].ptr))->upd_mydat(false, true);
	}

EXIT:
	return;
}


// ------------------------------------------------------ VOBJS 内 private 関数
//
// 整頓対象の仮身群の生成
//
void	VOBJS::gen_tidylist(std::vector<TLIST>& tlst)
{
	if (vobj != NULL) {
		VOBJ*	ptr;

		ptr = vobj;
		do {
			// 除外するかどうかを確認
			if ((!(ptr->get_sel())) || (ptr->get_back())) {
				// 非選択/背景化時は対象外
				// (固定化は場所に意味があるので含める)
				goto NEXT;
			} else if (ptr->get_type()) {
				// 仮身で隠蔽時は対象外(隠蔽は非表示状態)
				const	VLINK*	vlnk = ptr->get_vlnk();

				if ((vlnk->attr & V_HIDDEN) && (!(hidden))) {
					goto NEXT;
				}
			}

			// 挿入内容の生成
			TLIST	tdat;

			tdat.vid = ptr->get_vid();
			tdat.ptr = ptr;
			orsz_vob(tdat.vid, &tdat.r, V_CHECK);
			tlst.insert(tlst.begin(), tdat);

			// list の挿入場所の走査(bubble sort)
			W	idx;

			for (idx = 1; idx < tlst.size(); ++idx) {
				if ((tlst[idx].r.c.top > tdat.r.c.top) ||
				    ((tlst[idx].r.c.top == tdat.r.c.top) &&
				     (tlst[idx].r.c.left > tdat.r.c.left))) {
					// 左上にあればおしまい
					break;
				} else {
					// 右下にあれば、入れ替え
					TLIST	tbuf;

					tbuf = tlst[idx - 1];
					tlst[idx - 1] = tlst[idx];
					tlst[idx] = tbuf;
				}
			}

NEXT:
			// 次へ
			ptr = const_cast<VOBJ*>(ptr->get_next());
		} while (ptr != vobj);
	}

	return;
}
