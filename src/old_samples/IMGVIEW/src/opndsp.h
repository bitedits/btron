//
//	opndsp.h (画像閲覧/開いた仮身の表示処理系ヘッダ)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#ifndef	_IMGVIEW_OPNDSP_H
#define	_IMGVIEW_OPNDSP_H

#include	<basic.h>
#include	<btron/btron.h>


// --------------------------------------------------------------- class OPNDSP
class	OPNDSP {
public:
	OPNDSP(W gid);			// constructor
	~OPNDSP();			// destructor

	// 表示の主幹処理
	void	main();

private:
	W	iwid;			// 内部 window の WID
	W	igid;			// 内部 window の GID
	RECT	vr;			// 内部 window の描画範囲
};

#endif	// _IMGVIEW_OPNDSP_H
