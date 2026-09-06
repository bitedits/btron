//
//	savedat.cc (画像閲覧/保存処理系)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#include	<basic.h>
#include	<btron/btron.h>
#include	<btron/dp.h>
#include	<btron/libapp.h>
#include	<bstring.h>
#include	<tad.h>

#include	"err.h"
#include	"except.h"
#include	"debug.h"

#include	"savedat.h"


// 内部構造体
typedef	struct {			// TS_INFO の保存内容
	TADSEG	hd;
	INFOSEG	dat;
} INFODAT;

typedef	struct {			// TS_FIG の保存内容
	TADSEG	hd;
	FIGSEG	dat;
} FIGDAT;


// ----------------------------------------------------- SAVEDAT 内 public 関数
//
// constructor(copy)
//
SAVEDAT::SAVEDAT(const LINK* dlnk)
	: fd(-1), h_unit(-120), v_unit(-120), lnk(dlnk)
{
	DPRINT(("SAVEDAT constructor\n"));
	fd = opn_fil((LINK*)dlnk, F_WRITE | F_WEXCL, NULL);
	if (fd < ER_OK) {
		throw EXCEPT_SAVEDAT(fd);
	}

	wbuf.ofs = 0;
	wbuf.size = 0;
	wbuf.first = true;
	memset(wbuf.dat, 0x00, BUFFER_SIZE);
}


//
// destructor
//
SAVEDAT::~SAVEDAT()
{
	DPRINT(("SAVEDAT destructor\n"));
	if (fd >= 0) {
		cls_fil(fd);
	}
}


//
// 主幹処理
//
// 下位関数の exception は上位で受け取ること
//
void	SAVEDAT::main(const BMP* bmp, const CSPEC *cspec)
{
	r = bmp->bounds;

	write_header(bmp);
	write_image(bmp, cspec);
	write_footer();

	return;
}


// ---------------------------------------------------- SAVEDAT 内 private 関数
//
// TAD 主 record に出力
//
void	SAVEDAT::output_data(const B* dat, W size)
{
	if (size < 0) {
		// flush buffer
		if (wbuf.size > 0) {
			if (wbuf.first) {
				// 初回書き出し(対象実身に前のがあるかどうか)
				if (fnd_rec(fd, F_TOPEND, RM_TADDATA, 0x0000, NULL) == RT_TADDATA) {
					// 既存の TAD 主 record に上書き
					wri_rec(fd, 0, wbuf.dat, wbuf.size, NULL, NULL, 0);
					trc_rec(fd, wbuf.size);
				} else {
					// TAD 主 record を追加
					apd_rec(fd, wbuf.dat, wbuf.size, RT_TADDATA, 0, 0);
					see_rec(fd, -1, -1, NULL);
				}
				wbuf.first = false;
			} else {
				// 二回目以降の書き出し
				wri_rec(fd, wbuf.ofs, wbuf.dat, wbuf.size, NULL, NULL, 0);
			}
			wbuf.ofs += wbuf.size;
			wbuf.size = 0;
			memset(wbuf.dat, 0x00, BUFFER_SIZE);
		}
	} else {
		// buffer に溜めていく
		W	widx;
 
		for (widx = 0; widx < size; ++widx) {
			if (wbuf.size >= BUFFER_SIZE) {
				// buffer をはみ出すので flush で出力する
				output_data(NULL, -1);
			}
			wbuf.dat[wbuf.size] = dat[widx];
			++wbuf.size;
		}
	}

	return;
}


