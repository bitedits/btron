//
//	ud_ope.h (偽仮身一覧/取り消し動作基底 class ヘッダ)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#ifndef	_I_DLED_UD_OPE_H
#define	_I_DLED_UD_OPE_H

#include	<basic.h>
#include	<btron/btron.h>


// 参照 class の宣言
class	UD_MOVE;
class	UD_ADD;
class	UD_DEL;
class	UD_TRAYADD;
class	UD_RESIZE;
class	UD_HOLD;
class	UD_UNHOLD;
class	UD_BACK;
class	UD_UNBACK;
class	UD_RENUM;


// --------------------------------------------------------------- class UD_OPE
class	UD_OPE {
public:
	enum UD_TYPE {			// 取り消し種別
		UT_MOVE    = 1,		// 位置移動
		UT_ADD     = 2,		// 仮身追加
		UT_DEL     = 3,		// 仮身削除(一時削除化)
		UT_TRAYADD = 4,		// 仮身追加(トレーから移動のみ)
		UT_TRAYDEL = 5,		// 仮身削除(トレーから移動の取り消し時)
		UT_RESIZE  = 6,		// 形状変更
		UT_HOLD    = 7,		// 固定化
		UT_UNHOLD  = 8,		// 固定解除
		UT_BACK    = 9,		// 背景化
		UT_UNBACK  = 10,	// 背景解除
		UT_FRONT   = 11,	// いちばん前へ
		UT_REAR    = 12,	// いちばん後ろへ

		UT_NONE = 0		// 不定(取り消しは生成されない)
	};

	UD_OPE();			// constructor
	virtual	~UD_OPE() {}		// destructor

	// undo の生成(新規生成)(static member)
	static	void	make_undo(UW utype) throw();

	// undo の実行 - redo の生成(static member)
	static	void	exec_undo() throw();

	// undo の廃棄 - 更新状態の解除(static member)
	static	void	clear_undo() throw();

	//  内容の生成
	virtual	void	make() {}

	// 取り消しの実行
	virtual	void	exec() {}

	// 取り消しの取り消し(redo)の生成
	virtual	void	redo(const VP dat, UW cnt) {}

	// 取り消し内容の取得
	virtual	const	VP	get_udat() {return NULL;}

	// 取り消し内容の個数の取得
	virtual	const	UW	get_ucnt() {return 0;}

	// 種別の取得
	const	UW	get_type() {return type;}

private:
	UW	type;			// undo 方法

	friend	UD_MOVE;
	friend	UD_ADD;
	friend	UD_DEL;
	friend	UD_TRAYADD;
	friend	UD_RESIZE;
	friend	UD_HOLD;
	friend	UD_UNHOLD;
	friend	UD_BACK;
	friend	UD_UNBACK;
	friend	UD_RENUM;
};

#endif	// _I_DLED_UD_OPE_H
