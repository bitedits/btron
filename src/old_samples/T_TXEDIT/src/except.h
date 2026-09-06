//
//	except.h (簡易文字列編集/例外定義)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#ifndef	_T_TXEDIT_EXCEPT_H
#define	_T_TXEDIT_EXCEPT_H

#include	<basic.h>
#include	<errcode.h>

#include	<exception>


// ------------------------------------------------------------- exception 定義
//
// T_TXEDIT_EXCEPT(exception 基幹)
//
class	T_TXEDIT_EXCEPT : public exception {
public:
	T_TXEDIT_EXCEPT(ERR er = ER_OK) : exception(), err(er) {}

	ERR	get_err() {return err;}
	virtual	const	char*	what() {return "T_TXEDIT_EXCEPTION.";}

private:
	ERR	err;
};


//
// EXCEPT_EXECMSG(起動方法が違う)
//
class	EXCEPT_EXECMSG : public T_TXEDIT_EXCEPT {
public:
	EXCEPT_EXECMSG(ERR er = ER_OK) : T_TXEDIT_EXCEPT(er) {}

	const	char*	what() {return "bad execute message.";}
};


//
// EXCEPT_INITERR(起動時の初期化行程での例外)
//
class	EXCEPT_INITERR : public T_TXEDIT_EXCEPT {
public:
	EXCEPT_INITERR(ERR er = ER_OK) : T_TXEDIT_EXCEPT(er) {}

	const	char*	what() {return "initialize error.";}
};


//
// EXCEPT_FUSEN(付箋固有データ処理中での例外)
//
class	EXCEPT_FUSEN : public T_TXEDIT_EXCEPT {
public:
	EXCEPT_FUSEN(ERR er = ER_OK) : T_TXEDIT_EXCEPT(er) {}

	const	char*	what() {return "fusen operate error.";}
};


//
// EXCEPT_TADLOAD(実身からの読み込み中の例外)
//
class	EXCEPT_TADLOAD : public T_TXEDIT_EXCEPT {
public:
	EXCEPT_TADLOAD(ERR er = ER_OK) : T_TXEDIT_EXCEPT(er) {}

	const	char*	what() {return "loading error.";}
};


//
// EXCEPT_SAVEOPE(保存処理主幹部での例外)
//
class	EXCEPT_SAVEOPE : public T_TXEDIT_EXCEPT {
public:
	EXCEPT_SAVEOPE(ERR er = ER_OK) : T_TXEDIT_EXCEPT(er) {}

	const	char*	what() {return "saving operate error.";}
};


//
// EXCEPT_TADSAVE(実身への書き込み中の例外)
//
class	EXCEPT_TADSAVE : public T_TXEDIT_EXCEPT {
public:
	EXCEPT_TADSAVE(ERR er = ER_OK) : T_TXEDIT_EXCEPT(er) {}

	const	char*	what() {return "saving error.";}
};


//
// EXCEPT_MAINWIN(主ウィンドウ操舵系での例外)
//
class	EXCEPT_MAINWIN : public T_TXEDIT_EXCEPT {
public:
	EXCEPT_MAINWIN(ERR er = ER_OK) : T_TXEDIT_EXCEPT(er) {}
 
	const	char*	what() {return "main window operate error.";}
};


//
// EXCEPT_MAINMENU(メインメニュー管理系での例外)
//
class   EXCEPT_MAINMENU : public T_TXEDIT_EXCEPT {
public:
        EXCEPT_MAINMENU(ERR er = ER_OK) : T_TXEDIT_EXCEPT(er) {}

        const   char*   what() {return "main menu operate error.";}
};


//
// EXCEPT_TEXTIN(text_in 系処理での例外)
//
class	EXCEPT_TEXTIN : public T_TXEDIT_EXCEPT {
public:
	EXCEPT_TEXTIN(ERR er = ER_OK) : T_TXEDIT_EXCEPT(er) {}

        const   char*   what() {return "text_in operate error.";}
};

#endif	// _T_TXEDIT_EXCEPT_H
