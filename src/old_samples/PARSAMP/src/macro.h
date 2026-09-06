//
//	macro.h (パーツ操作例/macro 宣言)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#ifndef	_PARSAMP_MACRO_H
#define	_PARSAMP_MACRO_H


#define	DEF_WINFO	(WINFOREC){-1, 0, {{0, 0, 0, 0}}, {-1, -1, -1}, {0, 0}}
#define	DEF_FUSEN	(PARSAMP_FUSEN){				\
				PARSAMP_FUSEN_DLEN,			\
				PARSAMP_FUSEN_VERSION,			\
				{0, 0},					\
				{{TNULL},				\
				 {TNULL},				\
				 0,					\
				 16,					\
				 true,					\
				 false,					\
				 0					\
				}					\
			}

#endif	// _PARSAMP_MACRO_H
