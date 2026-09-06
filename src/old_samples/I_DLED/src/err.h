//
//	err.h (偽仮身一覧/内部 error code)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#ifndef	_I_DLED_ERR_H
#define	_I_DLED_ERR_H

#include	<basic.h>

// -------------------------------------------------------- namespace I_DLEDERR
namespace	I_DLEDERR {
	#define	I_DLEDERR(n)	((-1000 - n) << 16)

        // データボックスが登録できない
	static	const	W	DBOXOPEN = I_DLEDERR(0);

	// 付箋起動は禁止
	static	const	W	FSNEXEC = I_DLEDERR(1);

	// PD 操作は実行されなかった
	static	const	W	PD_NOOPE = I_DLEDERR(2);

	// keyboard 操作は実行されなかった
	static	const	W	KB_NOOPE = I_DLEDERR(3);

	// 選択枠は生成されなかった
	static	const	W	NO_SELFRM = I_DLEDERR(4);

	// 非保存強制終了(付箋固有データも実身も保存しない)
	static	const	W	NO_ALLSAVE = I_DLEDERR(5);

	// 非保存終了(付箋固有データは保存する)
	static	const	W	NO_UPDATE = I_DLEDERR(6);

	// 終了拒否/取り消し
	static	const	W	SAVE_CANCEL = I_DLEDERR(7);

	// 対応していない起動方法
	static	const	W	NOEXEC = I_DLEDERR(8);
};

#endif	// _I_DLED_ERR_H
