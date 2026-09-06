//
//	kb_del.h (偽仮身一覧/キーボード操作系-削除処理)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#ifndef	_I_DLED_KB_DEL_H
#define	_I_DLED_KB_DEL_H

#include	<basic.h>
#include	<btron/btron.h>

#include	"kb_ope.h"


// --------------------------------------------------------------- class KB_DEL
class	KB_DEL : public KB_OPE {
public:
	KB_DEL();			// constructor
	~KB_DEL();			// destructor

	// 実行処理
	void	main();

private:
};

#endif	// _I_DLED_KB_DEL_H
