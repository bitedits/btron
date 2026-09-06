//
//	imgope.h (画像閲覧/表示画像管理系ヘッダ)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#ifndef	_IMGVIEW_IMGOPE_H
#define	_IMGVIEW_IMGOPE_H

#include	<basic.h>
#include	<btron/btron.h>
#include	<btron/dp.h>


// --------------------------------------------------------------- class IMGOPE
class	IMGOPE {
	static	const	W	TYPE_MAX = 3;	// 読み込み形式の最大数

public:
	IMGOPE(const LINK* lnk);	// constructor
	~IMGOPE();			// destructor

	static	H	opt_tbl[TYPE_MAX];	// IMG_COMPACT.opt の table
	static	H	mthd_tbl[TYPE_MAX];	// IMG_COMPACT.method の table

	// 元の画像の大きさの取得
	const	RECT	get_srect() {return sbmp.bounds;}

	// 表示している範囲の取得
	const	RECT	get_drect() {return drect;}

	// 表示している画像の BMP の取得
	const	BMP*	get_dbmp() {return &dbmp;}

	// 表示している画像の CSPEC の取得
	const	CSPEC*	get_cspec() {return &sc_spec;}

	// 画像の取得
	void	disp_grph(W gid, RECT vr, RECT dr, PNT vp, W dzfact);

private:
	W	sgid;			// 元々の画像の描画環境 ID
	W	dgid;			// 表示用画像の描画環境 ID
	W	zfact;			// 表示倍率
	BMP	sbmp;			// 元々の画像
	BMP	dbmp;			// 表示用画像
	RECT	vrect;			// 表示先の矩形範囲
	RECT	drect;			// 元々の画像の表示範囲
	CSPEC	sc_spec;		// SCREEN の CSPEC
};

#endif	// _IMGVIEW_IMGOPE_H
