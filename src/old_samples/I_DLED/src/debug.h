//
//	debug.h (偽仮身一覧/debug 用宣言)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#ifndef	_I_DLEN_DEBUG_H
#define	_I_DLEN_DEBUG_H


#ifdef	DEBUG
#include	<bstdio.h>
#include	<bstdlib.h>

#define	DPRINT(arg)	(void)(printf("%s:%d: ", __FILE__, __LINE__), printf arg)
#else	// DEBUG
#define	DPRINT(arg)	(void)0
#endif	// DEBUG

#endif	// _I_DLEN_DEBUG_H
