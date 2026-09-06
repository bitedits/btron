//
//	kb_ope.h (偽仮身一覧/キーボード操作系基底 class ヘッダ)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#ifndef	_I_DLED_KB_OPE_H
#define	_I_DLED_KB_OPE_H

#include	<basic.h>
#include	<btron/btron.h>


// --------------------------------------------------------------- class KB_OPE
class	KB_OPE {
public:
	KB_OPE() {}			// constructor
	virtual	~KB_OPE() {}		// destructor

	// 実行処理
	virtual	void	main() {}

private:
};

#endif	// _I_DLED_KB_OPE_H
