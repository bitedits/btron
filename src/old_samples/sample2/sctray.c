/*
	sctray.c	電子手帳 : トレー入出力

	(C) Copyright 1997-98 by Personal Media Corporation
*/
#include "sched.h"
#include <tlang.h>
#include <mtstring.h>

/*
 * トレーへ出力
 *	文字列のみを仮定
 */
EXPORT	W	put_tray(W to,TC *buf,W len,RECT *rp)
/* 出力先。0：トレー、1：一時トレー */
/* データバッファ */
/* データ長(W単位) */
/* 文章開始セグメントの view */
{
	TRAYREC	*tray;
	W	err;
	TEXTSEG	text;
	FUNCP	put_fn;

	if (to != 0 && to != 1) {
		return -1;
	}
	put_fn = (to)? (FUNCP)tset_dat : (FUNCP)tpsh_dat;

	/* トレーレコードを確保する */
	if ((tray = (TRAYREC*)malloc(lsizeof(TRAYREC)*2)) == (TRAYREC*)NULL) return ER_NOMEM;
	memset(tray, 0, sizeof(TRAYREC));

	tray[1].id = TR_TEXT;
	tray[1].len = len * 2;
	tray[1].dt = (B*)buf;

	text.draw = text.view = *rp;
	text.v_unit = text.h_unit = 0;
	text.lang = 0x21;
	text.bgpat = 0;

	tray[0].id = TS_TEXT;
	tray[0].len = lsizeof(TEXTSEG);
	tray[0].dt = (B*)&text;

	ConvEndianHs(buf, buf, len);
	ConvEndianStruct((B*)&text, (B*)&text, TEXTSEG_STRUCT, sizeof(text));
	err = (*put_fn)( tray, 2, getdbox(TX_TRAYTL) );
	ConvEndianStruct((B*)&text, (B*)&text, TEXTSEG_STRUCT, sizeof(text));
	ConvEndianHs(buf, buf, len);

	free(tray);

	return err;
}

/* トレーバッファ */
LOCAL	TC	tray_buff[TBUFLEN];


/*
 *	トレーへ複写・移動
 */
EXPORT	W	text_to_tray(ix, cut)
	W	ix;		/* ウィンドウ番号 */
	W	cut;		/* 移動／複写 (!0/0) */
{
	W	pid, no;
	TC	buf[TBUFLEN];
	TC	erbuf[TBUFLEN];
	TC	*buff;		/* テキストボックスのバッファ */
	W	len, err;
	RECT	r;

	/* ix のどのテキストボックスが選択されているか */
	pid = wpinfo[ix].pid[(no = wpinfo[ix].tbox_no)];

	/* テキストボックスバッファを設定 */
	no -= wpinfo[ix].tbox_start;
	buff = tbufp[ix][no];

	/* @@選択領域の有無(メニューでも使う)？ */

	/* エラー時のためにテキストボックス全体を読み出す */
	cget_val( pid, TBUFLEN, (W*)erbuf );

	/* 選択領域を取り込む */
	if ((len = tx_cut_txt(pid, TBUFLEN, buf, cut)) <= 0) return -1;

	cget_val( pid, TBUFLEN, (W*)buff );

	/* トレーバッファにセットする */
	memcpy(tray_buff, buf, len * sizeof(TC));

	/* トレーへデータをセットする */
	setrect(r, 0, 0, (CHSSTD + (CHSSTD>>3)) * len, CHSSTD);
	if ( (err = put_tray( 0, tray_buff, len, &r )) <= 0 ) {
		errpanel((err < 0)? ER_CUT : ER_MOVE, err);
		txt_recover( pid, buff, erbuf );
		return -1;
	}

	if (cut != 0) {
		if (ix != SRC_WIN) {
			modified = TRUE;
		}
		if (ix == ADR_WIN && no == 0) chg_adr_yomi();
	}

	return E_OK;
}
/*
 * トレーから入力
 */
LOCAL	W	tray_rec;
LOCAL	W	cont_rec;
LOCAL	B*	tray_buf = (B*)NULL;
LOCAL	UW	tbuf_size = 0;		/* 確保した長さ(BYTE) */
LOCAL	UW	rbuf_size = 0;		/* 読み込んだ長さ(BYTE) */

#define	TRAYBUFSIZ	0x1000L

/*
 * トレーから入力の開始処理
 */
