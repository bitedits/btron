//
//	except.h (偽仮身一覧/例外定義)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#ifndef	_I_DLED_EXCEPT_H
#define	_I_DLED_EXCEPT_H

#include	<basic.h>
#include	<errcode.h>

#include	<exception>


// ------------------------------------------------------------- exception 定義
//
// I_DLED_EXCEPT(exception 基幹)
//
class	I_DLED_EXCEPT : public exception {
public:
	I_DLED_EXCEPT(ERR er = ER_OK) : exception(), err(er) {}

	ERR	get_err() {return err;}
	virtual	const	char*	what() {return "I_DLED_EXCEPTION.";}

private:
	ERR	err;
};


//
// EXCEPT_EXECMSG(起動方法が違う)
//
class	EXCEPT_EXECMSG : public I_DLED_EXCEPT {
public:
	EXCEPT_EXECMSG(ERR er = ER_OK) : I_DLED_EXCEPT(er) {}

	const	char*	what() {return "bad execute message.";}
};


//
// EXCEPT_INITERR(起動時の初期化行程での例外)
//
class	EXCEPT_INITERR : public I_DLED_EXCEPT {
public:
	EXCEPT_INITERR(ERR er = ER_OK) : I_DLED_EXCEPT(er) {}

	const	char*	what() {return "initialize error.";}
};


//
// EXCEPT_FUSEN(付箋固有データ処理中での例外)
//
class	EXCEPT_FUSEN : public I_DLED_EXCEPT {
public:
	EXCEPT_FUSEN(ERR er = ER_OK) : I_DLED_EXCEPT(er) {}

	const	char*	what() {return "fusen operate error.";}
};


//
// EXCEPT_TADLOAD(実身からの読み込み中の例外)
//
class	EXCEPT_TADLOAD : public I_DLED_EXCEPT {
public:
	EXCEPT_TADLOAD(ERR er = ER_OK) : I_DLED_EXCEPT(er) {}

	const	char*	what() {return "loading error.";}
};


//
// EXCEPT_SAVEOPE(保存処理主幹部での例外)
//
class	EXCEPT_SAVEOPE : public I_DLED_EXCEPT {
public:
	EXCEPT_SAVEOPE(ERR er = ER_OK) : I_DLED_EXCEPT(er) {}

	const	char*	what() {return "saving operate error.";}
};


//
// EXCEPT_TADSAVE(実身への書き込み中の例外)
//
class	EXCEPT_TADSAVE : public I_DLED_EXCEPT {
public:
	EXCEPT_TADSAVE(ERR er = ER_OK) : I_DLED_EXCEPT(er) {}

	const	char*	what() {return "saving error.";}
};


//
// EXCEPT_VOBJ(単一仮身単位での例外)
//
class	EXCEPT_VOBJ : public I_DLED_EXCEPT {
public:
	EXCEPT_VOBJ(ERR er = ER_OK) : I_DLED_EXCEPT(er) {}

	const	char*	what() {return "a virutal-object operation error.";}
};


//
// EXCEPT_VOBJS(仮身群管理系での例外)
//
class	EXCEPT_VOBJS : public I_DLED_EXCEPT {
public:
	EXCEPT_VOBJS(ERR er = ER_OK) : I_DLED_EXCEPT(er) {}

	const	char*	what() {return "virutal-objects operation error.";}
};


//
// EXCEPT_MAINWIN(主ウィンドウ操舵系での例外)
//
class	EXCEPT_MAINWIN : public I_DLED_EXCEPT {
public:
	EXCEPT_MAINWIN(ERR er = ER_OK) : I_DLED_EXCEPT(er) {}
 
	const	char*	what() {return "main window operate error.";}
};


//
// EXCEPT_MAINMENU(メインメニュー管理系での例外)
//
class   EXCEPT_MAINMENU : public I_DLED_EXCEPT {
public:
        EXCEPT_MAINMENU(ERR er = ER_OK) : I_DLED_EXCEPT(er) {}

        const   char*   what() {return "main menu operate error.";}
};


//
// EXCEPT_OVDISP(開いた仮身の表示上の例外)
//
class   EXCEPT_OVDISP : public I_DLED_EXCEPT {
public:
        EXCEPT_OVDISP(ERR er = ER_OK) : I_DLED_EXCEPT(er) {}

        const   char*   what() {return "opened virtual-object display error.";}
};


//
// EXCEPT_PDOPE(PD 操作系での例外)
//
class   EXCEPT_PDOPE : public I_DLED_EXCEPT {
public:
        EXCEPT_PDOPE(ERR er = ER_OK) : I_DLED_EXCEPT(er) {}

        const   char*   what() {return "PD operation error.";}
};


//
// EXCEPT_KBOPE(keyboard 操作系での例外)
//
class   EXCEPT_KBOPE : public I_DLED_EXCEPT {
public:
        EXCEPT_KBOPE(ERR er = ER_OK) : I_DLED_EXCEPT(er) {}

        const   char*   what() {return "keyboard operation error.";}
};


//
// EXCEPT_UNDO([取り消し] 情報管理系での例外)
//
class   EXCEPT_UNDO : public I_DLED_EXCEPT {
public:
        EXCEPT_UNDO(ERR er = ER_OK) : I_DLED_EXCEPT(er) {}

        const   char*   what() {return "undo/redo operate error.";}
};


//
// EXCEPT_SELFRM(選択枠管理系での例外)
//
class   EXCEPT_SELFRM : public I_DLED_EXCEPT {
public:
        EXCEPT_SELFRM(ERR er = ER_OK) : I_DLED_EXCEPT(er) {}

        const   char*   what() {return "selection frame operate error.";}
};


//
// EXCEPT_TIDYPNL(整頓設定パネルでの例外)
//
class   EXCEPT_TIDYPNL : public I_DLED_EXCEPT {
public:
        EXCEPT_TIDYPNL(ERR er = ER_OK) : I_DLED_EXCEPT(er) {}

        const   char*   what() {return "tidy setup panel error.";}
};

#endif	// _I_DLED_EXCEPT_H
