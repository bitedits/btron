//
//	pd_hprs.h (偽仮身一覧/ポインタプレス処理ヘッダ-仮身のハンドル)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#ifndef	_I_DLED_PD_HPRS_H
#define	_I_DLED_PD_HPRS_H

#include	<basic.h>
#include	<btron/btron.h>

#include	"pd_ope.h"


// -------------------------------------------------------------- class PD_HPRS
class	PD_HPRS : public PD_OPE {
public:
	PD_HPRS(W vid, W type);		// constructor
	~PD_HPRS();			// destructor

	// 実行処理部
	void	main();

private:
	W	svid;			// 選択している仮身 ID
	W	vtype;			// 操作対象のハンドル種別
	PNT	np;			// 元々つかんだ点
	SIZE	min;			// 最小仮身サイズ

	// 修正選択状態の確認
	void	chk_modify(PNT& p);

	// 選択枠を設定する
	void	set_selfrm(PNT p);
};

#endif	// _I_DLED_PD_HPRS_H
