//
//	except.h (画像閲覧/例外定義)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#ifndef	_IMGVIEW_EXCEPT_H
#define	_IMGVIEW_EXCEPT_H

#include	<basic.h>
#include	<errcode.h>

#include	<exception>


// ------------------------------------------------------------- exception 定義
//
// IMGVIEW_EXCEPT(exception 基幹)
//
class	IMGVIEW_EXCEPT : public exception {
public:
	IMGVIEW_EXCEPT(ERR er = ER_OK) : exception(), err(er) {}

	ERR	get_err() {return err;}
	virtual	const	char*	what() {return "IMGVIEW_EXCEPTION.";}

private:
	ERR	err;
};


//
// EXCEPT_EXECMSG(起動方法が違う)
//
class	EXCEPT_EXECMSG : public IMGVIEW_EXCEPT {
public:
	EXCEPT_EXECMSG(ERR er = ER_OK) : IMGVIEW_EXCEPT(er) {}

	const	char*	what() {return "bad execute message.";}
};


//
// EXCEPT_INITERR(起動時の初期化行程での例外)
//
class	EXCEPT_INITERR : public IMGVIEW_EXCEPT {
public:
	EXCEPT_INITERR(ERR er = ER_OK) : IMGVIEW_EXCEPT(er) {}

	const	char*	what() {return "initialize error.";}
};


//
// EXCEPT_FUSEN(付箋固有データ処理中での例外)
//
class	EXCEPT_FUSEN : public IMGVIEW_EXCEPT {
public:
	EXCEPT_FUSEN(ERR er = ER_OK) : IMGVIEW_EXCEPT(er) {}

	const	char*	what() {return "fusen operate error.";}
};


//
// EXCEPT_MAINWIN(主ウィンドウ操舵系での例外)
//
class	EXCEPT_MAINWIN : public IMGVIEW_EXCEPT {
public:
	EXCEPT_MAINWIN(ERR er = ER_OK) : IMGVIEW_EXCEPT(er) {}
 
	const	char*	what() {return "main window operate error.";}
};


//
// EXCEPT_MAINMENU(メインメニュー管理系での例外)
//
class   EXCEPT_MAINMENU : public IMGVIEW_EXCEPT {
public:
        EXCEPT_MAINMENU(ERR er = ER_OK) : IMGVIEW_EXCEPT(er) {}

        const   char*   what() {return "main menu operate error.";}
};


//
// EXCEPT_IMGOPE(表示画像管理系での例外)
//
class   EXCEPT_IMGOPE : public IMGVIEW_EXCEPT {
public:
        EXCEPT_IMGOPE(ERR er = ER_OK) : IMGVIEW_EXCEPT(er) {}

        const   char*   what() {return "display image operator error.";}
};


//
// EXCEPT_OVDISP(開いた仮身の表示上の例外)
//
class   EXCEPT_OVDISP : public IMGVIEW_EXCEPT {
public:
        EXCEPT_OVDISP(ERR er = ER_OK) : IMGVIEW_EXCEPT(er) {}

        const   char*   what() {return "opened virtual-object display error.";}
};


//
// EXCEPT_SAVEDAT(保存処理系での例外)
//
class   EXCEPT_SAVEDAT : public IMGVIEW_EXCEPT {
public:
        EXCEPT_SAVEDAT(ERR er = ER_OK) : IMGVIEW_EXCEPT(er) {}

        const   char*   what() {return "file save error.";}
};

#endif	// _IMGVIEW_EXCEPT_H
