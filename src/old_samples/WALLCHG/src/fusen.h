//
//	fusen.h (壁紙変更/付箋管理ヘッダ)
//
//	(C) Copyright 2001 by Personal Media Corporation.
//

#ifndef	_WALLCHG_FUSEN_H
#define	_WALLCHG_FUSEN_H

#include	<basic.h>
#include	<btron/btron.h>
#include	<btron/vobj.h>

#include	"struct.h"


// ---------------------------------------------------------------- class FUSEN
class	FUSEN {
public:
	FUSEN(MESSAGE* msg);		// constructor
	~FUSEN();			// destructor

	// 内部情報の取得
	const	UH	get_min() {return fsn.min;}
	const	UH	get_sec() {return fsn.sec;}
	const	UW	get_option() {return fsn.opt;}

	// 内部情報の更新
	void	set_min(UH i) {fsn.min = i;}
	void	set_sec(UH i) {fsn.sec = i;}
	void	set_option(UW opt) {fsn.opt = opt;}

	// 付箋を出力する
	void	write_fusen(MESSAGE* msg);

private:
	WALLCHG_FUSEN	fsn;		// 保持している付箋固有データ
	WALLCHG_FUSEN	org;		// 起動元から読み込んだ時の内容
};

#endif	// _WALLCHG_FUSEN_H
