//
//	fusen.h (簡易文字列編集/付箋管理ヘッダ)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#ifndef	_T_TXEDIT_FUSEN_H
#define	_T_TXEDIT_FUSEN_H

#include	<basic.h>
#include	<btron/btron.h>

#include	"struct.h"


// ---------------------------------------------------------------- class FUSEN
class	FUSEN {
public:
	FUSEN(const MESSAGE* msg);	// constructor
	~FUSEN();			// destructor

	// 内部情報の取得
	const	PNT	get_vpos()	{return fsn.vp;}
	const	RECT	get_wrect()	{return fsn.wattr.rect;}
	const	WINATTR	get_wattr()	{return fsn.wattr;}
	const	WINCOL	get_wcol()	{return fsn.wcol;}

	// 内部情報の更新
	void	set_vpos(PNT p)		{fsn.vp = p;}
	void	set_wrect(RECT r)	{fsn.wattr.rect = r;}
	void	set_wattr(WINATTR par)	{fsn.wattr = par;}
	void	set_wcol(WINCOL wcol)	{fsn.wcol = wcol;}

	// 付箋を出力する
	W	write_fusen(const MESSAGE* msg, W vid, bool eflg = true);

	// 付箋固有データの内容を起動前の状態に戻す
	void	undo_fusen() {fsn = org;}

private:
	T_TXEDIT_FUSEN	fsn;		// 保持している付箋固有データ
	T_TXEDIT_FUSEN	org;		// 起動元から読み込んだ時の内容
};

#endif	// _T_TXEDIT_FUSEN_H
