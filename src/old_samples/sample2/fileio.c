/*
	fileio.c	電子手帳 : 汎用ファイル入出力関数

	(C) Copyright 1997-98 by Personal Media Corporation

	※ 準 TAD 形式ファイルを扱うようになっているので注意
	※ 1 セグメントの大きさが 32KB 以上の入出力はサポートしていない
*/
#include <basic.h>
#include <bstdlib.h>
#include <bstring.h>
#include <errcode.h>
#include <tcode.h>
#include <btron/file.h>
#include <btron/message.h>
#include <btron/dp.h>
#include <btron/libapp.h>
#include <btron/cnvend.h>
#include "fileio.h"

#define	lsizeof(n)	((W)sizeof(n))

EXPORT	W	ropen_err;	/* rcreat,ropen,rfdopen のエラーステータス */

/*
 * 現在の読み込み位置を得る
 */
#define	get_cur_ofs(fp)	( (fp)->rec_ofs - (W)((fp)->buf_s - (fp)->buf_p) )

/*
 * 現在のファイル読み込み位置を保存する
 */
#define	set_prv_ofs(fp)	( (fp)->prv_ofs = get_cur_ofs(fp) )

/*
 * 現在のオフセット位置からレコード終端までのサイズを得る
 */
LOCAL	W	record_size( RFILE *fp)
{
	W		size;
	W		err;

	err = rea_rec(fp->fd, fp->rec_ofs, NULL, 0L, &size, NULL);
	if ( err < E_OK ) {
		fp->err = err;
		return 0L;
	}

	return size;
}

/*
 * 指定レコードを探す
 */
LOCAL	W	search_record( RFILE *fp,W mode)
{
	W		rectyp;

	rectyp = fnd_rec(fp->fd, mode, fp->typmsk, 0, &fp->rec_num);

	fp->rec_ofs = 0L;
	fp->prv_ofs = 0L;
	fp->buf_s = 0;
	fp->buf_p = 0;

	if ( rectyp >= E_OK ) {
		fp->rec_typ = rectyp;
		fp->rec_siz = record_size(fp);
	} else {
		if ( rectyp == ER_REC )	fp->eof = TRUE;
		else			fp->err = rectyp;
		fp->rec_siz = 0L;
	}

	return rectyp;
}

/*
 * レコードを I/O バッファに RBUFSIZ バイト読み出す
 */
LOCAL	W	read_record(RFILE *fp)
{
	W		rectyp;
	W		s;

	rectyp = rea_rec(fp->fd, fp->rec_ofs,
				fp->buf, (W)RBUFSIZ, &fp->rec_siz, NULL);
	if ( rectyp < E_OK ) fp->err = rectyp;

	s = ( fp->rec_siz > (W)RBUFSIZ )? RBUFSIZ: (W)fp->rec_siz;
	fp->rec_ofs += s;
	fp->buf_s = s;
	fp->buf_p = 0;

	return rectyp;
}

/*
 * I/O バッファからファイルに buf_p バイト書き出す
 */
LOCAL	W	write_record(RFILE *fp)
{
	W		rectyp;
	W		s;

	rectyp = wri_rec(fp->fd, fp->rec_ofs, fp->buf, (W)fp->buf_p,
						&fp->rec_siz, NULL, 0);
	if ( rectyp < E_OK ) fp->err = rectyp;

	if ( fp->rec_siz >= (W)fp->buf_p ) {
		s = fp->buf_p;
	} else {
		/* 全部は書き込めなかった */
		s = (W)fp->rec_siz;
		fp->eof = TRUE;
	}
	fp->rec_ofs += s;
	fp->buf_s = RBUFSIZ;
	fp->buf_p = 0;

	return rectyp;
}

/*
 * ファイルから１ワード読み出す
 * 読み込んだデータをリターン値として返す
 * エラーがあったときには -1L を返す
 */
