//
//	tadsave.cc (偽仮身一覧/実身への書き込み系)
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

#include	"dbox.h"
#include	"cval.h"
#include	"val.h"
#include	"struct.h"
#include	"err.h"
#include	"except.h"
#include	"debug.h"

#include	"appl.h"
#include	"vobjope.h"
#include	"vobjs.h"
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
	store_lnkfsn();
	del_oldlnkfsn();
	store_tad();
	del_oldtad();

	return;
}


// ---------------------------------------------------- TADSAVE 内 private 関数
//
// LINK / FFUSEN の書き出し
//
void	TADSAVE::store_lnkfsn()
{
	// 最初の LINK ないし FFUSEN に移動
	W	rtype;

	rtype = fnd_rec(fd, F_TOPEND, RM_LINK | RM_FFUSEN, 0x0000, NULL);
	if ((rtype != RT_LINK) && (rtype != RT_FFUSEN)) {
		// 存在していなければ最終 record に移動
		see_rec(fd, 0, -1, NULL);
	}

	// 書き出していく
	VOBJ*	ptr;
	const	VOBJ*	vobj = appl->vobjs.get_vobj();

	if (vobj == NULL) {
		goto EXIT;
	}
	ptr = const_cast<VOBJ*>(vobj);
	do {
		// 一時削除状態のものは相手にしない
		if (ptr->get_del()) {
			goto NEXT;
		}

		// 1 record の書き出し
		ERR	er;

		if (ptr->get_type()) {
			// 仮身
			LINK	lnk;

			ocnv_vob(dvid, ptr->get_vid(), &lnk);
			er = ins_rec(fd, (B*)&lnk, sizeof(LINK), RT_LINK, 0x0000, 0);
		} else {
			// 付箋(機能付箋)
			er = ins_rec(fd, (B*)ptr->get_fusenseg(), ptr->get_seglen(), RT_FFUSEN, 0x0000, 0);
		}

		if (er < ER_OK) {
			// 書き込み error
			throw EXCEPT_TADSAVE(er);
		}

		// 挿入したはずの record に移動する
		rtype = fnd_rec(fd, F_FWD, RM_LINK | RM_FFUSEN, 0x0000, NULL);
		if (rtype < ER_OK) {
			if (rtype != ER_REC) {
				throw EXCEPT_TADSAVE(rtype);
			}
		}

NEXT:
		// 次へ
		ptr = const_cast<VOBJ*>(ptr->get_next());
	} while (ptr != vobj);

EXIT:
	return;
}


//
// 古い LINK / FFUSEN を破棄
//
// store_lnkfsn() 後にある record を走査して削除していきます
//
void	TADSAVE::del_oldlnkfsn()
{
	W	mode;

	for (mode = F_FWD; ; ) {
		W	rtype;

		rtype = fnd_rec(fd, mode, RM_LINK | RM_FFUSEN, 0x0000, NULL);
		if (rtype == ER_REC) {
			// 削除終了
			break;
		} else if (rtype < ER_OK) {
			// 他の error
			throw EXCEPT_TADSAVE(rtype);
		} else if ((rtype == RT_LINK) || (rtype == RT_FFUSEN)) {
			// この record を削除
			mode = (del_rec(fd) >= 0) ? F_FWD : F_NFWD;
		}
	}

	return;
}


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
// 図形開始セグメントの書き出し
//
void	TADSAVE::store_fig()
{
	const	RECT	varea = appl->vobjs.get_allarea();
	TADSEG	fig = {
			TC_ESC | TS_FIG, sizeof(FIGSEG)
		};
	FIGSEG	dat = {
			varea, varea, -120, -120, 0
		};

	output_data(reinterpret_cast<B*>(&fig), sizeof(TADSEG));
	output_data(reinterpret_cast<B*>(&dat), sizeof(FIGSEG));

	return;
}


//
// 仮身セグメントの書き出し
//
void	TADSAVE::store_vobj(W vid)
{
	// 仮身セグメントの大きさの取得
	UW	vsize;

	oget_vob(vid, NULL, NULL, 0, &vsize);

	// 仮身セグメントの取得
	std::vector<B>	vbuf(vsize);
	VOBJSEG*	vseg = reinterpret_cast<VOBJSEG*>(vbuf.begin());

	oget_vob(vid, NULL, vseg, vsize, NULL);

	// 仮身セグメント部分の書き出し
	W	tsize;
	LTADSEG	ltseg;

	ltseg.id = TC_ESC | TS_VOBJ;
	if (vsize <= 0xffff) {
		ltseg.len = vsize;
		tsize = sizeof(TADSEG);
	} else {
		ltseg.len = 0xffff;
		ltseg.llen = vsize;
		tsize = sizeof(LTADSEG);
	}
	output_data(reinterpret_cast<B*>(&ltseg), tsize);
	output_data(vbuf.begin(), vsize);

	return;
}


//
// 図形アプリケーション指定付箋の書き出し(固定化/背景化)
//
void	TADSAVE::store_fappl(UB ltype)
{
	TADSEG	fappl = {
			TC_ESC | TS_FAPPL, sizeof(LOCKSEG)
		};
	LOCKSEG dat = {
			1 | (ltype << 8),
			CVAL::DLED_APPID_0,
			CVAL::DLED_APPID_1,
			CVAL::DLED_APPID_2
		};

	output_data(reinterpret_cast<B*>(&fappl), sizeof(TADSEG));
	output_data(reinterpret_cast<B*>(&dat), sizeof(LOCKSEG));

	return;
}


//
// 図形終了セグメントの書き出し
//
void	TADSAVE::store_figend()
{
	TADSEG	fend = {
			TC_ESC | TS_FIGEND, 0
		};

	output_data(reinterpret_cast<B*>(&fend), sizeof(TADSEG));

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
	ERR	er;

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
	store_fig();

	// 内容を走査していく
	UH	ltype;
	VOBJ*	ptr;
	const	VOBJ*	vobj = appl->vobjs.get_vobj();

	if (vobj == NULL) {
		goto ENDSEG;
	}

	ltype = CVAL::NONE_LOCK;
	ptr = const_cast<VOBJ*>(vobj);
	do {
		// 一時削除状態のものは相手にしない
		if (ptr->get_del()) {
			goto NEXT;
		}

		// 内容の書き出し(仮身以外は何も出さない)
		if (ptr->get_type()) {
			// 背景化/固定化状態の確認
			UH	tbuf;

			tbuf = ((ptr->get_hold()) ? CVAL::HOLD_TYPE : 0) |
			       ((ptr->get_back()) ? CVAL::BACK_TYPE : 0);
			if (tbuf != ltype) {
				ltype = (tbuf >= 2) ? 2 : (tbuf & 1);
				store_fappl(ltype);
			}

			// 仮身
			store_vobj(ptr->get_vid());
		}

NEXT:
		// 次へ
		ptr = const_cast<VOBJ*>(ptr->get_next());
	} while (ptr != vobj);

ENDSEG:
	// 図形終了セグメントの書き出し
	store_figend();

	// buffer flush
	output_data(NULL, -1);

	return;
}


//
// 古い TAD 主 record を破棄
//
// store_tad() 前にある record を走査して削除していきます
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