EXPORT	W	init_poptray()
{
	nosupport = FALSE;
	tray_rec = 1;
	cont_rec = 0;
	if (!tray_buf) {
	    /* トレーバッファ@@ */
	    if ( !(tray_buf = (B*)malloc(tbuf_size = TRAYBUFSIZ) ) )
		return ER_NOMEM;
	}
	return 0;
}

/*
 * トレーから入力の終了処理
 */
EXPORT	VOID	fin_poptray()
{
	if (tray_buf) free(tray_buf);		/* 920411 */
	tray_buf = (B*)NULL;
	tbuf_size = 0;
}

/*
 * トレーから入力
 */
EXPORT	W	pop_tray(W from, TC **buf, UW *tlen, W *eof)
	/* 入力先。0：トレー、1：一時トレー */
	/* データバッファへのポインタ */
	/* データ長(W単位) */
	/* 1：終了時、0：継続 */
{
	W	tadid, id;
	UW	size, len;
	W	rec;
	FUNCP	pop_fn;

	*eof = 0;

	pop_fn = (from)? (FUNCP)tget_dat : (FUNCP)tpop_dat;

	/* トレーから読み込み。
	 * 先頭８バイトは、ＴＡＤセグメントヘッダのために空けておく。
	 */
AGAIN:
	id = (*pop_fn)(&tray_buf[8], tbuf_size - 8, &size, tray_rec, NULL);
	if (id <= 0) {
	    if (id == EX_PAR) {
		if (size) {		/* バッファサイズ不足 */
		    /* 読み込み用バッファの再確保 */
		    free(tray_buf);
		    if ((tray_buf = (B*)malloc(tbuf_size = size + 8)))
			goto AGAIN;	/* 再度データを読み込む */
		    id = ER_NOMEM;
		} else {		/* レコードがない */
		    *eof = 1;
		    return 0;
		}
	    } else if (id == 0) id = -10000;	/* トレーは空 */
	    return id;
	}

	/* バッファに読み込んだトレーデータをＴＡＤに変換する */
	tadid = id & ~TR_CONT;
	if ( tadid != TR_TEXT && tadid != TR_FIG ) tadid |= 0xff00;

	/* @@size が 64k 以上の場合が必要 */
	rbuf_size = size;
	len = size;
	if (id & TR_CONT) {
	    if (cont_rec == 0) {    /* 接続レコード -- 全体の len を求める */
		for (rec = tray_rec; id & TR_CONT; ) {
		    if ((id = (*pop_fn)(NULL, 0L, &size, ++rec, NULL)) < 0)
			return id;
		    len += size;
		}
		cont_rec = 1;
	    } else tadid = 0;
	} else if (cont_rec) {tadid = 0; cont_rec = 0;}

	if ((tadid & 0x8000) == 0) {	/* tadid >= 0 */
		/* TR_TEXT 等 */
		*buf = (TC*)&tray_buf[8];
		*tlen = rbuf_size / 2;
	} else if (len >= 0x10000) {
		*((H*)tray_buf) = ConvEndianH(tadid);
		*((H*)(tray_buf+2)) = 0xffff;
		*((H*)(tray_buf+4)) = ConvEndianH(len);
		*buf = (TC*)tray_buf;
		*tlen = (rbuf_size + 8) / 2;
	} else {
		*((H*)(tray_buf+4)) = ConvEndianH(tadid);
		*((H*)(tray_buf+6)) = ConvEndianH(len);
		*buf = (TC*)&tray_buf[4];
		*tlen = (rbuf_size + 4) / 2;
	}
	tray_rec++;
	return 0;
}

/*
 * トレーから入力
 *	文字列のみを入力（関数値は読み込み文字数）
 */
static	B	invalid_code[] = {
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,		/*  0- F */
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 	/* 10-1F */
	1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 	/* 20-2F */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 	/* 30-3F */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 	/* 40-4F */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 	/* 50-5F */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 	/* 60-6F */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 	/* 70-7F */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 	/* 80-8F */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 	/* 90-9F */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 	/* A0-AF */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 	/* B0-BF */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 	/* C0-CF */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 	/* D0-DF */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 	/* E0-EF */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 	/* F0-FF*/
};