LOCAL	W	get_ch( RFILE *fp)
{
	TC		ch;
	W		n;

	if ( fp->buf_p >= fp->buf_s ) {
		/* ファイルから読み込み */
		if ( read_record(fp) < E_OK ) return -1L;
	}

	/* 読み込みサイズ確認 */
	if ( (n = fp->buf_s - fp->buf_p) <= 0 ) return -1L;

	if ( n >= sizeof(TC) ) {
		/* ２バイト連続で読める */
		ch = *((TC*)(fp->buf + fp->buf_p));
		fp->buf_p += sizeof(TC);
		return (W)ch;
	}

	/* ２バイト連続で読めない(１バイト目を保存) */
	ch = fp->buf[fp->buf_p++];

	/* 続きをファイルから読み込む */
	if ( read_record(fp) < E_OK ) return -1L;
	if ( (fp->buf_s - fp->buf_p) <= 0 ) return -1L;

	ch |= fp->buf[fp->buf_p++] << 8;
	return (W)ch;
}

/*
 * ファイルへ１ワード書き出す
 * エラーステータスを返す
 */
LOCAL	W	put_ch(TC ch, RFILE *fp)
{
	W		err;

	/* バッファ空きサイズ確認 */
	if ( (fp->buf_p + sizeof(TC)) > fp->buf_s ) {
		/* ファイルへ書き出す */
		if ( (err = write_record(fp)) < E_OK ) return err;
	}

	/* １ワード書き出し */
	*((TC*)(fp->buf + fp->buf_p)) = ch;
	fp->buf_p += sizeof(TC);

	return E_OK;
}

/*
 * ブロック読み出し
 * 読み出したバイト数を返す
 */
LOCAL	W	get_blk(B *buf,W size, RFILE *fp)
{
	W		s;
	W		n;

	for ( s = size; s > 0; s -= n ) {

		if ( fp->buf_p >= fp->buf_s ) {
			/* ファイルから読み込み */
			if ( read_record(fp) < E_OK ) break;
		}

		/* 読み込みサイズ確認 */
		n = fp->buf_s - fp->buf_p;
		if ( n >= s ) n = s;

		/* I/O buf から buf へ読み込み */
		memcpy(buf, fp->buf + fp->buf_p, n);
		fp->buf_p += n;
		buf += n;
	}

	return size-s;
}

/*
 * ブロック書き出し
 * 書き出したバイト数を返す
 */
LOCAL	W	put_blk(B *buf,W size, RFILE *fp)
{
	W		s;
	W		n;

	for ( s = size; s > 0; s -= n ) {

		if ( fp->buf_p >= fp->buf_s ) {
			/* ファイルへ書き出し */
			if ( write_record(fp) < E_OK || reof(fp) ) break;
		}

		/* 書き出しサイズ確認 */
		n = fp->buf_s - fp->buf_p;
		if ( n >= s ) n = s;

		/* buf から I/O buf へ書き出し */
		memcpy(fp->buf + fp->buf_p, buf, n);
		fp->buf_p += n;
		buf += n;
	}

	return size-s;
}

/*
 * レコードの読み捨て
 */
LOCAL	VOID	skip_rec(UW len, RFILE *fp)
{
	UW		l;

	l = fp->buf_s - fp->buf_p;
	if ( l >= len ) {
		/* buf 内に読み込まれている範囲でスキップ可能 */
		fp->buf_p += (W)len;
	} else {
		/* buf に読み込まれている以上にスキップする */
		fp->rec_ofs += len - l;
		fp->buf_s = 0;
		fp->buf_p = 0;
		fp->rec_siz = record_size(fp);
	}
}

/*
 * ファイルから buf に１セグメント読み出す
 * セグメントサイズが size バイトを超えるときには size バイトまで読み込み
 * 残りは捨てられる
 * リターン値として読み出したバイト数を返す
 */
