//
//	except.h (パーツ操作例/例外定義)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#ifndef	_PARSAMP_EXCEPT_H
#define	_PARSAMP_EXCEPT_H

#include	<basic.h>
#include	<errcode.h>

#include	<exception>


// ------------------------------------------------------------- exception 定義
//
// PARSAMP_EXCEPT(exception 基幹)
//
class	PARSAMP_EXCEPT : public exception {
public:
	PARSAMP_EXCEPT(ERR er = ER_OK) : exception(), err(er) {}

	ERR	get_err() {return err;}
	virtual	const	char*	what() {return "PARSAMP_EXCEPTION.";}

private:
	ERR	err;
};


//
// EXCEPT_EXECMSG(起動方法が違う)
//
class	EXCEPT_EXECMSG : public PARSAMP_EXCEPT {
public:
	EXCEPT_EXECMSG(ERR er = ER_OK) : PARSAMP_EXCEPT(er) {}

	const	char*	what() {return "bad execute message.";}
};


//
// EXCEPT_INITERR(起動時の初期化行程での例外)
//
class	EXCEPT_INITERR : public PARSAMP_EXCEPT {
public:
	EXCEPT_INITERR(ERR er = ER_OK) : PARSAMP_EXCEPT(er) {}

	const	char*	what() {return "initialize error.";}
};


//
// EXCEPT_FUSEN(付箋固有データ処理中での例外)
//
class	EXCEPT_FUSEN : public PARSAMP_EXCEPT {
public:
	EXCEPT_FUSEN(ERR er = ER_OK) : PARSAMP_EXCEPT(er) {}

	const	char*	what() {return "fusen operate error.";}
};


//
// EXCEPT_MAINWIN(主ウィンドウ操舵系での例外)
//
class	EXCEPT_MAINWIN : public PARSAMP_EXCEPT {
public:
	EXCEPT_MAINWIN(ERR er = ER_OK) : PARSAMP_EXCEPT(er) {}
 
	const	char*	what() {return "main window operate error.";}
};


//
// EXCEPT_MAINMENU(メインメニュー管理系での例外)
//
class   EXCEPT_MAINMENU : public PARSAMP_EXCEPT {
public:
        EXCEPT_MAINMENU(ERR er = ER_OK) : PARSAMP_EXCEPT(er) {}

        const   char*   what() {return "main menu operate error.";}
};

#endif	// _PARSAMP_EXCEPT_H
