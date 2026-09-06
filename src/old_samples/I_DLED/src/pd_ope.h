//
//	pd_ope.h (偽仮身一覧/ポインタ操作基底部ヘッダ)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#ifndef	_I_DLED_PD_OPE_H
#define	_I_DLED_PD_OPE_H

#include	<basic.h>
#include	<btron/btron.h>


// 参照 class の宣言
class	PD_NPRS;
class	PD_HPRS;
class	PD_VPRS;
class	PD_VQP;


// --------------------------------------------------------------- class PD_OPE
class	PD_OPE {
public:
	PD_OPE();			// constructor
	virtual	~PD_OPE() {}		// destructor

	// 実行処理部
	virtual	void	main() {}

private:
	// drag 操作関連
	W	wid;
	W	dgid;			// drag 描画環境
	W	dir;			// 束縛方法(1:水平  2:垂直  3:正方)
	RECT	vrect;			// 自ウィンドウ作業領域
	W	hjmp;			// 水平スクロール量
	W	vjmp;			// 垂直スクロール量
	PNT	sp;			// 選択枠始点
	PNT	vp;			// 表示原点
	PNT	vpN;			// 現在の表示原点
	bool	on;			// 自ウィンドウ枠上にいる
	bool	in;			// 自ウィンドウ作業領域内にいる
	bool	mod;			// true : 修正選択指状態
	bool	lock;			// 描画環境を lock している
	RECT	lrect;			// scroll の制限範囲
	SEL_RGN	srgn;			// 選択枠

	friend	PD_NPRS;
	friend	PD_HPRS;
	friend	PD_VPRS;
	friend	PD_VQP;

	// ウィンドウ枠上でのスクロール位置の設定
	void	set_spos(PNT p);

	// 選択枠の生成
	void	gen_selfrm(PNT p1, PNT p2);

	// 選択枠を移動する
	void	move_selfrm(RECT r, PNT p, SIZE gap);
};

#endif	// _I_DLED_PD_OPE_H
