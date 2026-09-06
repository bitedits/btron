//
//	tadload.h (簡易文字列編集/実身からの読み込み系ヘッダ)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#ifndef	_T_TXEDIT_TADLOAD_H
#define	_T_TXEDIT_TADLOAD_H

#include	<basic.h>
#include	<btron/btron.h>
#include	<btron/vobj.h>
#include	<tlang.h>

#include	<stack>


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

public:
	TADLOAD(const LINK* lnk);	// constructor
	~TADLOAD();			// destructor

	// 読み込みの主幹処理部
	void	main();

private:
	W	nst;			// 開始セグメントの段数
	bool	skiptad;		// 読み飛ばした TAD data あり
	bool	ln;			// 文末が改行系かどうか
	RINF	rinf;			// 実身の読み込み情報
	TLANG	lng;			// 現在の言語/スクリプト指定
	std::stack<TLANG>	nlng;	// 各段毎の言語/スクリプト指定

	// record からの読み込み(TC 1文字づつの読み込み)
	TC	read_buf(bool force);

	// TAD 主 record の内容の解析
	void	tad_parse();

	// 付箋/セグメントの種別毎の分岐
	void	chk_TS(TC ch);
};

#endif	// _T_TXEDIT_TADLOAD_H
