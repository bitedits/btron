//
//	evtope.h (簡易文字列編集/イベント操舵部主幹ヘッダ)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#ifndef	_T_TXEDIT_EVTOPE_H
#define	_T_TXEDIT_EVTOPE_H

#include	<basic.h>
#include	<btron/btron.h>
#include	<btron/libapp.h>

#include	"cval.h"


// --------------------------------------------------------------- class EVTOPE
class	EVTOPE {
public:
	EVTOPE();			// constructor
	~EVTOPE();			// destructor

//	static	bool	pflg;		// 一時トレー経由の操作を受け入れたか?
	static	WFUNCREC	wfunc;
	static	WINFOREC	winfo[CVAL::WIN_NUM + 1];

	// イベントループの入り口
	static	ERR	eventloop();

	// 各イベントループ関数
//	static	W	evt_bckgrd();
	static	void	evt_idle();
//	static	W	evt_message(MESSAGE* msg);
	static	W	evt_menu();
	static	void	evt_disp(W idx, W mode, RECT* newr);
	static	void	evt_chgsts(W ix, W sts);
	static	W	evt_key();
	static	W	evt_press(W ix);
	static	W	evt_finish(W ix, W mode);
//	static	W	evt_paste(W ix, PNT pos);
//	static	W	evt_resp();
	static	void	evt_scroll(W ix, W type, W diff);
//	static	void	evt_dev(TC* name);
//	static	void	evt_vobj(W ix);

private:
};

#endif	// _T_TXEDIT_EVTOPE_H
