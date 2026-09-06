//
//	appl.h (偽仮身一覧/application 基幹ヘッダ)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#ifndef	_I_DLED_APPL_H
#define	_I_DLED_APPL_H

#include	<basic.h>
#include	<btron/btron.h>
#include	<btron/dp.h>
#include	<btron/vobj.h>
#include	<errcode.h>

#include	<new>
#include	<memory>

#include	"vobjs.h"


// 参照 class の宣言
class	EDITOBJ;
class	FUSEN;
class	GUIOPE;
class	I_DLED_EXREQ;
class	I_DLED_PSTREQ;
class	I_DLED_DSPREQ;


// ---------------------------------------------------------- class I_DLED_APPL
// application 基幹部の基底 class
class	I_DLED_APPL {
	typedef	struct {		// 自 application 情報
		W	pid;		// 自 PID
		W	vid;		// 起動元仮身 ID
		W	pwid;		// 起動元仮身のある WID
		RECT	pr;		// 起動元仮身の矩形領域
		LINK	lnk;		// 対象実身への LINK
		LINK	alnk;		// 自 application 実身
		COLOR	bgcol;		// 仮身背景色
		union {			// 起動時 message
			const	MESSAGE*	msg;	// 不定
			const	M_EXECREQ*	exmsg;	// 仮身のオープン起動
			const	M_PASTEREQ*	pstmsg;	// データ貼り込み起動
			const	M_FUSENREQ*	fsnmsg;	// 付箋のオープン起動
			const	M_DISPREQ*	dspmsg;	// 開いた仮身の表示起動
			const	M_TADREQ*	tadmsg;	// TAD データ生成起動
		} msg;
	} MYDAT;

public:
	I_DLED_APPL();			// default constructor
	I_DLED_APPL(const MESSAGE* msg);	// copy constructor
	virtual	~I_DLED_APPL();		// destructor

	VOBJS	vobjs;			// 仮身管理系
	std::auto_ptr<EDITOBJ>	eobj;	// 編集対象実身管理系
	std::auto_ptr<FUSEN>	fsn;	// 付箋管理系
	std::auto_ptr<GUIOPE>	gui;	// GUI 操舵系基幹

	// 自 PID の取得
	const	W	get_pid() {return mydat.pid;}

	// 起動元仮身 ID の取得
	const	W	get_vid() {return mydat.vid;}

	// 起動元仮身のある window ID の取得
	const	W	get_pwid() {return mydat.pwid;}

	// 起動元仮身の矩形枠の取得
	const	RECT*	get_pr() {return &mydat.pr;}

	// 対象実身への LINK の取得
	const	LINK*	get_link() {return &mydat.lnk;}

	// 仮身背景色の取得
	const	COLOR	get_bgcol() {return mydat.bgcol;}

	// 起動時 message の取得
	const	MESSAGE*	get_msg() {return mydat.msg.msg;}
	const	M_EXECREQ*	get_exmsg() {return mydat.msg.exmsg;}
	const	M_PASTEREQ*	get_pstmsg() {return mydat.msg.pstmsg;}
	const	M_FUSENREQ*	get_fsnmsg() {return mydat.msg.fsnmsg;}
	const	M_DISPREQ*	get_dspmsg() {return mydat.msg.dspmsg;}
	const	M_TADREQ*	get_tadmsg() {return mydat.msg.tadmsg;}

	// application の入り口
	virtual	ERR	main() {return ER_OK;}

private:
	MYDAT	mydat;			// 自 application 情報

	friend	I_DLED_EXREQ;
	friend	I_DLED_PSTREQ;
	friend	I_DLED_DSPREQ;
};


// --------------------------------------------------------- class I_DLED_EXREQ
// 仮身のオープン起動の基幹部
class	I_DLED_EXREQ : public I_DLED_APPL {
public:
	I_DLED_EXREQ(const M_EXECREQ* msg);	// constructor
	~I_DLED_EXREQ();		// destructor

	// application の入り口
	ERR	main();

private:
	bool	endprc;			// oend_prc() の発行の必要性
};


// -------------------------------------------------------- class I_DLED_DSPREQ
// 開いた仮身の表示起動の基幹部
class	I_DLED_DSPREQ : public I_DLED_APPL {
public:
	I_DLED_DSPREQ(const M_DISPREQ* msg);	// constructor
	~I_DLED_DSPREQ();		// destructor

	// application の入り口
	ERR	main();

private:
	W	rspflg;			// oend_req() の返り値
};


// -------------------------------------------------------- class I_DLED_PSTREQ
// データ貼り込み起動の基幹部
class	I_DLED_PSTREQ : public I_DLED_APPL {
public:
	I_DLED_PSTREQ(const M_PASTEREQ* msg);	// constructor
	~I_DLED_PSTREQ();		// destructor

	// application の入り口
	ERR	main();

private:
	W	rspflg;			// oend_req() の返り値
	W	igid;			// 内部描画環境 ID
	W	iwid;			// 内部ウィンドウ ID
};

#endif	// _I_DLED_APPL_H
