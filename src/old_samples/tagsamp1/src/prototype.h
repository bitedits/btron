/*=============================================================================

	prototype.h : 見出しパネルサンプル プロトタイプ宣言

	(C) Copyright 1999 by Personal Media Corporation

=============================================================================*/

/*=============================================================tsexmain.cより*/
IMPORT	W	ex_main(M_EXECREQ *msg);

/*================================================================tsact.cより*/
IMPORT	W	evt_menu(VOID);
IMPORT	VOID	evt_idle(VOID);
IMPORT	W	evt_msg(MESSAGE *msg);
IMPORT	VOID	evt_chgstate(W ix, W sts);
IMPORT	W	evt_fin(W ix, W mode);
IMPORT	W	evt_key(VOID);
IMPORT	W	evt_press(W ix);
	
/*===============================================================tdsisp.cより*/
IMPORT	ERR	init_subpnl(RECT *subrp);
IMPORT	VOID	switch_subpnl(W pnum);
IMPORT	VOID	refresh(VOID);
IMPORT	VOID	evt_disp(W ix, W mode, RECT *new);

