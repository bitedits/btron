/*=============================================================================

	tspage12.c : 見出しパネルのサンプル ページ１の子２（ページ１２）の処理

	(C) Copyright 1999 Personal Media Corporation

=============================================================================*/
#include	"tagsamp.h"
#include	"prototype.h"

/*=========================================================雑多のイベント処理*/
EXPORT	W	page12_init(W flg)
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
EXPORT	W	page12_disp(W item, VP par)
{

	return 0;

}

/*=========================================================パーツ類の実行動作*/
EXPORT	W	page12_act(W item, W sts)
{

	return 0;

}