LOCAL	W	get_seg(B *buf, W size, RFILE *fp)
{
	W		ch;
	UW		len;
	W		s;
	W		n;

	/* １ワード目を読む */
	if ( (s = 2) > size ) return 0;
	if ( (ch = get_ch(fp)) < 0L ) return 0;
	*((TC*)buf)++ = (TC)ch;

	if ( ConvEndianH((TC)ch) < 0xff80 ) return s; /* 1 or 2 バイトコード */

	/* 可変長セグメント */
	if ( (s += 2) > size ) return s-2;
	if ( (ch = get_ch(fp)) < 0L ) return s-2;
	*((TC*)buf)++ = (TC)ch;

	if ( ConvEndianH((TC)ch) == 0xffff ) {
		/* ラージセグメント */
		if ( (s += 4) > size ) return s-4;
		if ( (ch = get_ch(fp)) < 0L ) return s-4;
		*((TC*)buf)++ = (TC)ch;
		len = ConvEndianH(ch);
		if ( (ch = get_ch(fp)) < 0L ) return s-2;
		*((TC*)buf)++ = (TC)ch;
		len |= ConvEndianH(ch) << 16;
	} else {
		/* 通常セグメント */
		len = ConvEndianH((UW)ch);
	}

	/* セグメント本体読み出し */
	n = ( (UW)(size-s) > len )? len: size-s;
	s += get_blk(buf, n, fp);

	if ( (len -= n) > 0L ) {
		/* buf 不足分の読み飛ばし */
		skip_rec(len, fp);
	}

	return s;
}

/*
 * buf からファイルへ１セグメント書き出す
 * リターン値として書き出したバイト数を返す
 *
 *	buf が指す内容は little endian data
 */
LOCAL	W	put_seg( B *buf,RFILE *fp)
{
	TC		ch;
	UW		len;
	W		s = 0;

	/* １ワード目を書き出す */
	ch = *((TC*)buf)++;
	if ( put_ch(ch, fp) < E_OK ) return s;
	s += 2;

	if ( ConvEndianH(ch) < 0xff80 ) return s; /* 1 or 2 バイトコード */

	/* 可変長セグメント */
	ch = *((TC*)buf)++;
	if ( put_ch(ch, fp) < E_OK ) return s;
	s += 2;

	if ( ConvEndianH(ch) == 0xffff ) {
		/* ラージセグメント */
		ch = *((TC*)buf)++;
		if ( put_ch(ch, fp) < E_OK ) return s;
		s += 2;
		len = (UW)ConvEndianH(ch);

		ch = *((TC*)buf)++;
		if ( put_ch(ch, fp) < E_OK ) return s;
		s += 2;
		len |= (UW)ConvEndianH(ch) << 16;
	} else {
		/* 通常セグメント */
		len = (UW)ConvEndianH(ch);
	}

	/* セグメント本体書き出し */
	if ( len >= 0x8000L ) {
		len = 0x7fffL;
		fp->err = ER_SZOVR;
	}
	s += put_blk(buf, (W)len, fp);

	return s;
}

/* ------------------------------------------------------------------------- */

/*
 * ファイルを作成し、typmsk レコードの入出力の準備をする
 * リターン値としてファイルポインタを返す
 * エラー時には NULL が返る
 */
EXPORT	RFILE*	rcreat(LINK *lnk,TC *name,UW typmsk,W opt)
/* ファイル作成先の指定 */
/* ファイル名 */
/* レコードタイプのマスク */
/* 作成方法 (F_FLOAT|F_FIX|F_FIELD) */
{
	RFILE		*fp;
	W		err;

	/* ファイルポインタ(fp)の初期化 */
	ropen_err = ER_NOMEM;
	if ( (fp = (RFILE*)malloc(lsizeof(RFILE))) == NULL ) return NULL;
	memset(fp, 0, sizeof(RFILE));

	/* ファイル作成 */
	if ( (err = cre_fil(lnk, name, NULL, 0, opt)) < E_OK ) goto err_exit;
	fp->fd	   = err;
	fp->typmsk = typmsk;
	fp->mode   = F_UPDATE;

	/* 先頭レコードに移動しておく */
	fp->rec_typ = ER_REC;
	err = search_record(fp, F_TOPEND);
	if ( err < E_OK && err != ER_REC ) goto err_exit;

	ropen_err = E_OK;
	return fp;

err_exit:
	ropen_err = err;
	free(fp);
	return NULL;
}

/*
 * ファイルをオープンし、typmsk レコードの入出力の準備をする
 * リターン値としてファイルポインタを返す
 * エラー時には NULL が返る
 */
