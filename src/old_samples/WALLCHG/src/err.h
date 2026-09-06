//
//	err.h (壁紙変更/内部 error code)
//
//	(C) Copyright 2001 by Personal Media Corporation.
//

#ifndef	_WALLCHG_ERR_H
#define	_WALLCHG_ERR_H

#include	<basic.h>

// ---------------------------------------------------------- namespace WALLERR
namespace	WALLERR {
	#define	WALLERR(n)	((-1000 - n) << 16)

        // データボックスが登録できない
	static	const	W	DBOXOPEN = WALLERR(0);

	// fep になれない
	static	const	W	NOREGFEP = WALLERR(1);

	// 背景のレコードではない
	static	const	W	NOBGREC = WALLERR(2);
};

#endif	// _WALLCHG_ERR_H
