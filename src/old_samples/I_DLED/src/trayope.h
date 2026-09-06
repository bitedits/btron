//
//	trayope.h (偽仮身一覧/トレー操舵系ヘッダ)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#ifndef	_I_DLED_TRAYOPE_H
#define	_I_DLED_TRAYOPE_H

#include	<basic.h>
#include	<btron/btron.h>


// -------------------------------------------------------------- class TRAYOPE
class	TRAYOPE {
public:
	// (一時含む)トレーへ複写/移動(仮身専用)(static member)
	static	void	push_tray(W vid, bool cut, bool tmp, SIZE gap);

	// (一時含む)トレーから複写/移動(仮身専用)(static member)
	static	W	pop_tray(bool chk, bool tmp, bool cut, bool disp, PNT pos);

private:
};

#endif	// _I_DLED_TRAYOPE_H
