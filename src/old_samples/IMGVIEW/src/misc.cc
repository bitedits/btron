//
//	misc.cc (画像閲覧/その他の関数群)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#include	<basic.h>
#include	<btron/btron.h>
#include	<tcode.h>

#include	"cval.h"

#include	"misc.h"


// ----------------------------------------- MISC 内 public 関数(static member)
//
// 自然数を文字に変換する
//	末尾に TNULL を付加します。値が負の場合は正の値に戻します。
//
void	MISC::tc_numtostr(TC* dst, W num, W base)
{
	W	idx;
	bool	flg;

	idx = 0;
	flg = false;
	if (num < 0) {
		num *= -1;
	}
	while (base > 0) {
		if ((flg) || (num / base > 0)) {
			dst[idx++] = num / base + TK_0;
			flg = true;
		}
		num %= base;
		base /= 10;
	}
	if (!(flg)) {
		dst[idx++] = TK_0;
	}
	dst[idx] = TNULL;

	return;
}


//
// 倍率を table 上の値に読み直す
//
W	MISC::zfact_to_tbl(UW& zfact)
{
	W	idx;

	if (zfact <= CVAL::Z_MIN) {
		idx = 0;
		zfact = CVAL::Z_MIN;
	} else if (zfact >= CVAL::Z_MAX) {
		idx = CVAL::Z_TBL_MAX - 1;
		zfact = CVAL::Z_MAX;  
	} else { 
		for (idx = 0; idx < (CVAL::Z_TBL_MAX - 1); ++idx) {
			if ((CVAL::Z_TBL[idx] >= zfact) &&
			    (zfact < CVAL::Z_TBL[idx + 1])) {
				break;
			}
		}
	}

	return idx;
}
