/*=============================================================================

	tsact.c : 見出しパネルサンプル イベント処理関数群

	(C) Copyright 1999 by Personal Media Corporation

=============================================================================*/
#include	"tagsamp.h"
#include	"prototype.h"

/*===================================================================定数宣言*/
#define	ES_CMND	(ES_CMD | ES_BUT2)		/* メニュー指定		*/

/*=========================================================メニュー処理関数群*/
LOCAL	W	cmd_tool(W item)		/* [小物]メニュー	*/
{
	setpointer(PS_BUSY, NULL);
	if (oexe_apg(0, item) > 0) setpointer(0x8001, NULL);

	return 0;
}


LOCAL	W	cmd_end(W item)			/* [終了]メニュー	*/
{

	return evt_fin(0, 0);

}

LOCAL	FUNCP	mfunc[] = {		/* メニュー処理関数テーブル	*/
		NULL,
		cmd_tool,			/* [小物]		*/
		NULL,
		cmd_end,			/* [終了]		*/
	};

/*=========================================================イベント処理関数群*/
EXPORT	W	evt_menu(VOID)			/* メニューイベント	*/
{

	return selmenu(-1, mfunc);

}

EXPORT	VOID	evt_idle(VOID)			/* アイドル処理		*/
{
	if (!(wevt.s.stat & ES_CMND) && (wevt.s.wid == mywid) &&
					(wevt.s.cmd == W_WORK)) {
			setpointer(0x4000 + PS_SELECT, NULL);
	}
}

EXPORT	W	evt_msg(MESSAGE *msg)		/* メッセージ処理	*/
{
	setpointer(0x8000, NULL);
	clr_msg(MM_ALL, MM_ALL);

	if (msg->msg_type == MS_TMOUT) refresh();

	return 0;
}

EXPORT	VOID	evt_chgstate(W ix, W sts)	/* 状態変化処理		*/
{

	setpointer(0x8000, NULL);
	if (sts != 0x0100) {
		if (sts == 2) {			/* W_SWITCHを受けた	*/
			/* アプリケーションがPASTEを受け入れるように作るのな
			   ら、ここでペーストを受け入れた後の動作をかいたり
			   する必要がある */
		}
	}

}

EXPORT	W	evt_fin(W ix, W mode)		/* 終了要求処理		*/
{

	return 1;

}

EXPORT	W	evt_key(VOID)			/* キー入力処理		*/
{
	W	rv;

	if (wevt.s.stat & ES_CMND) {
		rv = evt_menu();
	} else {
		rv = evt_press(0);
	}

	return rv;
}

EXPORT	W	evt_press(W ix)			/* ポインタプレス処理	*/
{
	W	i;
	W	rv = 0;

	i = pres_tagpnl(tagpnl, &wevt);	/* 見出しが押された?	*/
	if (i >= 0) {
		if (i > 0) switch_subpnl(i);
	}

	return rv;
}
