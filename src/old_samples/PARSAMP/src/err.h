//
//	err.h (パーツ操作例/内部 error code)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#ifndef	_PARSAMP_ERR_H
#define	_PARSAMP_ERR_H

#include	<basic.h>

// ------------------------------------------------------- namespace PARSAMPERR
namespace	PARSAMPERR {
	#define	PARSAMPERR(n)	((-1000 - n) << 16)

        // データボックスが登録できない
	static	const	W	DBOXOPEN = PARSAMPERR(0);
};

#endif	// _PARSAMP_ERR_H
