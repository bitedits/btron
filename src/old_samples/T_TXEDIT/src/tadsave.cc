//
//	tadsave.cc (簡易文字列編集/実身への書き込み系)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#include	<basic.h>
#include	<btron/btron.h>
#include	<btron/dp.h>		// tad.h で必要
#include	<btron/vobj.h>
#include	<btron/libapp.h>
#include	<bstring.h>
#include	<errcode.h>
#include	<tad.h>
#include	<tcode.h>
#include	<tlang.h>
#include	<wtstring.h>

#include	<vector>

#include	"dbox.h"
#include	"cval.h"
#include	"val.h"
#include	"struct.h"
#include	"err.h"
#include	"except.h"
#include	"debug.h"

#include	"appl.h"
#include	"strope.h"
#include	"tadsave.h"


// ----------------------------------------------------- TADSAVE 内 public 関数
//
// constructor
//
TADSAVE::TADSAVE(W pfd, W pvid)
	: fd(pfd), dvid(pvid)
{
	sysmsg(DBOX::SMSG_SAVE);
	wbuf.ofs = 0;
	wbuf.size = 0;
	memset(wbuf.dat, 0x00, BUFFER_SIZE);
}


//
// destructor
//
TADSAVE::~TADSAVE()
{
	sysmsg(0);
}


//
// 保存の実行処理部
//
void	TADSAVE::main()
{
	store_tad();
	del_oldtad();

	return;
}


// ---------------------------------------------------- TADSAVE 内 private 関数
//
// record への書き出し
//
void	TADSAVE::output_data(const B* dat, W size)
{
	if (size < 0) {
		// flush buffer
		if (wbuf.size > 0) {
			wri_rec(fd, wbuf.ofs, wbuf.dat, wbuf.size,NULL,NULL,0);
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
// 管理情報セグメントの書き出し
//
void	TADSAVE::store_info()
{
	TADSEG	inf = {
			TC_ESC | TS_INFO, sizeof(INFOSEG)
		};
	TC	dat[] = {		// padding 対策
			0x0000, 2, 0x0121
		};

	output_data(reinterpret_cast<B*>(&inf), sizeof(TADSEG));
	output_data(reinterpret_cast<B*>(&dat), sizeof(TC) * 3);

	return;
}


//
// 文章開始セグメントの書き出し
//
void    TADSAVE::store_txt()
{
	TADSEG	txt = {
			TC_ESC | TS_TEXT, sizeof(TEXTSEG)
		};
	TEXTSEG	dat = {
			(RECT){{0, 0, 0, 0}}, (RECT){{0, 0, 0, 0}},
			-120, -120, TSC_SYS, 0
		};

	output_data(reinterpret_cast<B*>(&txt), sizeof(TADSEG));
	output_data(reinterpret_cast<B*>(&dat), sizeof(TEXTSEG));

	return;
}


//
// 文章終了セグメントの書き出し
//
void	TADSAVE::store_txtend()
{
	TADSEG	tend = {
			TC_ESC | TS_TEXTEND, 0
		};

	output_data(reinterpret_cast<B*>(&tend), sizeof(TADSEG));

	return;
}


//
// TAD 主 record の書き出し
//
// 必ず、最後に追加(apd_rec())となります
//
void	TADSAVE::store_tad()
{
	// 対象レコードの生成
	ERR     er;

	er = apd_rec(fd, NULL, 0, RT_TADDATA, 0x0000, 0);
	if (er < ER_OK) {
		throw EXCEPT_TADSAVE(er);
	}

	// 作成したはずのレコードに移動
	W	rtype;

	rtype = fnd_rec(fd, F_ENDTOP, RM_TADDATA, 0x0000, NULL);
	if (rtype < ER_OK) {
		throw EXCEPT_TADSAVE(rtype);
	}

	// ヘッダ部分の書き出し
	store_info();
	store_txt();

        // 内容を走査していく
	W	l;
	TLANG	lng;
	const	W	wlen = appl->estr.get_tlen();
	const	WTC*	wstr = appl->estr.get_text();

	lng = TSC_SYS;
	for (l = 0; l < wlen; ++l) {
		if (lng != wtclang(wstr[l])) {
			// 言語/スクリプト指定の切り替え
			lng = wtclang(wstr[l]);

			// 言語/スクリプト指定を TAD 列に変換・出力
			std::vector<TC>	lstr(TLANGtoTC(NULL, -1, lng) + 1);

			TLANGtoTC(lstr.begin(), -1, lng);
			output_data(reinterpret_cast<B*>(lstr.begin()), (lstr.size() - 1) * sizeof(TC));
		}

		// 文字の書き出し
		TC	ch;

		ch = wtcchar(wstr[l]);
		output_data(reinterpret_cast<B*>(&ch), sizeof(TC));
	}

	// 文章終了セグメントの書き出し
	store_txtend();

	// buffer flush
	output_data(NULL, -1);

	return;
}


//
// 古い TAD 主 record を破棄
//
// store_tad() の時より前にある record を走査して削除していきます
//
void	TADSAVE::del_oldtad()
{
	W	mode;

	for (mode = F_NBWD; ; ) {
		W	rtype;

		rtype = fnd_rec(fd, mode, RM_TADDATA, 0x0000, NULL);
		if (rtype == ER_REC) {
			// 削除終了
			break;
		} else if (rtype < ER_OK) {
			// 他の error
			throw EXCEPT_TADSAVE(rtype);
		} else if (rtype == RT_TADDATA) {
			// この record を削除
			mode = (del_rec(fd) >= 0) ? F_NBWD : F_BWD;
		}
	}

	return;
}

