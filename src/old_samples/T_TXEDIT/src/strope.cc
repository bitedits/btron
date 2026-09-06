//
//	strope.cc (簡易文字列編集/編集文字列管理系)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#include	<basic.h>
#include	<btron/btron.h>
#include	<btron/libapp.h>
#include	<bstring.h>
#include	<tcode.h>
#include	<tlang.h>
#include	<wtstring.h>

#include	<vector>

#include	"cval.h"

#include	"strope.h"


// ------------------------------------------------------ STROPE 内 public 関数
//
// constructor
//
STROPE::STROPE()
	: cidx(0), wlen(0)
{
	lpos = (PNT){-1, -1};
}


//
// destructor
//
STROPE::~STROPE()
{
}


//
// 文字列の表示
//
// mode <  0 : 全体
//	== 0 : 着目文字位置の所属行のみ
//	== 1 : 着目文字位置の所属行以降
//
void	STROPE::disp_text(W gid, RECT vrect, W mode)
{
	// 描画範囲の設定
	W	st;			// 描画始点
	W	ed;			// 描画終点
	PNT	p;			// 描画文字位置

	st = 0;
	ed = 0;
	p = (PNT){CVAL::TEXT_HGAP, CVAL::TEXT_VGAP + CHSSTD};

	if ((mode < 0) || (mode == 1)) {
		ed = wlen;
	}
	if ((mode == 0) || (mode == 1)) {
		// 所属行を探す
		W	l;
		bool	lin;

		lin = false;
		for (l = 0; l < wlen; ++l) {
			if (l == cidx) {
				lin = true;
			}

			// 行の確認
			TC	ch;

			ch = wtcchar(wstr[l]);
			if ((ch == TC_NL) || (ch == TC_CR)) {
				if (lin) {
					if (mode == 0) {
						ed = l;
					}
					break;
				} else {
					st = l + 1;
					p.y += CHSSTD + CVAL::CH_VGAP;
				}
			}
		}
	}

	// 文字列の描画
	W	i;

	for (i = st; i < ed; ++i) {
		TC	ch;

		ch = wtcchar(wstr[i]);

		// 文字位置の確認
		RECT	r;

		r.c.left = p.x;
		r.c.top = p.y - CHSSTD;
		r.c.right = p.x + CHSSTD;
		r.c.bottom = p.y;
		if (sectrect(r, vrect) && (ch != TC_NL) && (ch != TC_CR)) {
			// 表示範囲内なので描画する
			gset_scr(gid, wtclang(wstr[i]));
			gdra_chp(gid, p.x, p.y, ch, G_STORE);
		} else if (r.c.top > vrect.c.bottom) {
			// 下底をはみ出すので打ち切る
			break;
		}

		// 文字の確認
		if ((ch == TC_NL) || (ch == TC_CR)) {
			// 改段落/改行
			p.x = CVAL::TEXT_HGAP;
			p.y += CHSSTD + CVAL::CH_VGAP;
		} else {
			// 他の一般文字列
			p.x += CHSSTD + CVAL::CH_HGAP;
		}
	}

	return;
}


//
// 編集対象文字列に文字を追加する
//
void	STROPE::add_text(WTC wch)
{
	if (wlen < wstr.size()) {
		wstr[wlen] = wch;
	} else {
		wstr.push_back(wch);
	}
	++wlen;

	return;
}


//
// 着目文字位置の直前の文字を削除する
//
void	STROPE::del_text()
{
	if ((wlen > 0) && (cidx > 0)) {
		if (cidx < wlen) {
			// 後ろの分を前に詰める
			memmove(&wstr[cidx - 1], &wstr[cidx], (wstr.size() - cidx) * sizeof(WTC));
		}
		wstr[wlen - 1] = WTNULL;

		if (cidx > 0) {
			--cidx;
			lpos = (PNT){-1, -1};	// 保持解除
		}
		if (wlen > 0) {
			--wlen;
		}
	}

	return;
}


//
// 着目文字位置の直後に文字列を挿入する
//
void	STROPE::ins_text(const WTC* istr, W ilen)
{
	W	rlen;			// 拡張文字数(WTC 単位)

	rlen = ilen + wlen - wstr.size();
	if (rlen > 0) {
		wstr.resize(wstr.size() + rlen);
	}
	memmove(&wstr[cidx + ilen], &wstr[cidx], (wlen - cidx) * sizeof(WTC));
	memcpy(&wstr[cidx], istr, ilen * sizeof(WTC));
	wlen += ilen;
	cidx += ilen;
	lpos = (PNT){-1, -1};		// 保持解除

	return;
}


