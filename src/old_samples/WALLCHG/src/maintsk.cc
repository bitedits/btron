//
//	maintsk.cc (壁紙変更/主幹処理部)
//
//	(C) Copyright 2001 by Personal Media Corporation.
//

#include	<basic.h>
#include	<btron/btron.h>
#include	<btron/libapp.h>
#include	<bstring.h>
#include	<errcode.h>

#include	<new>
#include	<memory>

#include	"val.h"
#include	"dbox.h"
#include	"err.h"
#include	"except.h"
#include	"debug.h"

#include	"appl.h"
#include	"fusen.h"
#include	"sendmsg.h"
#include	"wallchg.h"
#include	"maintsk.h"
#include	"subtsk.h"
#include	"setuppnl.h"


// ----------------------------------------------------- MAINTSK 内 public 関数
//
// constructor
//
MAINTSK::MAINTSK()
	: loop(true)
{
	if (wdef_fep(1) < ER_OK) {
		throw EXCEPT_MAINTSK(WALLERR::NOREGFEP);
	}
}

//
// destructor
//
MAINTSK::~MAINTSK()
{
	wdef_fep(0);
}


//
// メッセージの受信待ち(event loop の代わり)
//
void	MAINTSK::recv_msgloop() throw()
{
	do {
		// window は持たないが、他の雑多 message も見るため
		W	type;
		bool	pass;

		pass = true;
		type = wget_evt(&wevt, WAIT);
		switch (type) {
#if	0				// fep だと、これは取れない
			case EV_MSG:
				pass = recv_mesg((MESSAGE*)(&wevt.g.data[0]));
				break;
#else	// 0				// fepなので EV_NULL 時に自分で確認する
			case EV_NULL:
				MESSAGE	msg;

				if (rcv_msg(SENDMSG::CHG_MSGMASK, &msg, sizeof(msg), NOWAIT | NOCLR) >= ER_OK) {
					pass = recv_mesg(&msg);
				}
				break;
#endif	// 0
			case EV_REQUEST:
				if ((wevt.r.cmd == W_FINISH) ||
				    (wevt.r.cmd == W_DELETE)) {
					// subtask を強制停止
					appl->stsk=std::auto_ptr<SUBTSK>(NULL);
					wrsp_evt(&wevt, 0);
					DPRINT(("terminate main task.\n"));
					loop = false;
					pass = false;
				}
				break;
		}
		if (pass) {
			wpas_evt(&wevt);
		}
		wai_prc(0);
	} while (loop);

	return;
}


// ---------------------------------------------------- MAINTSK 内 private 関数
//
// メッセージの受信
//
// 自分用のメッセージだった場合は false を返す(次の process に送ってはいけない
// メッセージだったことを意味する)
//
bool	MAINTSK::recv_mesg(MESSAGE* wmsg)
{
	bool	flg;

	flg = true;
	DPRINT(("recive message.\n"));
	if (wmsg->msg_type == static_cast<W>(SENDMSG::CHG_MSGTYPE)) {
		MESSAGE	msg;

		DPRINT(("my message\n."));
		if (rcv_msg(SENDMSG::CHG_MSGMASK, &msg, sizeof(msg), NOWAIT | NOCLR) >= ER_OK) {
			if (memcmp(msg.msg_body.ANYMSG.msg_str, SENDMSG::TYPE0_STR, SENDMSG::SIZE_TYPE0_STR) == 0) {
				// 終了要求
				flg = false;
				loop = false;
				DPRINT(("terminate main task.\n"));
			} else if (memcmp(msg.msg_body.ANYMSG.msg_str, SENDMSG::TYPE1_STR, SENDMSG::SIZE_TYPE1_STR) == 0) {
				// 表示変更
				flg = false;
				try {
					WALLCHG	wchg;

					wchg.exec();
					appl->start_subtsk();
				} catch (EXCEPT_WALLCHG& err) {
					// 変えられなかった時は単に無視
					DPRINT(("can't change(%d).\n", err.get_err() >> 16));
				} catch (...) {
					// 何でもいいから失敗したら exit
					loop = false;
				}
			} else if (memcmp(msg.msg_body.ANYMSG.msg_str, SENDMSG::TYPE2_STR, SENDMSG::SIZE_TYPE2_STR) == 0) {
				flg = false;
				appl->stsk = std::auto_ptr<SUBTSK>(NULL);
				wdef_fep(0);	// panel を開けたいので
				try {
					SETUPPNL	pnl;
 
					if (pnl.exec() == 1) {
						if((appl->fsn->get_min() > 0)||
						   (appl->fsn->get_sec() > 0)){
							appl->create_subtsk();
						} else {
							// 待ちを0にしたら終了
							loop = false;
						}
					} else {
						loop = false;
					}
				} catch (...) {
					// 何でもいいから失敗したら exit
					loop = false;
				}
				wdef_fep(1);
			}
			if (!(flg)) {
				// pass してはいけないので消す
				clr_msg(MM_ALL, MM_ALL);
			}
		}
	}

	return flg;
}