EXPORT	W	text_from_tray(W ix, W from, PNT pnt)
/* ix: ウィンドウ番号 */
/* from: 入力先。0：トレー、1：一時トレー */
/* pnt: 挿入位置 */
{
	W	i, pno, tno, sz, er, eof;
	UW	seglen, remain;
	TC	buff[TBUFLEN];
	TC*	rbuff;
	TC	cc;
	UW	pos, len;
	W code;
	W work = 0;
	TC *multi_tlang = 0;
	W illegal_mode = 0;

	/* カレンダーは入力不可 */
	if (ix == CAL_WIN) return -1;

	tno = (pno = wpinfo[ix].tbox_no) - wpinfo[ix].tbox_start;

	sz = maxtxsz[ix][tno];

	if ((er = init_poptray()) < 0) return er;

	for ( i = remain = 0; i < sz; ) {
	    if (pop_tray(from, &rbuff, &len, &eof)) goto EEXIT;
	    ConvEndianHs(rbuff, rbuff, len);
	    if (eof) break;
	    if (remain > len) { remain -= len; continue; }
	    pos = remain; remain = 0;
	    while (i < sz && pos < len) {
		    /* 言語指定を、考える */
		    cc = rbuff[pos++];
		    if (cc < 0xfeff) {	/* 文字コード */
			    if (invalid_code[cc >> 8] || invalid_code[cc & 0xff]) {
				    if (cc != 0) {
					    nosupport = TRUE;
				    }
				    continue;
			    }
			    code = isTLANGch (cc, &work);
			    if (code != 0) {
				    if (code > 0) {
					    illegal_mode = 0;
					    multi_tlang = 0;
				    } else if (code == -2) {
					    /* 複数バイト言語指定 */
					    illegal_mode = 0;
					    if (!multi_tlang) {
						    multi_tlang = buff + i;
					    }
				    } else /*if (code == -1)*/ {
					    illegal_mode = 1;
					    illegal = TRUE;
					    continue;
				    }
			    }
			    /* 文字のみバッファに */
			    if (!illegal_mode) {
				    buff[i++] = cc;
				    /* バッファがいっぱいになったら、一意表現にして
				       縮まらないか、確認してみる */
				    if (i == sz && !multi_tlang) {
					    W new_length = mtc_unique (buff, buff, i);
					    if (new_length < i - 1) {
						    i = new_length;
					    }
				    }
			    }
		    } else if (cc >= 0xff80 || cc == (0xff00|TR_VOBJ)) {
			    /* 可変長セグメントは読み飛ばし */

		    /* 管理情報・文章開始・文章終了は、未サポートにはしない */
			    cc &= 0x00ff;
			    if (cc != TS_INFO && cc != TS_TEXT && cc != TS_TEXTEND)
				    nosupport = TRUE;

		    /* セグメントの長さ分スキップする */
			    if ((seglen = rbuff[pos++]) == 0xffff) {
				    seglen = *((UW*)&rbuff[pos]); pos += 2;
			    }
			    seglen /= 2;
			    if (pos + seglen > len) {
				    remain = seglen + pos - len;
				    break;
			    } else {
				    pos += seglen;
			    }
		    } else {		/* その他の２バイトコード */
			    nosupport = TRUE;
		    }
	    }
	    if (multi_tlang) {
		    /* 完結しない複数バイト言語指定があった */
		    *multi_tlang = TNULL;
		    i = multi_tlang - buff;
		    nosupport = TRUE;
	    }
	}
	if (i > 0) {	/* テキストデータがなかった場合は更新しない */
		W pid = wpinfo[ix].pid[pno];
		W max = maxtxsz[ix][tno];

		buff[i] = TNULL;

		er = cins_txt (pid, pnt, buff);
		if (er == EX_PAR) {
			/* 一文字も入らなかった時、cins_txt は EX_PAR を返すので無視 */
			er = cget_val (pid, 0, 0);
			goto EEXIT;
		}
		if (er > 0) {
			er = cget_val (pid, max, (VP) tbufp [ix] [tno]);
			if (er > 0) {
				if (ix == ADR_WIN && tno == 0 ) {
					/* 「よみ」が変更された：住所録のソート */
					chg_adr_yomi();
				}
				if ( ix != SRC_WIN ) {
					modified = TRUE; /* 編集された */
				}
				er = i;		/* 読み込み文字数を返す */
			}
		}
	}

EEXIT:
	fin_poptray();
	return er;
}
