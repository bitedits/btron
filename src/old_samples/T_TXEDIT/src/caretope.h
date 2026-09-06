//
//	caretope.h (簡易文字列編集/CARET 管理系ヘッダ)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#ifndef	_T_TXEDIT_CARETOPE_H
#define	_T_TXEDIT_CARETOPE_H

#include	<basic.h>
#include	<btron/btron.h>


// 参照 class の宣言
class	TEXTIN;


// ------------------------------------------------------------- class CARETOPE
class	CARETOPE {
public:
	CARETOPE(const TEXTPORT* port);	// constructor
	~CARETOPE();			// destructor

	// caret の消灯
	void	off() {idsp_car(tport->car, 0);}

	// caret の表示
	void	on() {idsp_car(tport->car, 1);}

	// caret の点滅
	void	blink() {idsp_car(tport->car, -1);}

	// caret の移動
	void	move(PNT p, bool disp);

private:
	const	TEXTPORT*	tport;
};

#endif	// _T_TXEDIT_CARETOPE_H
