//
//	macro.h (偽仮身一覧/macro 宣言)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#ifndef	_I_DLED_MACRO_H
#define	_I_DLED_MACRO_H


// ウィンドウ情報レコードの初期値
#define	DEF_WINFO	(WINFOREC){					\
				-1,					\
				0,					\
				{{0, 0, 0, 0}},				\
				{-1, -1, -1},				\
				{0, 0}					\
			}

// 付箋固有データの初期値
#define	DEF_FUSEN	(I_DLED_FUSEN){					\
				I_DLED_FUSEN_DLEN,			\
				I_DLED_FUSEN_VERSION,			\
				{0, 0},					\
				{{0, 0, 0, 0}},				\
				{{{0, 0, 0, 0}},			\
				 false,					\
				 0					\
				},					\
				{0,					\
				 CVAL::RGB_WHITE,			\
				},					\
				{0,					\
				 0,					\
				 0					\
				}					\
			}

// 仮身セグメントの初期値
#define	DEF_VOBJSEG	(VOBJSEG){					\
				{{0, 0, 0, 0}},				\
				0,					\
				0,					\
				CVAL::RGB_BLACK,			\
				CVAL::RGB_BLACK,			\
				CVAL::RGB_WHITE,			\
				CVAL::RGB_WHITE,			\
				0					\
			}

#endif	// _I_DLED_MACRO_H
