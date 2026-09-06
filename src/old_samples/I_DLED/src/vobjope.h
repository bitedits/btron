//
//	vobjope.h (偽仮身一覧/単一仮身/付箋管理系ヘッダ)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#ifndef	_I_DLED_VOBJ_H
#define	_I_DLED_VOBJ_H

#include	<basic.h>
#include	<btron/btron.h>
#include	<btron/vobj.h>
#include	<bstring.h>

#include	<new>
#include	<vector>


// ----------------------------------------------------------------- class VOBJ
class	VOBJ {
public:
	VOBJ(const VLINK* lnk, const VP dat, W len, W pwid, const VOBJ* ptr);	// constructor(仮身/付箋を登録)
	VOBJ(W avid, bool vtype, W len, W pwid, const VOBJ* ptr);	// constructor(仮身ID を追加する)
	~VOBJ();			// destructor

	friend	bool	operator==(const VOBJ&, const VOBJ& ) throw();
	friend	bool	operator!=(const VOBJ&, const VOBJ& ) throw();
	VOBJ&	operator=(const VOBJ& ) throw(std::bad_alloc);

	// 取得系
	// 仮身 ID の取得
	const	W	get_vid() {return vid;}

	// 種別の取得
	const	bool	get_type() {return type;}

	// 固定化状態の取得
	const	bool	get_hold() {return hold;}

	// 背景化状態の取得
	const	bool	get_back() {return back;}

	// 一時削除状態の取得
	const	bool	get_del() {return del;}

	// 選択状態の取得
	const	bool	get_sel() {return sel;}

	// VLINK の取得
	const	VLINK*	get_vlnk() {return &vlnk;}

	// segment 長さの取得
	const	W	get_seglen() {return buf.size();}

	// segment の内容の取得(VOBJSEG 形式)
	const	VOBJSEG*	get_vobjseg() {return reinterpret_cast<const VOBJSEG*>(buf.begin());}

	// segment の内容の取得(FUSENSEG 形式)
	const	FUSENSEG*	get_fusenseg() {return reinterpret_cast<const FUSENSEG*>(buf.begin());}

	// 設定系
	// 固定化状態の設定
	void	set_hold(bool flg);

	// 背景化状態の設定
	void	set_back(bool flg);

	// 一時削除状態の設定
	void	set_del(bool flg);

	// 選択状態の設定
	void	set_sel(bool flg);

	// VLINK の内容の差し替え(更新)
	void	set_vlnk(const VLINK* lnk);

	// segment の内容の差し替え(更新)
	void	set_segdat(const VP dat, W len);

	// その他
	// VLINK, segment の内容の取得(更新)
	void	upd_mydat(bool lupd, bool supd);

	// 仮身の default application での実行
	void	exec_vobj();

	// list 管理用
	const	VOBJ*	get_prev() {return prev;}
	const	VOBJ*	get_next() {return next;}
	void	set_prev(const VOBJ* ptr) {prev = const_cast<VOBJ*>(ptr);}
	void	set_next(const VOBJ* ptr) {next = const_cast<VOBJ*>(ptr);}

private:
	W	wid;			// 所属元 window ID
	W	vid;			// 仮身 ID
	bool	type;			// true : 仮身     false : 付箋
	bool	hold;			// 固定化
	bool	back;			// 背景化
	bool	del;			// 一時削除
	bool	sel;			// 選択状態
	VLINK	vlnk;			// 対象実身への VLINK
	std::vector<UB>	buf;		// 実際の内容の保管領域

	VOBJ*	prev;			// (順番で)前の仮身
	VOBJ*	next;			// (順番で)後ろの仮身
};

#endif	// _I_DLED_VOBJ_H
