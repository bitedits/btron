//
//	tadsave.h (簡易文字列編集/実身への書き込み系ヘッダ)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#ifndef	_T_TXEDIT_TADSAVE_H
#define	_T_TXEDIT_TADSAVE_H

#include	<basic.h>
#include	<btron/btron.h>


// -------------------------------------------------------------- class TADSAVE
class	TADSAVE {
	static	const	W	BUFFER_SIZE = 4096;

	typedef	struct {		// 出力 buffer 定義
		B	dat[BUFFER_SIZE];	// buffer(実 data)
		W	ofs;		// offset(wri_rec() 用)
		W	size;		// buffer に占める data 量(byte 単位)
	} WRITE_BUFFER;

public:
	TADSAVE(W pfd, W pvid);		// constructor
	~TADSAVE();			// destructor

	// 実行処理部
	void	main();

private:
	W	fd;			// 保存先の実身(F_UPDATE で開いている)
	W	dvid;			// 保存先実身の仮身の仮身 ID
	WRITE_BUFFER	wbuf;		// 出力 buffer

	// record への書き出し
	void	output_data(const B* dat, W size);

	// 管理情報セグメントの書き出し
	void	store_info();

	// 文章開始セグメントの書き出し
	void	store_txt();

	// 文章終了セグメントの書き出し
	void	store_txtend();

	// TAD 主 record の書き出し
	void	store_tad();

	// 古い TAD 主 record を破棄
	void	del_oldtad();
};

#endif	// _T_TXEDIT_TADSAVE_H
