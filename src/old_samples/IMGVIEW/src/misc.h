//
//	misc.h (画像閲覧/その他関数群ヘッダ)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#ifndef	_IMGVIEW_MISC_H
#define	_IMGVIEW_MISC_H

#include	<basic.h>
#include	<btron/btron.h>


// ----------------------------------------------------------------- class MISC
class	MISC {
public:
	// 自然数を文字に変換する
	static	void	tc_numtostr(TC* dst, W num, W base);

	// 倍率を table 上の値に読み直す
	static	W	zfact_to_tbl(UW& zfact);

private:
};

#endif	// _IMGVIEW_MISC_H
