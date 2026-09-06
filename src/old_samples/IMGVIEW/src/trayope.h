//
//	trayope.h (画像閲覧/トレー操舵系ヘッダ)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#ifndef	_IMGVIEW_TRAYOPE_H
#define	_IMGVIEW_TRAYOPE_H

#include	<basic.h>
#include	<btron/btron.h>


// -------------------------------------------------------------- class TRAYOPE
class	TRAYOPE {
public:
	// トレーへ複写
	static	void	push_tray(const BMP* bmp, const CSPEC* cspec);

private:
};

#endif	// _IMGVIEW_TRAYOPE_H
