//
//	debug.h (´Ê°×Ê¸»úÎóÊÔ½¸/debug ÍÑÀë¸À)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#ifndef	_T_TXEDIT_DEBUG_H
#define	_T_TXEDIT_DEBUG_H


#ifdef	DEBUG
#include	<bstdio.h>
#include	<bstdlib.h>

#define	DPRINT(arg)	(void)(printf("%s:%d: ", __FILE__, __LINE__), printf arg)
#else	// DEBUG
#define	DPRINT(arg)	(void)0
#endif	// DEBUG

#endif	// _T_TXEDIT_DEBUG_H
