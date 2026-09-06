//
//	ud_uback.h (偽仮身一覧/取り消し動作-背景解除)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#ifndef	_I_DLED_UD_UNBACK_H
#define	_I_DLED_UD_UNBACK_H

#include	<basic.h>
#include	<btron/btron.h>

#include	<vector>

#include	"ud_ope.h"


// ------------------------------------------------------------ class UD_UNBACK
class	UD_UNBACK : public UD_OPE {
	typedef	struct {		// 取り消しの内容(1仮身当たり)
		W	vid;		// 仮身 ID
		bool	chg;		// true : 変更対象とする
	} UDATA;

public:
	UD_UNBACK(UW ptype);		// constructor
	~UD_UNBACK();			// destructor

	// 内容の生成
	void	make();

	// 取り消しの実行
	void	exec();

	// 取り消しの取り消し(redo)の生成
	void	redo(const VP dat, UW cnt);

	// 取り消し内容の取得
	const	VP	get_udat() {return udat.begin();}

	// 取り消し内容の個数の取得
	const	UW	get_ucnt() {return udat.size();}

private:
	std::vector<UDATA>	udat;	// 取り消しの内容
};

#endif	// _I_DLED_UD_UNBACK_H
