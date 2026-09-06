//
//	sendmsg.h (壁紙変更/message 送信部ヘッダ)
//
//	(C) Copyright 2001 by Personal Media Corporation.
//

#ifndef	_WALLCHG_SENDMSG_H
#define	_WALLCHG_SENDMSG_H

#include	<basic.h>
#include	<btron/btron.h>


// -------------------------------------------------------------- class SENDMSG
class	SENDMSG {
public:
	enum CHGTYPE {			// メッセージの種別
		CHGTYPE_0 = 0,		// 終了要求
		CHGTYPE_1 = 1,		// 壁紙変更要求
		CHGTYPE_2 = 2		// 設定パネル要求
	};
	static	UB	TYPE0_STR[13];
	static	UB	TYPE1_STR[13];
	static	UB	TYPE2_STR[13];
	static	const	UW	CHG_MSGTYPE = MS_TYPE7;
	static	const	UW	CHG_MSGMASK = MM_TYPE7;
	static	const	UW	SIZE_TYPE0_STR = sizeof(TYPE0_STR);
	static	const	UW	SIZE_TYPE1_STR = sizeof(TYPE1_STR);
	static	const	UW	SIZE_TYPE2_STR = sizeof(TYPE2_STR);

	// メッセージの送信
	static	void	send_mesg(W pid, W type);

private:
};

#endif	// _WALLCHG_SENDMSG_H
