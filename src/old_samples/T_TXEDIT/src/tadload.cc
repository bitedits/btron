//
//	tadload.cc (簡易文章編集/実身からの読み込み系)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#include	<basic.h>
#include	<btron/btron.h>
#include	<btron/dp.h>		// tad.h で必要
#include	<btron/vobj.h>
#include	<btron/libapp.h>
#include	<bstring.h>
#include	<tad.h>
#include	<tcode.h>
#include	<tlang.h>
#include	<wtstring.h>

#include	<stack>

#include	"dbox.h"
#include	"cval.h"
#include	"val.h"
#include	"struct.h"
#include	"err.h"
#include	"except.h"
#include	"debug.h"

#include	"appl.h"
#include	"editobj.h"
#include	"strope.h"
#include	"tadload.h"


// ----------------------------------------------------- TADLOAD 内 public 関数
//
// constructor
//
TADLOAD::TADLOAD(const LINK* lnk)
	: nst(0), lng(TSC_SYS), skiptad(false), ln(false)
{
	// 対象実身を開ける
	rinf.fd = opn_fil((LINK*)lnk, F_READ, NULL);
	if (rinf.fd < ER_OK) {
		throw EXCEPT_TADLOAD(rinf.fd);
	}

	sysmsg(DBOX::SMSG_LOAD);
	rinf.ofs = 0;
	rinf.idx = 0;
	rinf.rsize = 0;
	memset(rinf.buf, 0x00, BUFFER_SIZE);
}


//
// destructor
//
TADLOAD::~TADLOAD()
{
	sysmsg(0);
	cls_fil(rinf.fd);
}


//
// 読み込みの主幹部
//
void	TADLOAD::main()
{
	// TAD 主 record を追いかける
	W	mode;

	for (mode = F_TOPEND; ; mode = F_NFWD) {
		// 対象 record を探す
		if (fnd_rec(rinf.fd, mode, RM_TADDATA, 0x0000, NULL) != RT_TADDATA) {
			break;
		}

		// 読み込み情報の初期化
		rinf.ofs = 0;
		rinf.idx = 0;
		memset(rinf.buf, 0x00, BUFFER_SIZE);
		rea_rec(rinf.fd, 0, NULL, 0, &rinf.rsize, NULL);
		DPRINT(("record size : %d\n", rinf.rsize));

		// parser の呼出し
		tad_parse();
	}

	// 読み込み後の状態の確認
	if (nst != 0) {
		// 段数がおかしい
		skiptad = true;
	}
	if (!(ln)) {
		// 最後が改段落/改行でなければ、追加する
		appl->estr.add_text(towtc(lng, TC_NL));
	}

	// 異常通知
	if (skiptad) {
		errpanel(DBOX::EPNL_LOADSKIP, 0);
		appl->eobj->set_skipflg(true);
	}

	return;
}


// ---------------------------------------------------- TADLOAD 内 private 関数
//
// record からの読み込み(TC 1文字づつの読み込み)
//
// force == true : 必ず実身から読み取る
//
TC	TADLOAD::read_buf(bool force)
{
	TC	ch;

	if (rinf.idx >= rinf.rsize) {
		// record の終端に到達
		rinf.idx = -1;
		rinf.ofs = -1;
		ch = TNULL;
	} else {
		// buffer 中からとりだす
		if (((rinf.ofs + BUFFER_SIZE) <= rinf.idx) || (force)) {
			// buffer 中にはなさそうなので record から読み込み
			rinf.ofs = rinf.idx;
			rea_rec(rinf.fd, rinf.ofs, rinf.buf, BUFFER_SIZE, NULL, NULL);
		}
		ch = *(reinterpret_cast<const TC*>(&rinf.buf[rinf.idx - rinf.ofs]));
        }

	return ch;
}


//
// TAD 主 record の内容の解析
//
void	TADLOAD::tad_parse()
{
	do {
		TC	ch;

		ch = read_buf(rinf.idx <= 0);
		if ((ch == TNULL) && (rinf.idx < 0) && (rinf.ofs < 0)) {
			// 終端
			DPRINT(("buffer over.\n"));
			break;
		} else if ((ch & TC_SPEC) == TC_SPEC) {
			// 付箋/セグメント
			chk_TS(ch);
		} else if ((ch & TC_SPEC) == TC_LANG) {
			// 言語/スクリプト指定
			W	n;	// TC_LANG の個数
			TLANG	lbuf;	// 最後の TLANG

			n = -1;		// 最初に +1 されるので初期値は -1
			while ((ch & TC_SPEC) == TC_LANG) {
				// isTLANG() ほどの厳密さではない
				++n;
				lbuf = ch & 0x00ff;
				rinf.idx += sizeof(TC);
				if ((ch & 0x00ff) != 0x00fe) {
					// 下位が 0xfe でなければ TLANG は終了
					break;
				}
				ch = read_buf(false);
			}
			lng = (n << 8) | lbuf;
		} else if ((ch == TC_NL) || (ch == TC_CR) || (ch >= 0x2121)) {
			// 一般文字列/改段落/改行
			appl->estr.add_text(towtc(lng, ch));
			rinf.idx += sizeof(TC);
			ln = ((ch == TC_NL) || (ch == TC_CR));
		} else {
			// 何か
			skiptad = true;
			rinf.idx += sizeof(TC);
		}
	} while ((rinf.idx >= 0) && (rinf.ofs >= 0));

	return;
}
 

//
// 付箋/セグメントの種別毎の分岐
//
void	TADLOAD::chk_TS(TC ch)
{
	W	len;			// 付箋/セグメント長さ(byte 単位)
	UB	id;			// segment ID
	UB	sid;			// segment sub ID
	UB	sattr;			// attribute

	// TADSEG 相当部分の取得
	id = ch & 0x00ff;
	rinf.idx += sizeof(TC);
	len = read_buf(false);
	rinf.idx += sizeof(TC);
	if (len == 0xffff) {
		// large segment(この方法では endian に注意)(LTADSEG 相当)
		len = read_buf(false);
		rinf.idx += sizeof(TC);
		len += (read_buf(false) << 16);
		rinf.idx += sizeof(TC);
	}

	// sub ID とかの取得(ここでは rinf.idx はまだ進めない)
	TC	sch;

	sch = read_buf(false);
	sid = sch >> 8;
	sattr = sch & 0x00ff;

	// segment ID 毎の分岐
	switch (id) {
		case TS_INFO:
			// 管理情報セグメント(無視)
			break;
		case TS_TEXT:
		case TS_FIG:
			// 文章開始セグメント/図形開始セグメント
			++nst;		// 段数の追加のみ
			nlng.push(lng);
			lng = TSC_SYS;
			break;
		case TS_TEXTEND:
		case TS_FIGEND:
			// 文章終了セグメント/図形終了セグメント
			--nst;		// 段数の減少
			if (!(nlng.empty())) {
				lng = nlng.top();
				nlng.pop();
			}
			break;
		default:
			// 何か
			skiptad = true;
			break;
	}

	// rinf.idx を進める
	rinf.idx += len;

	return;
}
