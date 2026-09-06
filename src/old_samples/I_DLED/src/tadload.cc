//
//	tadload.cc (偽仮身一覧/実身からの読み込み系)
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

#include	<vector>

#include	"dbox.h"
#include	"cval.h"
#include	"val.h"
#include	"struct.h"
#include	"err.h"
#include	"except.h"
#include	"macro.h"
#include	"debug.h"

#include	"appl.h"
#include	"editobj.h"
#include	"vobjope.h"
#include	"vobjs.h"
#include	"tadload.h"


// ----------------------------------------------------- TADLOAD 内 public 関数
//
// constructor
//
TADLOAD::TADLOAD(const LINK* lnk)
	: nst(0), vreg(0), ltype(CVAL::NONE_LOCK),
	  skiptad(false), skipobj(false)
{
	// 対象実身を開ける
	rinf.fd = opn_fil((LINK*)lnk, F_READ, NULL);
	if (rinf.fd < ER_OK) {
		throw EXCEPT_TADLOAD(rinf.fd);
	}

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
	cls_fil(rinf.fd);
}


//
// 読み込みの主幹部
//
void	TADLOAD::main()
{
	// LINK と機能付箋を追いかける
	read_lnkfsn();

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

		// paser の呼出し
		tad_parse();
	}

	// 読み込み後の状態の確認
	if (nst != 0) {
		// 段数がおかしい
		skiptad = true;
	}
	if (vreg < rdat.size()) {
		// 残りの登録
		VOBJSEG	dmy = DEF_VOBJSEG;

		dmy.view.c.left = CVAL::VOBJ_HGAP;
		dmy.view.c.right = CVAL::VOBJ_HGAP;
		for ( ; vreg < rdat.size(); ++vreg) {
			const	RECT	varea = appl->vobjs.get_allarea();

			dmy.view.c.top = varea.c.bottom - CHSSTD + CVAL::VOBJ_VGAP;
			dmy.view.c.bottom = dmy.view.c.top;
			if (rdat[vreg].vlnk) {
				reg_vobj((VLINK*)rdat[vreg].buf.begin(), &dmy, sizeof(VOBJSEG));
			} else {
				reg_vobj(NULL, rdat[vreg].buf.begin(), rdat[vreg].buf.size());
			}

			// 登録した仮身の固定化/背景化状態の設定
			if (ltype != CVAL::NONE_LOCK) {
				VOBJ*	vobj;

				vobj = const_cast<VOBJ*>(appl->vobjs.get_vobj());
				if (vobj != NULL) {
					VOBJ*	ptr;

					ptr = const_cast<VOBJ*>(vobj->get_prev());
					ptr->set_hold((ltype & CVAL::HOLD_TYPE) != 0);
					ptr->set_back((ltype & CVAL::BACK_TYPE) != 0);
				}
			}
		}
	}

	// 読み飛ばしの報告
	if (skipobj) {
		errpanel(DBOX::EPNL_VOBJSKIP, 0);
		appl->eobj->set_skipflg(true);
	} else if (skiptad) {
		errpanel(DBOX::EPNL_LOADSKIP, 0);
		appl->eobj->set_skipflg(true);
	}

	return;
}


// ---------------------------------------------------- TADLOAD 内 private 関数
//
// LINK と機能付箋を追いかける
//
void	TADLOAD::read_lnkfsn()
{
	W	mode;

	for (mode = F_TOPEND; ; mode = F_NFWD) {
		W	rtype;

		rtype = fnd_rec(rinf.fd, mode, RM_LINK | RM_FFUSEN, 0x0000, NULL);
		if (rtype == RT_LINK) {
			RECDAT	dat;

			dat.vlnk = true;
			dat.buf.resize(sizeof(VLINK));
			rea_rec(rinf.fd, 0, dat.buf.begin(), sizeof(VLINK), NULL, NULL);
			rdat.push_back(dat);
		} else if (rtype == RT_FFUSEN) {
			// 容量の取得
			W	size;

			rea_rec(rinf.fd, 0, NULL, 0, &size, NULL);

			// 内容の読み込み
			RECDAT	dat;

			dat.vlnk = false;
			dat.buf.resize(size);

			rea_rec(rinf.fd, 0, dat.buf.begin(), size, NULL, NULL);
			rdat.push_back(dat);
		} else {
			// 終わり
			break;
		}
	}

	return;
}


