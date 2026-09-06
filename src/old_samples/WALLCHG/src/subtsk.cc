//
//	subtsk.cc (壁紙変更/時間待ち用 subtask 処理部)
//
//	(C) Copyright 2001 by Personal Media Corporation.
//

#include	<basic.h>
#include	<btron/btron.h>

#include	"cval.h"
#include	"err.h"
#include	"except.h"
#include	"debug.h"

#include	"sendmsg.h"
#include	"subtsk.h"


// 内部変数
LOCAL	SUBTSK*	stsk = NULL;		// 時間待ち subtask からの参照用


// 内部関数プロトタイプ
LOCAL	VOID	subtsk_fn(W time);


// ----------------------------------------------------------- class 外内部関数
//
// 時間待ち subtask
//
LOCAL	VOID	subtsk_fn(W time)
{
	for ( ; ; ) {
		DPRINT(("start : frequencial event.\n"));
		wai_prc(time);
		DPRINT(("end : frequencial event.\n"));
		SENDMSG::send_mesg(0, SENDMSG::CHGTYPE_1);

		// 壁紙変更でもたつくとやばいので、延々と起床待ちにする
		slp_tsk(-1);
	}

	ext_tsk();			// ここには来ないはず
	stsk->set_tid(-1);

	return;
}


// ------------------------------------------------------ SUBTSK 内 public 関数
//
// constructor
//
SUBTSK::SUBTSK(W time)
	: tid(-1)
{
	stsk = this;

	tid = cre_tsk(&subtsk_fn, -1, time);
	DPRINT(("tid : %d\n", tid));
	if (tid < ER_OK) {
		throw EXCEPT_SUBTSK(tid);
	}
}


//
// destructor
//
SUBTSK::~SUBTSK()
{
	if (tid >= 0) {
		DPRINT(("terminate sub task.\n"));
		ter_tsk(tid);
		tid = -1;
	}
}
