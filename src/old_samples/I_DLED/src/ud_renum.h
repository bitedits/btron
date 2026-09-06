//
//	ud_renum.h (偽仮身一覧/取り消し動作-順位変動)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#ifndef	_I_DLED_UD_RENUM_H
#define	_I_DLED_UD_RENUM_H

#include	<basic.h>
#include	<btron/btron.h>

#include	"ud_ope.h"


// ------------------------------------------------------------- class UD_RENUM
class	UD_RENUM : public UD_OPE {
	typedef	struct {		// 取り消しの内容(仮身 1つ当たり)
		W	vid;		// 対象仮身 ID
		VOBJ*	vptr;		// 仮身情報(環にはならない)
	} UDATA;

public:
	UD_RENUM(UW ptype);		// constructor
	~UD_RENUM();			// destructor

	// 内容の生成
	void	make();

	// 取り消しの実行
	void	exec();

private:
	std::vector<UDATA>	udat;	// 取り消しの内容
};

#endif	// _I_DLED_UD_RENUM_H
