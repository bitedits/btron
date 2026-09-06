//
//	appl.h (壁紙変更/application 基幹ヘッダ)
//
//	(C) Copyright 2001 by Personal Media Corporation.
//

#ifndef	_WALLCHG_APPL_H
#define	_WALLCHG_APPL_H

#include	<basic.h>
#include	<btron/btron.h>
#include	<btron/vobj.h>

#include	<new>
#include	<memory>


// 参照 class の宣言
class	FUSEN;
class	MAINTSK;
class	SUBTSK;


// --------------------------------------------------------- class WALLCHG_APPL
class	WALLCHG_APPL {
	typedef	struct {		// 自 application 情報
		W	pid;		// process ID
		LINK	lnk;		// application 実身への LINK
		union {			// 起動時 message
			MESSAGE*	msg;	// 不定
			M_EXECREQ*	exreq;	// 仮身のオープン起動
			M_FUSENREQ*	fsnreq;	// 付箋のオープン起動
		} msg;
	} MYDAT;

public:
	WALLCHG_APPL(MESSAGE* msg);	// constructor
	~WALLCHG_APPL();		// destructor

	std::auto_ptr<FUSEN>	fsn;	// 付箋管理系
	std::auto_ptr<MAINTSK>	mtsk;	// 主幹 task
	std::auto_ptr<SUBTSK>	stsk;	// 時間待ち subtask

	// 自 PID の取得
	const	W	get_pid() {return mydat.pid;}

	// application の入り口
	ERR	main();

	// 時間待ち subtask の生成
	void	create_subtsk();

	// 時間待ち subtask の立ち上げ
	void	start_subtsk();

private:
	bool	endprc;			// oend_prc() の発行が必要か?
	MYDAT	mydat;			// 自 application 情報

	// 前のの確認と設定要求
	bool	chk_front();
};

#endif	// _WALLCHG_APPL_H
