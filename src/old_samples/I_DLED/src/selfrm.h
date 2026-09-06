//
//	selfrm.h (偽仮身一覧/選択枠管理系ヘッダ)
//
//	(C) Copyright 2002 by Personal Media Coporation.
//

#ifndef	_I_DLED_SELFRM_H
#define	_I_DLED_SELFRM_H

#include	<basic.h>
#include	<btron/btron.h>
#include	<btron/vobj.h>

#include	<vector>


// --------------------------------------------------------------- class SELFRM
class	SELFRM {
public:
	SELFRM(W pgid);			// constructor
	~SELFRM();			// destructor

	// 選択枠のちらつき動作
	void	blink() {adsp_slt(gid, slist.begin(), -1, 0, 0);}

	// 選択枠を点灯させる
	void	on() {adsp_slt(gid, slist.begin(), 1, 0, 0);}

	// 選択枠を消灯させる
	void	off() {adsp_slt(gid, slist.begin(), 0, 0, 0);}

	// 選択枠を移動させる
	void	move(PNT p) {off(); adsp_slt(gid, slist.begin(), 1, p.x, p.y);}

	// 最外接矩形枠の取得
	const	RECT	get_frect() {return r;}

private:
	W	gid;			// 描画対象 GID
	RECT	r;			// 各選択枠の最外接矩形枠
	std::vector<SEL_LIST>	slist;	// 選択枠群
};

#endif	// _I_DLED_SELFRM_H
