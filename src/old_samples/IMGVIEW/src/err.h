//
//	err.h (画像閲覧/内部 error code)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#ifndef	_IMGVIEW_ERR_H
#define	_IMGVIEW_ERR_H

#include	<basic.h>

// ------------------------------------------------------- namespace IMGVIEWERR
namespace	IMGVIEWERR {
	#define	IMGVIEWERR(n)	((-1000 - n) << 16)

        // データボックスが登録できない
	static	const	W	DBOXOPEN = IMGVIEWERR(0);
};

#endif	// _IMGVIEW_ERR_H