EXPORT	RFILE*	ropen(LINK *lnk,UW typmsk,W mode)
/* ファイル指定 */
/* レコードタイプのマスク */
/* オープンモード (F_READ|F_WRITE|F_UPDATE) */
{
	RFILE		*fp;
	W		err;

	/* ファイルポインタ(fp)の初期化 */
	ropen_err = ER_NOMEM;
	if ( (fp = (RFILE*)malloc(lsizeof(RFILE))) == NULL ) return NULL;
	memset(fp, 0, sizeof(RFILE));

	/* ファイルオープン */
	if ( (err = opn_fil(lnk, mode, NULL)) < E_OK ) goto err_exit;
	fp->fd	   = err;
	fp->typmsk = typmsk;
	fp->mode   = mode;

	/* 先頭レコードに移動しておく */
	fp->rec_typ = ER_REC;
	err = search_record(fp, F_TOPEND);
	if ( err < E_OK && err != ER_REC ) goto err_exit;

	ropen_err = E_OK;
	return fp;

err_exit:
	ropen_err = err;
	free(fp);
	return NULL;
}

/*
 * すでにオープンされたファイルに対して、typmsk レコードの入出力の準備をする
 * リターン値としてファイルポインタを返す
 * エラー時には NULL が返る
 */
EXPORT	RFILE*	rfdopen(W fd,UW typmsk,W mode)
/* ファイルディスクリプタ */
/* レコードタイプのマスク */
/* オープンモード (F_READ|F_WRITE|F_UPDATE) */
{
	RFILE		*fp;
	W		err;

	/* ファイルポインタ(fp)の初期化 */
	ropen_err = ER_NOMEM;
	if ( (fp = (RFILE*)malloc(lsizeof(RFILE))) == NULL ) return NULL;
	memset(fp, 0, sizeof(RFILE));

	/* fp 設定 */
	fp->fd	   = fd;
	fp->typmsk = typmsk;
	fp->mode   = mode;

	/* 先頭レコードに移動しておく */
	fp->rec_typ = ER_REC;
	err = search_record(fp, F_TOPEND);
	if ( err < E_OK && err != ER_REC ) goto err_exit;

	ropen_err = E_OK;
	return fp;

err_exit:
	ropen_err = err;
	free(fp);
	return NULL;
}

/*
 * エラークリア
 * リターン値としてエラーコードが返る (必ず no error になるとは限らない)
 */
EXPORT	W	rclearerr( RFILE *fp)
{
	fp->err = E_OK;
	fp->eof = FALSE;
	search_record(fp, F_TOPEND);
	return rerror(fp);
}

/*
 * 先頭レコードに移動する
 * エラーステータスを返す
 */
EXPORT	W	rrewind(RFILE *fp)
{
	search_record(fp, F_TOPEND);
	return rerror(fp);
}

/*
 * バッファの内容をファイルに書き出す
 * 書き出しに成功すれば０(E_OK)、失敗すればエラーステータスを返す
 */
EXPORT	W	rflush( RFILE *fp)
{
	/* 読み込のみのモードでは不要 */
	if ( (fp->mode & F_UPDATE) == F_READ ) return E_OK;

	if ( fp->buf_p > 0 ) {
		/* I/O buf を書き出す */
		write_record(fp);
	}
	return rerror(fp);
}

/*
 * ファイルをクローズする
 * クローズに成功すれば０(E_OK)、失敗すればエラーステータスを返す
 */
EXPORT	W	rclose( RFILE *fp)
/* ファイルポインタ */
{
	W		err1, err2;

	err1 = rflush(fp);

	err2 = cls_fil(fp->fd);
	free(fp);

	return ( err1 == E_OK )? err2: err1;
}

/*
 * ファイルから size バイトのブロックを count 個 buf に読み込む
 * リターン値として読み出したブロックの個数を返す
 */
EXPORT	W	rread(B *buf,W size,W count, RFILE *fp)
/* バッファ */
/* ブロックサイズ */
/* ブロック個数 */
/* ファイルポインタ */
{
	W		i;

	/* レコード終端ならば次のレコードへ移動 */
	if ( reor(fp) ) search_record(fp, F_NFWD);

	set_prv_ofs(fp); /* 現在位置保存 */

	for ( i = 0; i < count; ++i ) {

		/* 現在レコードに１ブロック分のデータが残っていなければ
		   読まずに終了 */
		if ( size > fp->rec_siz + (fp->buf_s - fp->buf_p) ) break;

		/* １ブロック分読み出し */
		if ( get_blk(buf, size, fp) < 0 ) break;
		buf += size;
	}

	return i;
}

