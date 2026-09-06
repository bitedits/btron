//
//	subtsk.h (壁紙変更/時間待ち用 subtask 処理部ヘッダ)
//
//	(C) Copyright 2001 by Personal Media Corporation.
//

#ifndef	_WALLCHG_SUBTSK_H
#define	_WALLCHG_SUBTSK_H
 
#include	<basic.h>
#include	<btron/btron.h>
 

// --------------------------------------------------------------- class SUBTSK
class	SUBTSK {
public:
	SUBTSK(W time);			// constructor
	~SUBTSK();			// destructor

	// subtask ID の取得
	const	W	get_tid() {return tid;}

	// subtask ID の設定
	void	set_tid(W id) {tid = id;}

	// subtask を起き上がらせる
	void	wakeup_subtask() {wup_tsk(tid);}

private:
	W	tid;			// subtask ID
};
 
#endif	// _WALLCHG_SUBTSK_H
