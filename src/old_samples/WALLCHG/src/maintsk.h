//
//	maintsk.h (壁紙変更/主幹処理部ヘッダ)
//
//	(C) Copyright 2001 by Personal Media Corporation.
//

#ifndef	_WALLCHG_MAINTSK_H
#define	_WALLCHG_MAINTSK_H

#include	<basic.h>
#include	<btron/btron.h>


// -------------------------------------------------------------- class MAINTSK
class	MAINTSK {
public:
	MAINTSK();			// constructor
	~MAINTSK();			// destructor

	// メッセージの受信待ち
	void	recv_msgloop() throw();

private:
	bool	loop;			// 待ちをつづけるかどうか

	// メッセージの受信
	bool	recv_mesg(MESSAGE* wmsg);
};

#endif	// _WALLCHG_MAINTSK_H