/*
 * buf から size バイトのブロックを count 個 ファイルに書き出す
 * リターン値として書き出したブロックの個数を返す
 */
EXPORT	W	rwrite(B *buf,W size,W count, RFILE *fp)
/* バッファ */
/* ブロックサイズ */
/* ブロック数 */
/* ファイルポインタ */
{
	W		i;

	for ( i = 0; i < count; ++i ) {

		/* １ブロック分書き出し */
		if ( put_blk(buf, size, fp) < size ) break;
		buf += size;
	}

	return i;
}

/*
 * ファイルから buf に１セグメント読み出す
 * セグメントサイズが size バイトを超えるときには size バイトまで読み込み
 * 残りは捨てられる
 * リターン値として読み出したバイト数を返す
 */
EXPORT	W	rgetseg(B *buf,W size, RFILE *fp)
{
	/* レコード終端ならば次のレコードへ移動 */
	if ( reor(fp) ) search_record(fp, F_NFWD);

	set_prv_ofs(fp); /* 現在位置保存 */

	/* １セグメント読み出し */
	return get_seg(buf, size, fp);
}

/*
 * buf からファイルに１セグメント書き出す
 * エラーステータスを返す
 */
EXPORT	W	rputseg(B *buf, RFILE *fp)
{
	/* １セグメント書き出し */
	put_seg(buf, fp);
	return rerror(fp);
}

/*
 * 前回の読み出しを無効にする (読み出さなかったことにする)
 * 処理できたときには０(E_OK)、処理できなかったときには負数を返す
 */
EXPORT	W	runget( RFILE *fp)
{
	W		n;

	/* 書き込みモードでは不可 */
	if ( (fp->mode & F_WRITE) != 0 ) return -1;

	/* 戻すバイト数 */
	n = get_cur_ofs(fp) - fp->prv_ofs;
	if ( n <= 0L ) return -1;

	/* ポインターを戻し、バッファをクリアする */
	if ( n <= (W)fp->buf_p ) {
		fp->buf_p -= (W)n;
	} else {
		fp->rec_ofs = fp->prv_ofs;
		fp->buf_p = 0;
		fp->buf_s = 0;
		fp->rec_siz = record_size(fp);
	}

	return fp->err;
}

/*
 * ファイルから最大 size-1 バイトまたは改段落に出会うまでのデータを
 * buf に読み込み、最後にヌルを追加する
 * (改段落もデータに含まれる)
 * リターン値として読み出したバイト数を返す
 */
EXPORT	W	rgets(B *buf,W size, RFILE *fp)
/* バッファ */
/* サイズ */
/* ファイルポインタ */
{
	W		n;
	W		s;

	/* レコード終端ならば次のレコードへ移動 */
	if ( reor(fp) ) search_record(fp, F_NFWD);

	set_prv_ofs(fp); /* 現在位置保存 */

	for ( s = size; s > 0; s -= n ) {

		/* １セグメント読み出し */
		if ( (n = get_seg(buf, s, fp)) <= 0 ) break;

		/* 改段落確認 */
		if ( n == 2 && *((TC*)buf) == TK_NL ) {
			buf += n;
			s -= n;
			if ( s >= 2 ) {
				*((TC*)buf) = TNULL;
				s -= 2;
			}
			break;
		}

		buf += n;
	}

	return size - s;
}

/*
 * buf からデータを読み出し、ヌルに出会うまでファイルに書き出す
 * リターン値として書き出したバイト数を返す
 */
EXPORT	W	rputs( B *buf,RFILE *fp)
/* バッファ */
/* ファイルポインタ */
{
	W		s = 0;
	W		n;

	while ( !rerror(fp) && *(TC*)buf != TNULL ) {
		n = put_seg(buf, fp);
		buf += n;
		s += n;
	}

	return s;
}

/*
 * １文字書き出す
 * エラーステータスを返す
 */
EXPORT	W	rputc(TC ch,RFILE *fp)
{
	return put_ch(ch, fp);
}
