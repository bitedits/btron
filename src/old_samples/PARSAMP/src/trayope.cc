//
//	trayope.cc (パーツ操作例/トレー操舵系)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#include	<basic.h>
#include	<btron/btron.h>
#include	<btron/dp.h>		// tad.h 用に必要
#include	<btron/hmi.h>
#include	<btron/libapp.h>
#include	<tad.h>
#include	<tcode.h>
#include	<errcode.h>
#include	<tlang.h>

#include	<new>
#include	<memory>
#include	<vector>

#include	"cval.h"
#include	"dbox.h"

#include	"trayope.h"


// -------------------------------------- TRAYOPE 内 public 関数(static member)
//
// (一時含む)トレーへ複写/移動
//
void	TRAYOPE::push_tray(W pid, bool tmp, bool cut)
{
	ERR	er;
	W	l;
	TC	buf[CVAL::STR_LEN + 1];
	TRAYREC	trec[2];
	TEXTSEG	head = {
			{{0, 0, 0, 0}}, {{0, 0, 0, 0}}, 0, 0, TSC_SYS, 0
		};
 
        er = ER_OK;
 
	// 内容の取り出し
	l = ccut_txt(pid, CVAL::STR_LEN + 1, buf, 0);
	if (l <= ER_OK) {
		goto EXIT;
	}
 
	// トレーレコードの生成
	trec[0].id = TS_TEXT;
	trec[0].len = sizeof(TEXTSEG);
	trec[0].dt = reinterpret_cast<B*>(&head);
	trec[1].id = TR_TEXT;
	trec[1].len = l * sizeof(TC);
	trec[1].dt = reinterpret_cast<B*>(buf);
 
	if (tmp) {
		// 一時トレーを経由する処理
		WERR	rv;
 
		if (wevt.s.wid <= 0) {
			goto EXIT;
		}
		rv = tset_dat(trec, 2);
		if (rv < ER_OK) {
			errpanel(DBOX::EPNL_TMPTRAY, rv);
			goto EXIT;
		}
 
		// 要求の送信
		wevt.r.type = EV_REQUEST;
		wevt.r.cmd = W_PASTE;
		wevt.s.time = PNTtoUW(wevt.s.pos);
		rv = wsnd_evt(&wevt);
		if (rv < ER_OK) {
			tset_dat(NULL, 0);
			errpanel(DBOX::EPNL_PASTE, rv);
			goto EXIT;
		}

		// 応答待ち
		rv = wwai_rsp(&wevt, W_PASTE, 60000);
		if (rv == W_ACK) {
			// 相手が受け入れてくれた
			if ((wevt.s.pos.x == static_cast<H>(0x8000)) &&
			    (wevt.s.pos.y == static_cast<H>(0x8000))) {
				// 削除しなくてもよいと言われた
				cut = false;
			}
                } else {
			// 相手が受け入れてくれなかった
			tset_dat(NULL, 0);
			errpanel(DBOX::EPNL_PASTE, rv);
			goto EXIT;
		}
	} else {
		// トレーを経由する処理
		WERR	rv;

		rv = tpsh_dat(trec, 2, NULL);
		if (rv < ER_OK) {
			errpanel(DBOX::EPNL_PUSHTRAY, rv);
			goto EXIT;
		}
	}

	// 選択範囲を削除
	if (cut) {
		ccut_txt(pid, 1, buf, 1);
	}

	// 一時トレー経由の場合はウィンドウの切り替え(一連の作業後に切り替え)
	if (tmp) {
		wswi_wnd(wevt.s.wid, NULL);
	}

EXIT:
	return;
}


//
// (一時含む)トレーから複写/移動
//
// pid が負の場合は挿入は行われない
// 返り値は挿入(予定)文字列長さ(TC 単位)(末尾の TNULL 含む)
//
W	TRAYOPE::pop_tray(W pid, bool tmp, bool cut, PNT pos)
{
	W	len;
	W	rec;			// tray record 走査用
	W	size;			// 取り出した tray record size
	W	type;			// 取り出した tray record type
	FUNCP	tfunc;			// 用いる tray 操舵関数
	std::vector<TC>	buf;		// tray からの内容の取り出し用
	std::vector<TC>	buf2;		// 挿入用
 
	if (tmp) {
		tfunc = reinterpret_cast<FUNCP>(tget_dat);
	} else {
		tfunc = reinterpret_cast<FUNCP>(tpop_dat);
		pos = (PNT){0x8000, 0x8000};
	}

	// 先頭の文章レコードへ移動/内容の取り出し
	for (rec = 1; ; ++rec) {
		type = (*tfunc)(NULL, 0, &size, rec, NULL);
		if (type < ER_OK) {
			goto EXIT;
		}
		if (type == TR_TEXT) {
			try {
				buf.resize(size / sizeof(TC));
				(*tfunc)(buf.begin(), size, NULL, rec, NULL);
			} catch (...) {
				// vector がこけたら廃棄
				size = 0;
			}
			break;
		}
	}

	// 値(文字列)の検査/挿入
	W	l;

	for (l = 0; (l < size / static_cast<W>(sizeof(TC))) && (buf2.size() < CVAL::STR_LEN); ) {
		if ((buf[l] & TC_SPEC) == TC_SPEC) {
			// 付箋を飛ばす
			LTADSEG*	ltseg;

			ltseg = reinterpret_cast<LTADSEG*>(&buf[l]);
			l += ((ltseg->len == 0xffff) ? (sizeof(LTADSEG) + ltseg->llen) : (sizeof(TADSEG) + ltseg->len)) / sizeof(TC);
		} else if (buf[l] < TK_KSP) {
			// 制御コードは読み飛ばす
			++l;
		} else {
			// その他の文字・言語/スクリプト指定
			try {
				buf2.insert(buf2.end(), buf[l]);
				++l;
			} catch (...) {
				// vector がこけたら廃棄
				buf2.clear();
				l = size / static_cast<W>(sizeof(TC));
			}
		}
	}

	if (buf2.size() > 0) {
		try {
			buf2.insert(buf2.end(), TNULL);
			if (pid >= 0) {
				cins_txt(pid, pos, buf2.begin());
				// 取り出したレコードを削除
				if (tmp) {
					if (cut) {
						tset_dat(NULL, 0);
					}
				} else {
					if (cut) {
						tdel_dat();
					}
				}
			}
		} catch (...) {
			// vector がこけたら廃棄
			buf2.clear();
		}
	}

EXIT:
	return buf2.size();
}
