//
//	pd_vqp.h (偽仮身一覧/ポインタクイックプレス処理ヘッダ-仮身をつかむ)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#ifndef	_I_DLED_PD_VQP_H
#define	_I_DLED_PD_VQP_H

#include	<basic.h>
#include	<btron/btron.h>

#include	"pd_ope.h"


// --------------------------------------------------------------- class PD_VQP
class	PD_VQP : public PD_OPE {
public:
	PD_VQP(W vid);			// constructor
	~PD_VQP();			// destructor

	// 実行処理部
	void	main();

private:
	W	svid;			// 操作対象の仮身 ID
	SIZE	gap;			// 仮身の左上原点からの間隙
	RECT	vr;			// 仮身の領域
	RECT	frect;			// 全画面領域
	VOBJ*	vobj;			// 操作対象の仮身情報
};

#endif	// _I_DLED_PD_VQP_H
