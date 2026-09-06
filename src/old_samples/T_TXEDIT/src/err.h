//
//	err.h (簡易文字列編集/内部 error code)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#ifndef	_T_TXEDIT_ERR_H
#define	_T_TXEDIT_ERR_H

#include	<basic.h>

// ------------------------------------------------------ namespace T_TXEDITERR
namespace	T_TXEDITERR {
	#define	T_TXEDITERR(n)	((-1000 - n) << 16)

        // データボックスが登録できない
	static	const	W	DBOXOPEN = T_TXEDITERR(0);

	// 付箋起動は禁止
	static	const	W	FSNEXEC = T_TXEDITERR(1);

	// PD 操作は実行されなかった
	static	const	W	PD_NOOPE = T_TXEDITERR(2);

	// keyboard 操作は実行されなかった
	static	const	W	KB_NOOPE = T_TXEDITERR(3);

	// 選択枠は生成されなかった
	static	const	W	NO_SELFRM = T_TXEDITERR(4);

	// 非保存強制終了(付箋固有データも実身も保存しない)
	static	const	W	NO_ALLSAVE = T_TXEDITERR(5);

	// 非保存終了(付箋固有データは保存する)
	static	const	W	NO_UPDATE = T_TXEDITERR(6);

	// 終了拒否/取り消し
	static	const	W	SAVE_CANCEL = T_TXEDITERR(7);

	// 対応していない起動方法
	static	const	W	NOEXEC = T_TXEDITERR(8);
};

#endif	// _T_TXEDIT_ERR_H
