/*=============================================================================

	tspage11.c : 見出しパネルのサンプル ページ１の子１（ページ１１）の処理

	(C) Copyright 1999 Personal Media Corporation

=============================================================================*/
#include	"tagsamp.h"
#include	"prototype.h"

/*===================================================================定数宣言*/
#define	PAGE11_MS1	0			/* [ダミー１]を指す	*/
#define	PAGE11_MS2	1			/* [ダミー２]を指す	*/
#define	PAGE11_MS3	2			/* [ダミー３]を指す	*/

/*===================================================================変数宣言*/
LOCAL	TC	dummy1[] = {			/* ダミー１		*/
			0x2540, 0x255f, 0x213c, 0x2331, TNULL
		};
LOCAL	TC	dummy2[] = {			/* ダミー２		*/
			0x2540, 0x255f, 0x213c, 0x2332, TNULL
		};
LOCAL	TC	dummy3[] = {			/* ダミー３		*/
			0x2540, 0x255f, 0x213c, 0x2333, TNULL
		};

/*=========================================================雑多のイベント処理*/
EXPORT	W	page11_init(W flg)
{
	W	rv;			/* 関数返り値			*/

	rv = 0;
	switch (flg) {
		case K_SETUP:		/* その他の起動時設定処理	*/
			break;
		case K_CHECK:		/* ページチェック処理		*/
			break;
		case K_INIT:		/* ページ初期化処理		*/
			break;
		case K_DISP:		/* ページ表示処理		*/
			break;
		case K_FINISH:		/* ページ終了処理		*/
			break;
	}

	return rv;
}

/*=================================================================再表示処理*/
EXPORT	W	page11_disp(W item, VP par)
{

	return 0;

}

/*=========================================================パーツ類の実行動作*/
EXPORT	W	page11_act(W item, W sts)
{
	switch  (item) {
		case K_PARTS(PAGE11_MS1):	/* [ダミー１]が押された	*/
			pdsp_msg(dummy1);
			break;
		case K_PARTS(PAGE11_MS2):	/* [ダミー２]が押された	*/
			pdsp_msg(dummy2);
			break;
		case K_PARTS(PAGE11_MS3):	/* [ダミー３]が押された	*/
			pdsp_msg(dummy3);
			break;
		case K_MENU(1):			/* [操作]-[ダミー]メニュー*/
			sig_buz(0);
			break;
		case K_MENU(2):			/* [操作]-[テスト]メニュー*/
			panel(PNL_TEST);
			break;
	}

	return 0;
}
