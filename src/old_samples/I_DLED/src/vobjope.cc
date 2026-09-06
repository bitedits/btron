//
//	vobjope.cc (偽仮身一覧/単一仮身/付箋管理系)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#include	<basic.h>
#include	<btron/btron.h>
#include	<btron/vobj.h>
#include	<btron/libapp.h>
#include	<bstring.h>

#include	<new>
#include	<memory>
#include	<vector>

#include	"err.h"
#include	"except.h"
#include	"debug.h"

#include	"vobjope.h"


// -------------------------------------------------------- VOBJ 内 public 関数
//
// constructor(仮身/付箋を登録する)
//
VOBJ::VOBJ(const VLINK* lnk, const VP dat, W len, W pwid, const VOBJ* ptr)
	: wid(pwid), vid(-1), type(lnk != NULL),
	  hold(false), back(false), del(false), sel(false),
	  buf(len),
	  prev(const_cast<VOBJ*>(ptr))
{
//	DPRINT(("VOBJ constructor\n"));

	memcpy(buf.begin(), dat, buf.size());
	if (type) {
		vlnk = *lnk;
	}
	vid = oreg_vob((VLINK*)lnk, dat, pwid, V_NODISP);
//	DPRINT(("vid : %d(%d)\n", vid, vid >> 16));
	if (vid < ER_OK) {
		throw EXCEPT_VOBJ(vid);
	}
}


//
// constructor(仮身ID を追加する)
//
VOBJ::VOBJ(W avid, bool vtype, W len, W pwid, const VOBJ* ptr)
	: wid(pwid), vid(avid), type(vtype),
	  hold(false), back(false), del(false), sel(false),
	  buf(len),
	  prev(const_cast<VOBJ*>(ptr))
{
	oget_vob(vid, (vtype) ? &vlnk : NULL, buf.begin(), buf.size(), NULL);
}


//
// destructor
//
VOBJ::~VOBJ()
{
//	DPRINT(("VOBJ destructor(vid : %d)\n", vid));

	odel_vob(vid, 0);
}


//
// 演算子 == への動作(VOBJ friend)
//
bool	operator==(const VOBJ& lhs, const VOBJ& rhs) throw()
{
	return ((lhs.wid == rhs.wid) &&
		(lhs.vid == rhs.vid) &&
		(lhs.type == rhs.type) &&
		(lhs.hold == rhs.hold) &&
		(lhs.back == rhs.back) &&
		(lhs.del == rhs.del) &&
		(lhs.sel == rhs.sel) &&
		(lhs.buf.size() == rhs.buf.size()) &&
		((memcmp(lhs.buf.begin() ,rhs.buf.begin(), lhs.buf.size()) == 0)));
}


//
// 演算子 != への動作(VOBJ friend)
//
bool	operator!=(const VOBJ& lhs, const VOBJ& rhs) throw()
{
	W	size;

	size = (lhs.buf.size() <= rhs.buf.size()) ? lhs.buf.size() : rhs.buf.size();

	return ((lhs.wid != rhs.wid) ||
		(lhs.vid != rhs.vid) ||
		(lhs.type != rhs.type) ||
		(lhs.hold != rhs.hold) ||
		(lhs.back != rhs.back) ||
		(lhs.del != rhs.del) ||
		(lhs.sel != rhs.sel) ||
		(lhs.buf.size() != rhs.buf.size()) ||
		((memcmp(lhs.buf.begin() ,rhs.buf.begin(), size) != 0)));
}


//
// 演算子 = への動作
//
VOBJ&	VOBJ::operator=(const VOBJ& vbuf) throw(std::bad_alloc)
{
	if (vbuf != *this) {
		// 諸々の転写
		wid = vbuf.wid;
		vid = vbuf.vid;
		type = vbuf.type;
		hold = vbuf.hold;
		back = vbuf.back;
		del = vbuf.del;
		sel = vbuf.sel;
		prev = vbuf.prev;
		next = vbuf.next;

		// segment の内容の転写
		buf.resize(vbuf.buf.size());
		memcpy(buf.begin(), vbuf.buf.begin(), vbuf.buf.size());
	}

	return *this;
}


//
// 固定化状態の設定
//
void	VOBJ::set_hold(bool flg)
{
	if (flg) {
		// 背景化状態か一時削除状態の時は固定化不能
		if ((back) || (del)) {
			flg = false;
		}
	}
	hold = flg;

	return;
}


//
// 背景化状態の設定
//
void	VOBJ::set_back(bool flg)
{
	if (flg) {
		// 一時削除状態の時は背景化不能
		if (del) {
			flg = false;
		} else {
			// 背景化設定時は固定化/選択状態は解除される
			hold = false;
			sel = false;
		}
	} else if (back) {
		// 背景化解除時は選択状態に移行(背景化されたいたもののみ)
		sel = true;
	}
	back = flg;

	return;
}


//
// 一時削除状態の設定
//
void	VOBJ::set_del(bool flg)
{
	if (flg) {
		// 固定化状態か背景化状態の時は削除不能
		if ((hold) || (back)) {
			flg = false;
		} else {
			// 一時削除時は選択状態は解除される
			sel = false;
		}
	}

	del = flg;
	omov_vob(vid, (flg) ? 0xffff8000 : wid, NULL, V_NODISP);
	reggabage((LINK*)&vlnk, (flg) ? 1 : 0);

	return;
}


//
// 選択状態の設定
//
void	VOBJ::set_sel(bool flg)
{
	if (flg) {
		// 背景化状態か一時削除状態の時は選択不能
		if ((back) || (del)) {
			flg = false;
		}
	}
	sel = flg;

	return;
}


//
// VLINK の内容の差し替え(更新)
//
void	VOBJ::set_vlnk(const VLINK* lnk)
{
	if ((type) && (lnk != NULL)) {
		// 付箋でないことを確認しておく
		vlnk = *lnk;
	}

	return;
}


//
// segment の内容の差し替え(更新)
//
void	VOBJ::set_segdat(const VP dat, W len)
{
	try {
		buf.resize(len);
	} catch (std::bad_alloc) {
		throw EXCEPT_VOBJ(ER_NOMEM);
	} catch (...) {
		throw EXCEPT_VOBJ(ER_SYS);
	}
	memcpy(buf.begin(), dat, buf.size());

	return;
}


//
// VLINK, segment の内容の取得(更新)
//
// lupd : VLINK の更新
// supd : VOBJSEG(ないし FUSENSEG) の更新
//
void	VOBJ::upd_mydat(bool lupd, bool supd)
{
	UW	len;

	oget_vob(vid, (lupd) ? &vlnk : NULL, NULL, 0, &len);
	if (supd) {
		try {
			buf.resize(len);
			oget_vob(vid, NULL, buf.begin(), len, NULL);
		} catch (std::bad_alloc) {
			throw EXCEPT_VOBJ(ER_NOMEM);
		} catch (...) {
			throw EXCEPT_VOBJ(ER_SYS);
		}
	}

	return;
}


//
// 仮身の default application での実行
//
void	VOBJ::exec_vobj()
{
	setpointer(PS_BUSY, NULL);
	if (oexe_apg(vid, 0) > 0) {
		setpointer(0x8001, NULL);
	}

	return;
}
