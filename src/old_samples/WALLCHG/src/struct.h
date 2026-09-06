//
//	struct.h (壁紙変更/広域構造体)
//
//	(C) Copyright 2001 by Personal Media Corporation.
//
 
#ifndef	_WALLCHG_STRUCT_H
#define	_WALLCHG_STRUCT_H

#include	<basic.h>

 
// --------------------------------------------------------- 付箋固有データ定義
// opt : (H) _  _  _  _  -  _  _  _  _  =  _  _  _  _  -  _  _  _  _
//       (L) _  _  _  _  -  _  _  _  _  =  _  _  _  _  -  _  _  C  P
//                                                              |  |
//                                                              | 起動時のpanel
//                                                             起動時の壁紙変更
typedef	struct {			// 付箋定義
	UH	dlen;			// データ長さ(byte 単位)
	UH	ver;			// version(0xXYYY = X.YYY)
	UH	min;			// 待機時間(分)
	UH	sec;			// 待機時間(秒)
	UW	opt;			// option
} WALLCHG_FUSEN;
#define	WALLCHG_FUSEN_STRUCT    "hhhhw"
#define	WALLCHG_FUSEN_DLEN      (sizeof(WALLCHG_FUSEN) - sizeof(UH))
#define	WALLCHG_FUSEN_VERSION   0x1000

#endif	// _WALLCHG_STRUCT_H
