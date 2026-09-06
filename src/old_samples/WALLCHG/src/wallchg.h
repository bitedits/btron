//
//	wallchg.h (壁紙変更/壁紙変更処理主幹部ヘッダ)
//
//	(C) Copyright 2001 by Personal Media Copration.
//

#ifndef	_WALLCHG_WALLCHG_H
#define	_WALLCHG_WALLCHG_H

#include	<basic.h>
#include	<btron/btron.h>


// -------------------------------------------------------------- class WALLCHG
class	WALLCHG {
	// $$BGSCREEN.BOX への access 系
	static	const	W	RETRY_MAX = 5;
	static	const	W	RETRY_WAIT = 0;

	// $$BGSCREEN.BOX の管理情報系
	static	const	W	BG_MAX = 50;	// 壁紙の最大登録数
	static	const	W	BGINFO_RTYPE = 15;
	static	const	UW	BGINFO_RMASK = (1 << BGINFO_RTYPE);
	static	const	W	BGDATA_RTYPE = 31;
	static	const	UW	BGDATA_RMASK = (1 << BGDATA_RTYPE);

	typedef	struct {		// $$BGSCREEN.BOX 内の管理情報
		H	num;		// 選択番号
		H	max;		// 背景の個数
		TC	name[BG_MAX][L_FNM];	// 各壁紙の名称
	} BGINFO;

public:
	WALLCHG();			// constructor
	~WALLCHG();			// destructor

	// 乱数の初期化
	static	void	init_rand();

	// 壁紙変更の実行
	void	exec();

private:
	W	fd;			// $$BGSCREEN.BOX を開いている FD
	LINK	blnk;			// $$BGSCREEN.BOX への LINK
	BGINFO	bginfo;			// $$BGSCREEN.BOX の管理情報

	// 管理情報の読み込み
	void	read_info();

	// 管理情報への書き込み
	void	write_info();

	// 乱数の発生
	H	get_rand(H now, H min, H max);

	// 番号に応じた背景画像の読み込み・設定の発行
	void	set_bgpat();
};

#endif	// _WALLCHG_WALLCHG_H