//
// 着目文字位置の移動([命令] や [拡張] 同時押しの分は含まず)
//
// type == 0 : 左
//	== 1 : 右
//	== 2 : 上
//	== 3 : 下
//
void	STROPE::move_cidx(W type)
{
	if ((type == 0) || (type == 1)) {
		// 左か右へ移動
		W	step;
		TC	ch;

		ch = wtcchar(wstr[cidx]);
		cidx += (type == 0) ? -1 : 1;
		if (cidx < 0) {
			cidx = 0;
		} else if (cidx >= wlen) {
			cidx = wlen - 1;
		}
		lpos = (PNT){-1, -1};	// 保持解除
	} else if ((type == 2) || (type == 3)) {
		// 上下の時は実座標を元に移動する
		PNT	cp;
		PNT	np;

		if (lpos.y < 0) {
			// 保持状態ではない
			lpos = cnv_crpos();
		}
		cp = lpos;

		cp.y += ((type == 2) ? -CHSSTD : CHSSTD) - (CHSSTD >> 1);
		cnv_pdpos(cp);
		np = cnv_crpos();	// 移動後の位置の取得
		lpos = (PNT){cp.x, np.y};
	}

	return;
}


//
// 着目文字位置から caret 位置(window 内相対座標)に変換する
//
// 文字サイズは CHSSTD 固定
//
const	PNT	STROPE::cnv_crpos()
{
	PNT	p;
	W	l;
	TC	ch;

	p = (PNT){CVAL::TEXT_HGAP, CVAL::TEXT_VGAP + CHSSTD};
	for (l = 0; (l < wlen) && (l < cidx); ++l) {
		ch = wtcchar(wstr[l]);
		if ((ch == TC_NL) || (ch == TC_CR)) {
			// 改段落/改行
			p.x = CVAL::TEXT_HGAP;
			p.y += CHSSTD + CVAL::CH_VGAP;
		} else {
			// 他の一般文字列
			p.x += CHSSTD + CVAL::CH_HGAP;
		}
	}

	return p;
}


//
// PD 位置(window 内相対座標)から着目文字位置に変換する
//
void	STROPE::cnv_pdpos(PNT p)
{
	W	l;
	TC	ch;
	PNT	cp;
	bool	lin;

	cp = (PNT){CVAL::TEXT_HGAP + CHSSTD, CVAL::TEXT_VGAP + CHSSTD};
	lin = false;
	cidx = -1;
	for (l = 0; l < wlen; ++l) {
		if ((p.y >= cp.y - CVAL::TEXT_VGAP - CHSSTD) && (p.y <= cp.y)){
			if (p.x <= cp.x) {
				cidx = l;
				break;
			}
			lin = true;
		} else if (lin) {
			cidx = l - 1;
			break;
		}

		ch = wtcchar(wstr[l]);
		if ((ch == TC_NL) || (ch == TC_CR)) {
			// 改段落/改行
			cp.x = CVAL::TEXT_HGAP + CHSSTD;
			cp.y += CHSSTD + CVAL::CH_VGAP;
		} else {
			// 他の一般文字列
			cp.x += CHSSTD + CVAL::CH_HGAP;
		}
	}
	if (cidx < 0) {
		cidx = l - 1;
	}
	lpos = (PNT){-1, -1};		// 保持解除

	return;
}


//
// レイアウト範囲を取得する
//
const	SIZE	STROPE::get_layarea()
{
	SIZE	sz;
	W	l;
	TC	ch;
	PNT	p;

	sz = (SIZE){0, 0};
	p = (PNT){CVAL::TEXT_HGAP, CVAL::TEXT_VGAP + CHSSTD};
	for (l = 0; l < wlen; ++l) {
		// 各文字位置の算出
		ch = wtcchar(wstr[l]);
		if ((ch == TC_NL) || (ch == TC_CR)) {
			// 改段落/改行
			p.x = CVAL::TEXT_HGAP;
			p.y += CHSSTD + CVAL::CH_VGAP;
		} else {
			// 他の一般文字列
			p.x += CHSSTD + CVAL::CH_HGAP;
		}

		// 範囲の確認
		if (sz.h < p.x) {
			sz.h = p.x;
		}
		if (sz.v < p.y) {
			sz.v = p.y;
		}
	}

	return sz;
}
