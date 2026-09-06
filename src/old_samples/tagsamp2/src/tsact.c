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

LOCAL	W	cmd_do(W item)			/* [操作]-[ダミー]メニュー*/
{

			/* 各ページ毎でのメニューに対する動作を呼び出す	*/
	return (*(subfn[subnum].act))(K_MENU(item), 0);

}

LOCAL	FUNCP	mfunc[] = {		/* メニュー処理関数テーブル	*/
		NULL,
		cmd_tool,			/* [小物]		*/
		NULL,
		cmd_end,			/* [終了]		*/
		cmd_do				/* [操作]-[ダミー]	*/
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
		if (cidl_par(mywid, &wevt.s.pos) == 0) {
			setpointer(0x4000 + PS_SELECT, NULL);
		}
	}

	/* テキストボックスなどを常にフォーカスを持った状態にするためのダミー
	   イベント発生用 */
	if (act_box >= 0) {
		wevt.e.type = EV_KEYDWN;
		wevt.e.data.key.code = 0;
		wevt.s.stat &= ~(ES_CMND);
		unget_wevt();
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
		cdsp_pwd(mywid, NULL, P_RDISP);
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
	W	pid;
	W	sts;

	if (wevt.s.type == EV_KEYDWN || wevt.s.type == EV_AUTKEY) {
			/* 常にボックス系パーツにフォーカスを与えるため	*/
		if (wevt.s.type == EV_KEYDWN && wevt.e.data.key.code == 0)
			wevt.s.type = EV_NULL;
		if (act_box < 0) goto EXIT;
		i = act_box;
		pid = subpid[i];
	} else {
		i = pres_tagpnl(tagpnl, &wevt);	/* 見出しが押された?	*/
		if (i >= 0) {
			if (i > 0) switch_subpnl(i);
			goto EXIT;
		}
		sts = cfnd_par(mywid, wevt.s.pos, &pid);
		if (sts == 0) goto EXIT;

		for (i = 0; pid != subpid[i]; i++);
		if (sts >= TB_PARTS && sts <= SB_PARTS) act_box = i;
	}

	for ( ; ; ) {
		sts = cact_par(pid, &wevt);
		if (sts <= 0) break;
		if (sts == P_EVENT) {
			unget_wevt();
			break;
		}
				/* ここでパーツの個別処理を呼び出す	*/
		sts = (*(subfn[subnum].act))(K_PARTS(i), sts);
		if (sts >= 0) {
			rv = sts;
			break;
		}
	}


EXIT:
	return rv;
}
