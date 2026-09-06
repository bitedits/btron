//
//	editobj.cc (簡易文字列編集/編集対象実身管理系)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#include	<basic.h>
#include	<btron/btron.h>
#include	<errcode.h>
#include	<tstring.h>

#include	"editobj.h"


// ----------------------------------------------------- EDITOBJ 内 public 関数
//
// constrcutor
//
EDITOBJ::EDITOBJ(const LINK* plnk)
	: edit(false), save(false), skip(false), lnk(*plnk)
{
	tc_strset(name, TNULL, L_FNM + 1);
	fil_sts((LINK*)plnk, name, &fstat, NULL);
}


//
// destructor
//
EDITOBJ::~EDITOBJ()
{
}


//
// 他から更新されているかを確認する
//
const	bool	EDITOBJ::chk_update()
{
	bool	flg;
	F_STATE	fbuf;

	if (fil_sts(&lnk, NULL, &fbuf, NULL) >= ER_OK) {
		flg = (fstat.f_mtime != fbuf.f_mtime);
	} else {
		flg = false;
	}

	return flg;
}


// 書き込み不可かを確認する
const	bool	EDITOBJ::chk_ronly()
{
	return (chk_fil(&lnk, F_WRITE, NULL) < ER_OK);
}


//
// 管理情報の更新
//
void	EDITOBJ::upd_mydat()
{
	tc_strset(name, TNULL, L_FNM + 1);
	fil_sts(&lnk, name, &fstat, NULL);

	return;
}
