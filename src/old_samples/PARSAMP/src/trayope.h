//
//	trayope.h (パーツ操作例/トレー操舵系ヘッダ)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#ifndef	_PARSAMP_TRAYOPE_H
#define	_PARSAMP_TRAYOPE_H

#include	<basic.h>
#include	<btron/btron.h>


// -------------------------------------------------------------- class TRAYOPE
class	TRAYOPE {
public:
	// (一時含む)トレーへ複写/移動(static member)
	static	void	push_tray(W pid, bool tmp, bool cut);

	// (一時含む)トレーから複写/移動(static member)
	static	W	pop_tray(W pid, bool tmp, bool cut, PNT pos);

private:
};

#endif	// _PARSAMP_TRAYOPE_H