//
// 仮身/付箋の登録をする
//
void	TADLOAD::reg_vobj(const VLINK* lnk, const VP dat, W len)
{
	// 仮身の登録
	try {
		appl->vobjs.add_vobj(lnk, dat, len);
	} catch (EXCEPT_VOBJ& err) {
		ERR	er;

		er = err.get_err();
		DPRINT(("%s : %d(%d)\n", err.what(), er, er >> 16));
		if (er == ER_LIMIT) {
			// 制限抵触
			skipobj = true;
		} else {
			throw EXCEPT_TADLOAD(er);
		}
	} catch (EXCEPT_VOBJS& err) {
		ERR	er;

		er = err.get_err();
		DPRINT(("%s : %d(%d)\n", err.what(), er, er >> 16));
	} catch (std::bad_alloc) {
		DPRINT(("memory allocation error.\n"));
		throw EXCEPT_TADLOAD(ER_NOMEM);
	} catch (...) {
		DPRINT(("other exception.\n"));
		throw EXCEPT_TADLOAD(ER_SYS);
	}

	return;
}


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
			// 言語/スクリプト指定(無視)
			skiptad = true;
			while ((ch & TC_SPEC) == TC_LANG) {
				rinf.idx += sizeof(TC);
				ch = read_buf(false);
			}
		} else {
			// 一般文字列(無視)
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
			break;
		case TS_TEXTEND:
		case TS_FIGEND:
			// 文章終了セグメント/図形終了セグメント
			--nst;		// 段数の減少
			break;
		case TS_VOBJ:
			// 仮身セグメント
			get_TS_VOBJ(len);
			break;
		case TS_FAPPL:
			get_TS_FAPPL(len);
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


//
// TS_VOBJ の取得
//
void	TADLOAD::get_TS_VOBJ(W len)
{
	if (vreg >= rdat.size()) {
		// 対応する LINK がない
		;			// 特になにもしない
	} else {
		// 実身からの読み込み
		std::vector<B>	vbuf(len);

		rea_rec(rinf.fd, rinf.idx, vbuf.begin(), len, NULL, NULL);

		// 仮身の登録(対応する仮身がくる間で走査
		bool	rflg;

		rflg = true;
		while ((vreg < rdat.size()) && (rflg)) {
			if (rdat[vreg].vlnk) {
				// 仮身セグメントを登録
				reg_vobj((VLINK*)rdat[vreg].buf.begin(), vbuf.begin(), vbuf.size());
				rflg = false;
			} else {
				// 機能付箋を登録
				reg_vobj(NULL, rdat[vreg].buf.begin(), rdat[vreg].buf.size());
			}

			// 登録した仮身の固定化/背景化状態の設定
			if (ltype != CVAL::NONE_LOCK) {
				VOBJ*	vobj;

				vobj = const_cast<VOBJ*>(appl->vobjs.get_vobj());
				if (vobj != NULL) {
					VOBJ*	ptr;

					ptr = const_cast<VOBJ*>(vobj->get_prev());
					ptr->set_hold((ltype & CVAL::HOLD_TYPE) != 0);
					ptr->set_back((ltype & CVAL::BACK_TYPE) != 0);
				}
			}
			++vreg;
		}
	}

	return;
}


//
// TS_FAPPPL の取得(固定化/背景化の設定)(DLED 互換)
//
void	TADLOAD::get_TS_FAPPL(W len)
{
	if (len == sizeof(LOCKSEG)) {
		// 実身からの読み込み
		LOCKSEG	lseg;

		rea_rec(rinf.fd, rinf.idx, (B*)&lseg, sizeof(LOCKSEG), NULL, NULL);

		// 内容の確認
		if ((lseg.app_id1 == CVAL::DLED_APPID_0) &&
		    (lseg.app_id2 == CVAL::DLED_APPID_1) &&
		    (lseg.app_id3 == CVAL::DLED_APPID_2)) {
			// DLED の図形アプリケーション指定付箋
			ltype = (lseg.id_at >> 8);
		}
	}

	return;
}
