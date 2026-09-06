//
//	pd_nprs.h (偽仮身一覧/ポインタプレス処理ヘッダ-仮身が無い部分)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#ifndef	_I_DLED_PD_NPRS_H
#define	_I_DLED_PD_NPRS_H

#include	<basic.h>
#include	<btron/btron.h>

#include	"pd_ope.h"


// -------------------------------------------------------------- class PD_NPRS
class	PD_NPRS : public PD_OPE {
public:
	PD_NPRS();			// constructor
	~PD_NPRS();			// destructor

	// 実行処理部
	void	main();

private:
};

#endif	// _I_DLED_PD_NPRS_H
