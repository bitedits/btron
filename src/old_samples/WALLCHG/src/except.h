//
//	except.h (壁紙変更/例外定義)
//
//	(C) Copyright 2001 by Personal Media Corporation.
//

#ifndef	_WALLCHG_EXCEPT_H
#define	_WALLCHG_EXCEPT_H

#include	<basic.h>
#include	<errcode.h>

#include	<exception>


// ------------------------------------------------------------- exception 定義
//
// WALLCHG_EXCEPT(exception 基幹)
//
class	WALLCHG_EXCEPT : public exception {
public:
	WALLCHG_EXCEPT(ERR er = ER_OK) : exception(), err(er) {}

	ERR	get_err() {return err;}
	virtual	const	char*	what() {return "WALLCHG_EXCEPTION.";}

private:
	ERR	err;
};


//
// EXCEPT_EXECMSG(起動方法が違う)
//
class	EXCEPT_EXECMSG : public WALLCHG_EXCEPT {
public:
	EXCEPT_EXECMSG(ERR er = ER_OK) : WALLCHG_EXCEPT(er) {}

	const	char*	what() {return "bad execute message.";}
};


//
// EXCEPT_INITERR(起動時の初期化行程での例外)
//
class	EXCEPT_INITERR : public WALLCHG_EXCEPT {
public:
	EXCEPT_INITERR(ERR er = ER_OK) : WALLCHG_EXCEPT(er) {}

	const	char*	what() {return "initialize error.";}
};


//
// EXCEPT_FUSEN(付箋固有データ処理中での例外)
//
class	EXCEPT_FUSEN : public WALLCHG_EXCEPT {
public:
	EXCEPT_FUSEN(ERR er = ER_OK) : WALLCHG_EXCEPT(er) {}

	const	char*	what() {return "fusen operate error.";}
};
 
 
//
// EXCEPT_MAINTSK(主幹処理部での例外)
//
class	EXCEPT_MAINTSK : public WALLCHG_EXCEPT {
public:
	EXCEPT_MAINTSK(ERR er = ER_OK) : WALLCHG_EXCEPT(er) {}
 
	const	char*	what() {return "main task error.";}
};
 
 
//
// EXCEPT_SUBTSK(時間待ち subtask での例外)
//
class	EXCEPT_SUBTSK : public WALLCHG_EXCEPT {
public:
	EXCEPT_SUBTSK(ERR er = ER_OK) : WALLCHG_EXCEPT(er) {}
 
	const	char*	what() {return "sub task error.";}
};
 
 
//
// EXCEPT_WALLCHG(壁紙変更処理での例外)
//
class	EXCEPT_WALLCHG : public WALLCHG_EXCEPT {
public:
	EXCEPT_WALLCHG(ERR er = ER_OK) : WALLCHG_EXCEPT(er) {}
 
	const	char*	what() {return "wallpaper chagne error.";}
};
 
 
//
// EXCEPT_SETUP(設定パネルでの例外)
//
class	EXCEPT_SETUP : public WALLCHG_EXCEPT {
public:
	EXCEPT_SETUP(ERR er = ER_OK) : WALLCHG_EXCEPT(er) {}
 
	const	char*	what() {return "setup panel operate error.";}
};

#endif	// _WALLCHG_EXCEPT_H
