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
IMPORT	VOID	cre_parts(W pnum);
IMPORT	VOID	disp_par(W pn, W sts);
IMPORT	VOID	refresh(VOID);
IMPORT	VOID	evt_disp(W ix, W mode, RECT *new);

/*=============================================================tspage11.cより*/
IMPORT	W	page11_init(W flg);
IMPORT	W	page11_disp(W item, VP par);
IMPORT	W	page11_act(W item, W sts);

/*=============================================================tspage12.cより*/
IMPORT	W	page12_init(W flg);
IMPORT	W	page12_disp(W item, VP par);
IMPORT	W	page12_act(W item, W sts);

/*=============================================================tspage21.cより*/
IMPORT	W	page21_init(W flg);
IMPORT	W	page21_disp(W item, VP par);
IMPORT	W	page21_act(W item, W sts);

/*=============================================================tspage31.cより*/
IMPORT	W	page31_init(W flg);
IMPORT	W	page31_disp(W item, VP par);
IMPORT	W	page31_act(W item, W sts);