//
// ヘッダ部分の書き込み
//
// 管理情報セグメント・図形開始セグメントの出力をします。
//
void	SAVEDAT::write_header(const BMP* bmp)
{
	// 管理情報セグメントの出力
	INFODAT	info;

	info.hd.id = TC_ESC | TS_INFO;
	info.hd.len = sizeof(INFOSEG);
	info.dat.subid = 0x0000;
	info.dat.sublen = sizeof(UH);
	info.dat.data[0] = 0x0121;	// Version 1.21 準拠を明記
	output_data((const B*)&info, sizeof(INFODAT));

	// 図形開始セグメントの出力
	FIGDAT	fig;

	fig.hd.id = TC_ESC | TS_FIG;
	fig.hd.len = sizeof(FIGSEG);
	fig.dat.view = r;
	fig.dat.draw = r;
	fig.dat.h_unit = h_unit;
	fig.dat.v_unit = v_unit;
	fig.dat.ratio = 0;		// 拡張余白の部分
	output_data((const B*)&fig, sizeof(FIGDAT));

	return;
}


//
// 画像セグメントの書き込み
//
void	SAVEDAT::write_image(const BMP* bmp, const CSPEC *cspec)
{
	// 画像の容量(byte 単位)の算出
	W	asize;			// total size;
	W	isize;			// plane 情報の size
	W	psize;			// palette 情報の size

	isize = bmp->rowbytes * rectheight(bmp->bounds);
	if (cspec->attr & DA_HAVECMAP) {
		// color map を持つ
		psize = sizeof(COLOR) * cspec->info[0];
	} else {
		// color map を持たない
		psize = 0;
	}
	asize = sizeof(IMAGESEG) + isize + psize;

	// TS_IMAGE の書き出し
	W	slen;
	LTADSEG	img;

	img.id = TC_ESC | TS_IMAGE;
	if (asize < 65536) {
		img.len = asize;
		slen = sizeof(TADSEG);
	} else {
		// large segment にしないといけない
		img.len = 0xffff;
		img.llen = asize;
		slen = sizeof(LTADSEG);
	}
	output_data((const B*)&img, slen);

	// TS_IMAGE の内容の書き出し(plane の方はまだ)
	IMAGESEG	idat;

	idat.view = r;
	idat.draw = r;
	idat.h_unit = h_unit;
	idat.v_unit = v_unit;
	idat.slope = 0;
	idat.color = cspec->attr & 0x000f;
	if (cspec->attr & DA_HAVECMAP) {
		// color map を持つ
		idat.cinfo[0] = psize;
		idat.cinfo[1] = 0;
		idat.cinfo[2] = (sizeof(IMAGESEG) + isize) >> 16;
		idat.cinfo[3] = (sizeof(IMAGESEG) + isize) & 0x0000ffff;
	} else {
		// color map を持たない
		W	l;

		for (l = 0; l < 4; ++l) {
			idat.cinfo[l] = cspec->info[l];
		}
	}
	idat.extlen = 0;
	idat.extend = 0;
	idat.mask = 0;
	idat.compac = 0;
	idat.planes = 1;
	idat.pixbits = bmp->pixbits;
	idat.rowbytes = bmp->rowbytes;
	idat.bounds = bmp->bounds;
	idat.base_off[0] = sizeof(IMAGESEG);

	output_data((const B*)&idat, sizeof(IMAGESEG));

	// buffer flush(必ずここで行っておくこと)
	output_data(NULL, -1);

	// plane 情報の出力
	wri_rec(fd, wbuf.ofs, (B*)bmp->baseaddr[0], isize, NULL, NULL, 0);
	wbuf.ofs += isize;

	// palette 情報の出力(無いこともある)
	if ((cspec->attr & DA_HAVECMAP) && (psize > 0)) {
		wri_rec(fd, wbuf.ofs, (B*)cspec->colmap, psize, NULL, NULL, 0);
		wbuf.ofs += psize;
	}

	return;
}


//
// フッタ部分の書き込み
//
// 図形終了セグメントの出力をします。
//
void	SAVEDAT::write_footer()
{
	TADSEG	fend;

	fend.id = TC_ESC | TS_FIGEND;
	fend.len = 0;
	output_data((const B*)&fend, sizeof(TADSEG));

	// buffer flush
	output_data(NULL, -1);

	return;
}
