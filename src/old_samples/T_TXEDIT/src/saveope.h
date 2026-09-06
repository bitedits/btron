//
//	saveope.h (簡易文字列編集/保存処理基幹部ヘッダ)
//
//	(C) Copyright 2002 by Personal Media Corporaion.
//

#ifndef	_T_TXEDIT_SAVEOPE_H
#define	_T_TXEDIT_SAVEOPE_H

#include	<basic.h>
#include	<btron/btron.h>


// -------------------------------------------------------------- class SAVEOPE
class	SAVEOPE {
public:
	SAVEOPE();			// constructor(default)(非確認保存)
	SAVEOPE(bool flg);		// constructor(copy)(確認終了/確認保存)
	SAVEOPE(W pvid, const LINK* plnk);	// constructor(copy)(新規保存)
	~SAVEOPE();			// destructor

	// 新規保存時の保存先の生成(static member)
	static	UW	make_newfile(W& vid, LINK& lnk);

	// 主幹処理部
	void	main();

private:
	W	fd;			// 保存先実身の FD
	W	vid;			// 保存対象実身への仮身の仮身 ID
	bool	newfile;		// true : 新規実身が対象の状態
	bool	hidden;			// true : 隠匿保存状態
	LINK	lnk;			// 保存対象実身
};

#endif	// _T_TXEDIT_SAVEOPE_H
