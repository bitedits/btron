//
//	savedat.h (画像閲覧/保存処理系ヘッダ)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#ifndef	_IMGVIEW_SAVEDAT_H
#define	_IMGVIEW_SAVEDAT_H

#include	<basic.h>
#include	<btron/btron.h>
#include	<btron/dp.h>		// tad.h で必要
#include	<tad.h>


// -------------------------------------------------------------- class SAVEDAT
class	SAVEDAT {
	static	const	W	BUFFER_SIZE = 4096;

	typedef	struct {		// 出力 buffer 定義
		B	dat[BUFFER_SIZE];	// buffer (実 data)
		W	ofs;		// offset(wri_rec() 用)
		W	size;		// buffer に占める data 量(byte 単位)
		bool	first;		// 初回書き出しかどうか
	} WRITE_BUFFER;

public:
	SAVEDAT(const LINK* dlnk);	// constructor
	~SAVEDAT();			// destructor

	// 主幹処理
	void	main(const BMP* bmp, const CSPEC *cspec);

private:
	W	fd;
	RECT	r;			// 画像矩形枠
	UNITS	h_unit;			// 印刷水平解像度
	UNITS	v_unit;			// 印刷垂直解像度
	const	LINK*	lnk;
	WRITE_BUFFER	wbuf;

	// 現在の record への書き出し
	void	output_data(const B* dat, W size);

	// ヘッダ部分の書き出し
	void	write_header(const BMP* bmp);

	// 画像セグメントの書き出し
	void	write_image(const BMP* bmp, const CSPEC *cspec);

	// フッタ部分の書き出し
	void	write_footer();
};

#endif	// _IMGVIEW_SAVEDAT_H
