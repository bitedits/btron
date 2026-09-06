//
//	tadload.h (偽仮身一覧/実身からの読み込み系ヘッダ)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#ifndef	_I_DLED_TADLOAD_H
#define	_I_DLED_TADLOAD_H

#include	<basic.h>
#include	<btron/btron.h>
#include	<btron/vobj.h>

#include	<vector>


// -------------------------------------------------------------- class TADLOAD
class	TADLOAD {
	static	const	W	BUFFER_SIZE = 4096;
	typedef	struct {		// 実身の読み込み情報
		W	fd;
		W	ofs;		// 読み込み時の offset(rea_rec()用)
		W	idx;		// record 全体での参照位置
		W	rsize;		// record size
		B	buf[BUFFER_SIZE];
	} RINF;

	typedef	struct {		// 1 record の内容
		bool	vlnk;		// true : VLINK    false : FUSENSEG
		std::vector<B>	buf;	// 内容
	} RECDAT;

public:
	TADLOAD(const LINK* lnk);	// constructor
	~TADLOAD();			// destructor

	// 読み込みの主幹処理部
	void	main();

private:
	W	nst;			// 開始セグメントの段数
	W	vreg;			// 登録総数
	UH	ltype;			// 固定化/背景化状態
	bool	skiptad;		// 読み飛ばした TAD data あり
	bool	skipobj;		// 読み飛ばした LINK あり
	RINF	rinf;			// 実身の読み込み情報
	std::vector<RECDAT>	rdat;	// VLINK / FUSENSEG の内容

	// LINK と機能付箋を追いかける
	void	read_lnkfsn();

	// 仮身/付箋の登録をする
	void	reg_vobj(const VLINK* lnk, const VP dat, W len);

	// record からの読み込み
	TC	read_buf(bool force = false);

	// TAD 主 record の内容の解析
	void	tad_parse();

	// 付箋/セグメントの種別毎の分岐
	void	chk_TS(TC ch);

	// TS_VOBJ の取得
	void	get_TS_VOBJ(W len);

	// TS_FAPPPL の取得(固定化/背景化の設定)(DLED 互換)
	void	get_TS_FAPPL(W len);
};

#endif	// _I_DLED_TADLOAD_H
