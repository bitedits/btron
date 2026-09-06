//
//	macro.h (簡易文字列編集/macro 宣言)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#ifndef	_T_TXEDIT_MACRO_H
#define	_T_TXEDIT_MACRO_H


// ウィンドウ情報レコードの初期値
#define	DEF_WINFO	(WINFOREC){					\
				-1,					\
				0,					\
				{{0, 0, 0, 0}},				\
				{-1, -1, -1},				\
				{0, 0}					\
			}

// 付箋固有データの初期値
#define	DEF_FUSEN	(T_TXEDIT_FUSEN){				\
				T_TXEDIT_FUSEN_DLEN,			\
				T_TXEDIT_FUSEN_VERSION			\
			}

#endif	// _T_TXEDIT_MACRO_H
