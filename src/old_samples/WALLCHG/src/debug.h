//
//	debug.h (壁紙変更/debug 用宣言)
//
//	(C) Copyright 2001 by Personal Media Corporation.
//

#ifndef	_WALLCHG_DEBUG_H
#define	_WALLCHG_DEBUG_H


#ifdef	DEBUG
#include	<bstdio.h>

#define	DPRINT(arg)	(void)(printf("%s:%d: ", __FILE__, __LINE__), printf arg)
#else	// DEBUG
#define	DPRINT(arg)	(void)0
#endif	// DEBUG

#endif	// _WALLCHG_DEBUG_H
