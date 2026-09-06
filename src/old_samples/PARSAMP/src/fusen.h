//
//	fusen.h (パーツ操作例/付箋管理ヘッダ)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#ifndef	_PARSAMP_FUSEN_H
#define	_PARSAMP_FUSEN_H

#include	<basic.h>
#include	<btron/btron.h>

#include	"struct.h"
#include	"macro.h"


// ---------------------------------------------------------------- class FUSEN
class	FUSEN {
public:
	FUSEN(const MESSAGE* msg);	// constructor
	~FUSEN();			// destructor

	// 内部情報の取得
	const	PNT	get_wpos()	{return fsn.p;}
	const	PARDATA	get_pdat()	{return fsn.pdat;}
	const	TC*	get_str1()	{return fsn.pdat.str1;}
	const	TC*	get_str2()	{return fsn.pdat.str2;}
	const	UW	get_color()	{return fsn.pdat.col;}
	const	UW	get_size()	{return fsn.pdat.size;}
	const	bool	get_enable()	{return fsn.pdat.enbl;}
	const	bool	get_pitch()	{return fsn.pdat.pitch;}

	// 内部情報の更新
	void	set_wpos(PNT p)		{fsn.p = p;}
	void	set_pdat(PARDATA dat)	{fsn.pdat = dat;}
	void	set_color(UW col)	{fsn.pdat.col = col;}
	void	set_size(UW size)	{fsn.pdat.size = size;}
	void	set_enable(bool flg)	{fsn.pdat.enbl = flg;}
	void	set_pitch(bool flg)	{fsn.pdat.pitch = flg;}
	void	set_str1(const TC* str);
	void	set_str2(const TC* str);

	// 付箋を出力する
	W	write_fusen(const MESSAGE* msg, W vid);

	// 付箋固有データの内容を初期化する
	void	init_fusen() {fsn = DEF_FUSEN;}

	// 付箋固有データの内容を起動前に戻す
	void	undo_fusen() {fsn = org;}

	// 付箋固有データの更新を確認する
	bool	chk_fusen();

private:
	PARSAMP_FUSEN	fsn;		// 保持している付箋固有データ
	PARSAMP_FUSEN	org;		// 起動元から読み込んだ時の内容
};

#endif	// _PARSAMP_FUSEN_H
