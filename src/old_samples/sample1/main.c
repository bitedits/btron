/*
	main.c		サンプルプログラム : メイン処理

	(C) Copyright 1998 by Personal Media Corporation
*/
#include	"sample.h"

IMPORT	UH	databox_data[];			/* データボックスデータ */

/*
	サンプルプログラムメイン処理
*/
EXPORT	W	MAIN(MESSAGE *msg)
{
	W		sts;
	M_EXECREQ	exmsg;

	/* 作業ファイルを空にしておく : ディスクを切断できるようにするため */
	chg_wrk(NULL);

	/* システムデータボックスのオープン */
	opendbox(NULL, 0, 0);

	/* データボックス(オンメモリ)のオープン */
	setdbox((VP)databox_data);

	/* スクリーン情報の初期化 */
	getscreen();

	/* 標準パネルの初期化 */
	initstdpnl();

	/* 起動タイプに応じた処理を行う */
	sts = EX_PAR;
	switch(msg->msg_type) {
	case 0:			/* CLI 起動 */
		/* msg の本体は CLI の起動パラメータの文字列:
		   通常はサポートしないが、このサンプルではテスト用として
		   小物起動と同じ処理を行う */

		/* 小物起動用の起動メッセージを生成する */
		exmsg.type = EXECREQ;
		exmsg.size = sizeof(exmsg) - sizeof(W) * 2;
		exmsg.vid = 0;			/* ダミー */
		exmsg.pwid = 0;
		exmsg.mode = 0x3;		/* 小物起動 */
		exmsg.bgcol = RGB_WHITE;
		sts = exec_main(&exmsg);	/* 実行 */
		break;

	case FUSENREQ:		/* 付箋のオープン起動 */
		if (msg->msg_size == sizeof(M_FUSENREQ) - sizeof(W) * 2) {
			/* msg は M_FUSENREQ の形式 :
			   付箋のオープン起動の処理を行う
			   このサンプルでは小物起動と同じ処理を行う */

			/* 小物起動用の起動メッセージを生成する */
			exmsg.type = EXECREQ;
			exmsg.size = sizeof(exmsg) - sizeof(W) * 2;
			exmsg.vid = ((M_FUSENREQ*)msg)->vid;
			exmsg.pwid = 0;
			exmsg.mode = 0x3;		/* 小物起動 */
			exmsg.bgcol = RGB_WHITE;
			sts = exec_main(&exmsg);	/* 実行 */
		}
		break;

	case EXECREQ:		/* 仮身のオープン起動／小物起動 */
		if (msg->msg_size == sizeof(M_EXECREQ) - sizeof(W) * 2) {
			/* msg は M_EXECREQ の形式 :
			   仮身のオープン起動、または小物起動の処理を行う */
			sts = exec_main((M_EXECREQ*)msg);
		}
		break;

	case DISPREQ:		/* 開いた仮身の起動 */
		if (msg->msg_size == sizeof(M_DISPREQ) - sizeof(W) * 2) {
			/* msg は M_DISPREQ の形式:
			   開いた仮身の表示起動の処理を行う
			   このサンプルでは未サポートとする !! */
			sts = EX_NOSPT;
		}
		break;

	case PASTEREQ:		/* データの貼り込み起動 */
		if (msg->msg_size == sizeof(M_PASTEREQ) - sizeof(W) * 2) {
			/* msg は M_PASTEREQ の形式 :
			   データの貼り込み起動の処理を行う
			   このサンプルでは未サポートとする !! */
			sts = EX_NOSPT;
		}
		break;
	}
	/* システムデータボックスのクローズ */
	closedbox();

	/* プロセスの終了 */
	return sts;
}
