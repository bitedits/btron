//
//	fusen.h (偽仮身一覧/付箋管理ヘッダ)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#ifndef	_I_DLED_FUSEN_H
#define	_I_DLED_FUSEN_H

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
	const	RECT	get_area()	{return fsn.area;}
	const	RECT	get_wrect()	{return fsn.wattr.rect;}
	const	WINATTR	get_wattr()	{return fsn.wattr;}
	const	WINCOL	get_wcol()	{return fsn.wcol;}
	const	TIDYDAT	get_tidy()	{return fsn.tidy;}

	// 内部情報の更新
	void	set_vpos(PNT p)		{fsn.vp = p;}
	void	set_area(RECT r)	{fsn.area = r;}
	void	set_wrect(RECT r)	{fsn.wattr.rect = r;}
	void	set_wattr(WINATTR par)	{fsn.wattr = par;}
	void	set_wcol(WINCOL wcol)	{fsn.wcol = wcol;}
	void	set_tidy(TIDYDAT dat)	{fsn.tidy = dat;}

	// 付箋を出力する
	W	write_fusen(const MESSAGE* msg, W vid, bool eflg = true);

	// 付箋固有データの内容を起動前の状態に戻す
	void	undo_fusen() {fsn = org;}

private:
	I_DLED_FUSEN	fsn;		// 保持している付箋固有データ
	I_DLED_FUSEN	org;		// 起動元から読み込んだ時の内容
};

#endif	// _I_DLED_FUSEN_H
