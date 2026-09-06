//
//	appl.h (パーツ操作例/application 基幹ヘッダ)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#ifndef	_PARSAMP_APPL_H
#define	_PARSAMP_APPL_H

#include	<basic.h>
#include	<btron/btron.h>
#include	<errcode.h>

#include	<new>
#include	<memory>


// 参照 class の宣言
class	FUSEN;
class	GUIOPE;
class	PARSAMP_EXREQ;
class	PARSAMP_FSNREQ;


// --------------------------------------------------------- class PARSAMP_APPL
// application 基幹部の基底 class
class	PARSAMP_APPL {
	typedef	struct {		// 自 application 情報
		W	pid;		// 自 PID
		W	vid;		// 起動元仮身 ID
		W	pwid;		// 起動元仮身のある WID
		RECT	pr;		// 起動元仮身の矩形領域
		LINK	lnk;		// 対象実身への LINK
		LINK	alnk;		// 自 application 実身
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
	PARSAMP_APPL();			// default constructor
	PARSAMP_APPL(const MESSAGE* msg);	// copy constructor
	virtual	~PARSAMP_APPL();		// destructor

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

	friend	PARSAMP_EXREQ;
	friend	PARSAMP_FSNREQ;
};


// -------------------------------------------------------- class PARSAMP_EXREQ
// 仮身のオープン起動の基幹部
class	PARSAMP_EXREQ : public PARSAMP_APPL {
public:
	PARSAMP_EXREQ(const M_EXECREQ* msg);	// constructor
	~PARSAMP_EXREQ();		// destructor

	// application の入り口
	ERR	main();

private:
	bool	endprc;			// oend_prc() の発行の必要性
};


// ------------------------------------------------------- class PARSAMP_FSNREQ
// 付箋のオープン起動の基幹部
class	PARSAMP_FSNREQ : public PARSAMP_APPL {
public:
	PARSAMP_FSNREQ(const M_FUSENREQ* msg);	// constructor
	~PARSAMP_FSNREQ();		// destructor

	// application の入り口
	ERR	main();

private:
	bool	endprc;			// oend_prc() の発行の必要性
};

#endif	// _PARSAMP_APPL_H
