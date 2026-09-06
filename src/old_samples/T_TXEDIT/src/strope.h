//
//	strope.h (簡易文字列編集/編集文字列管理系ヘッダ)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#ifndef	_T_TXEDIT_STROPE_H
#define	_T_TXEDIT_STROPE_H

#include	<basic.h>
#include	<btron/btron.h>
#include	<tlang.h>
#include	<wtstring.h>

#include	<vector>


// --------------------------------------------------------------- class STROPE
class	STROPE {
public:
	STROPE();			// constructor
	~STROPE();			// destructor

	// 編集文字列長さの取得
	const	W	get_tlen() {return wlen;}

	// 編集文字列の取得
	const	WTC*	get_text() {return wstr.begin();}

	// 文字列の表示
	void	disp_text(W gid, RECT vrect, W mode);

	// 編集対象文字列に文字を追加する
	void	add_text(WTC wch);

	// 着目文字位置の直前の文字を削除する
	void	del_text();

	// 着目文字位置の直後に文字列を挿入する
	void	ins_text(const WTC* istr, W ilen);

	// 着目文字位置の移動
	void	move_cidx(W type);

	// 着目文字位置から caret 位置(window 内相対座標)に変換する
	const	PNT	cnv_crpos();

	// PD 位置(window 内相対座標)から着目文字位置に変換する
	void	cnv_pdpos(PNT p);

	// レイアウト範囲を取得する
	const	SIZE	get_layarea();

private:
	W	cidx;			// 着目文字位置
	W	wlen;			// 実文字列長さ(WTC 単位)
	PNT	lpos;			// caret の保持座標
	std::vector<WTC>	wstr;	// 編集対象文字列
};

#endif	// _T_TXEDIT_STROPE_H
