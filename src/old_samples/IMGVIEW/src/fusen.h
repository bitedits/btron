//
//	fusen.h (画像閲覧/付箋管理ヘッダ)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#ifndef	_IMGVIEW_FUSEN_H
#define	_IMGVIEW_FUSEN_H

#include	<basic.h>
#include	<btron/btron.h>

#include	"struct.h"


// ---------------------------------------------------------------- class FUSEN
class	FUSEN {
public:
	FUSEN(const MESSAGE* msg);	// constructor
	~FUSEN();			// destructor

	// 内部情報の取得
	const	RECT	get_wrect()	{return fsn.wattr.rect;}
	const	WINATTR	get_wattr()	{return fsn.wattr;}
	const	PNT	get_vpos()	{return fsn.vp;}
	const	UW	get_zfact()	{return fsn.zfact;}

	// 内部情報の更新
	void	set_wrect(RECT r)	{fsn.wattr.rect = r;}
	void	set_wattr(WINATTR par)	{fsn.wattr = par;}
	void	set_vpos(PNT p)		{fsn.vp = p;}
	void	set_zfact(UW val)	{fsn.zfact = val;}

	// 付箋を出力する
	W	write_fusen(const MESSAGE* msg, W vid);

private:
	IMGVIEW_FUSEN	fsn;		// 保持している付箋固有データ
	IMGVIEW_FUSEN	org;		// 起動元から読み込んだ時の内容
};

#endif	// _IMGVIEW_FUSEN_H
