//
//	textin.h (簡易文字列編集/text_in 系処理ヘッダ)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#ifndef	_T_TXEDIT_TEXTIN_H
#define	_T_TXEDIT_TEXTIN_H

#include	<basic.h>
#include	<btron/btron.h>


// --------------------------------------------------------------- class TEXTIN
class	TEXTIN {
public:
	TEXTIN(W wid, W pmode);		// constructor
	~TEXTIN();			// destructor

	// TIP ID を取得する
	const	W	get_tid() {return tid;}

	// テキスト入力ポートを取得する
	TEXTPORT*	get_tport() {return tport;}

	// 再 open
	void	reopen(W pmode);

private:
	W	tid;			// TIP ID
	W	mode;			// テキスト入力ポートの動作モード
	TEXTPORT*	tport;		// テキスト入力ポート
};

#endif	// _T_TXEDIT_TEXTIN_H
