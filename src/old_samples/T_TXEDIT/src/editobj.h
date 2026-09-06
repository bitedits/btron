//
//	editobj.h (簡易文字列編集/編集対象実身管理系ヘッダ)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#ifndef	_T_TXEDIT_EDITOBJ_H
#define	_T_TXEDIT_EDITOBJ_H

#include	<basic.h>
#include	<btron/btron.h>


// -------------------------------------------------------------- class EDITOBJ
class	EDITOBJ {
public:
	EDITOBJ(const LINK* plnk);	// constructor
	~EDITOBJ();			// destructor

	// 実身名の取得
	const	TC*	get_fname() {return name;}

	// 含む LINK 数の取得
	const	W	get_nlnk() {return fstat.f_nlink;}

	// 実身の application type の取得
	const	W	get_atype() {return fstat.f_atype;}

	// 隠匿保存の flag を取得する
	const	bool	get_saveflg() {return save;}

	// 編集済み flag を取得する
	const	bool	get_editflg() {return edit;}

	// 読み飛ばしの flag を取得する
	const	bool	get_skipflg() {return skip;}

	// 更新の有無の確認
	const	bool	chk_update();

	// 書き込み不可かを確認する
	const	bool	chk_ronly();

	// 管理情報の更新
	void	upd_mydat();

	// 隠匿保存の flag を設定する
	void	set_saveflg(bool flg) {save = flg;}

	// 編集済み flag を設定する
	void	set_editflg(bool flg) {edit = flg;}

	// 読み飛ばしの flag を設定する
	void	set_skipflg(bool flg) {skip = flg;}

private:
	bool	edit;			// true : 実身は(自身で)編集済み
	bool	save;			// true : 隠匿保存すべきかも知れない
	bool	skip;			// true : 読み飛ばしがあった
	LINK	lnk;
	F_STATE	fstat;
	TC	name[L_FNM + 1];
};

#endif	// _T_TXEDIT_EDITOBJ_H
