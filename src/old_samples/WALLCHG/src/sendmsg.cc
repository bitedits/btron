//
//	sendmsg.cc (壁紙変更/message 送信部)
//
//	(C) Copyright 2001 by Personal Media Corporation.
//

#include	<basic.h>
#include	<btron/btron.h>
#include	<bstring.h>

#include	"cval.h"
#include	"debug.h"

#include	"sendmsg.h"


// ---------------------------------------- SENDMSG 内 static member 変数の実体
UB	SENDMSG::TYPE0_STR[] = "WALLCHG_EXIT";
UB	SENDMSG::TYPE1_STR[] = "WALLCHG_WCHG";
UB	SENDMSG::TYPE2_STR[] = "WALLCHG_STUP";


// -------------------------------------- SENDMSG 内 public 関数(static member)
//
// メッセージの送信
//
void	SENDMSG::send_mesg(W pid, W type)
{
	MESSAGE	msg;

	switch (type) {
		case CHGTYPE_0:		// 終了要求の送信
			memcpy(msg.msg_body.ANYMSG.msg_str,
						TYPE0_STR, SIZE_TYPE0_STR);
			DPRINT(("send type : CHGTYPE_0\n"));
			break;
		case CHGTYPE_1:		// 表示変更
			memcpy(msg.msg_body.ANYMSG.msg_str,
						TYPE1_STR, SIZE_TYPE1_STR);
			DPRINT(("send type : CHGTYPE_1\n"));
			break;
		case CHGTYPE_2:		// 設定パネル
			memcpy(msg.msg_body.ANYMSG.msg_str,
						TYPE2_STR, SIZE_TYPE2_STR);
			DPRINT(("send type : CHGTYPE_2\n"));
			break;
	}
	msg.msg_type = CHG_MSGTYPE;
	msg.msg_size = sizeof(MESSAGE) - (sizeof(W) * 2);
#ifdef	DEBUG
	DPRINT(("send message : %d\n", snd_msg(pid, &msg, NOWAIT)));
#else	// DEBUG
	snd_msg(pid, &msg, NOWAIT);
#endif	// DEBUG

	return;
}
