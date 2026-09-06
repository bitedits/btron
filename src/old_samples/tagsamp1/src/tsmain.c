/*=============================================================================

	tsmain.c : 見出しパネルサンプル メイン

	(C) Copyright 1999 by Personal Media Corporation

=============================================================================*/
#include	"tagsamp.h"
#include	"prototype.h"

/*===================================================================変数宣言*/
EXPORT	W	myvid = -1;		/* 仮身ID			*/
LOCAL	TC	MSG_DBX[] = {		/* データボックスが開けません。	*/
	0x2547, 0x213c, 0x253f, 0x255c, 0x2543, 0x252f, 0x2539, 0x242c, 0x332b,
	0x2431, 0x245e, 0x243b, 0x2473, 0x2123, 0x0000
};

/*===================================================================終了処理*/
LOCAL	VOID	exit_proc(VOID)
{
	setpointer(PS_BUSY, NULL);

	closemenu();
	closedbox();
	if (myvid > 0) oend_prc(myvid, NULL, 0);
	if (mywid > 0) wcls_wnd(mywid, CLR);
	if (pbgpat != NULL && pbgpat != WHITE0) free(pbgpat);
	if (wbgpat != NULL && wbgpat != WHITE0) free(wbgpat);
	if (tagpnl != NULL) del_tagpnl(tagpnl);
}

/*=====================================================================メイン*/
EXPORT	VOID	MAIN(M_EXECREQ *msg)
{
	W	er = 0;
	LINK	lnk;

	switch (msg->type) {			/* 起動方法の確認	*/
		case 0:
			myvid = 0;
			break;
		case DISPREQ:
			oend_req(((M_DISPREQ *)msg)->vid, -1);
			er = -1;
			break;
		case PASTEREQ:
			oend_req(((M_PASTEREQ *)msg)->vid, -1);
			er = -1;
			break;
		case FUSENREQ:
			myvid = ((M_FUSENREQ *)msg)->vid;
			break;
		case EXECREQ:
			myvid = ((M_EXECREQ *)msg)->vid;
			if (msg->mode & 2) break;
		default:
			er = -1;
	}
	if (er < 0) goto EXIT;
						/* データボックスを開く	*/
	prc_inf(0, PI_LINK, (VP)&lnk, sizeof(LINK));
	er = opendbox(&lnk, USER_DATA, 0x2000);
	if (er < ER_OK) {
		sig_buz(0);
		pdsp_msg(MSG_DBX);
		goto EXIT;
	}

	chg_wrk(NULL);
	getscreen();
	initstdpnl();


	er = ex_main(msg);

EXIT:
	exit_proc();
	ext_prc(er);
}
