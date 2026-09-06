/*
	fileio.h	電子手帳 : 汎用ファイル入出力関数用ヘッダ

	(C) Copyright 1997-98 by Personal Media Corporation
*/
#ifndef	_FILEIO_H
#define	_FILEIO_H

#define	RBUFSIZ		1024		/* I/O バッファサイズ */

typedef	struct {
	W		fd;		/* ファイルディスクリプタ */
	W		mode;		/* オープンモード */
	UW		typmsk;		/* レコードタイプマスク */
	W		rec_typ;	/* 現在レコードのタイプ */
	W		rec_num;	/* 現在レコード番号 */
	W		rec_siz;	/* レコードの残りサイズ */
	W		rec_ofs;	/* レコード内の現在オフセット */
	W		prv_ofs;	/* 前回読み込み時のオフセット */
	BOOL		eof;		/* EOF ステータス */
	W		err;		/* エラーステータス */
	W		buf_p;		/* I/O バッファ内の現在位置 */
	W		buf_s;		/* I/O バッファ内の有効サイズ */
	B		buf[RBUFSIZ];	/* I/O バッファ */
} RFILE;

#define	rfileno(fp)	((fp)->fd)
#define	rtype(fp)	((fp)->rec_typ)
#define	rrecordno(fp)	((fp)->rec_num)
#define	rerror(fp)	((fp)->err)
#define	reof(fp)	((fp)->eof)
#define	reor(fp)	((fp)->rec_siz <= 0L && (fp)->buf_p >= (fp)->buf_s)

IMPORT	RFILE*	rcreat(LINK *lnk, TC *name, UW typmsk, W opt);
IMPORT	RFILE*	ropen(LINK *lnk, UW typmsk, W mode);
IMPORT	RFILE*	rfdopen(W fd, UW typmsk, W mode);
IMPORT	W	rclearerr(RFILE *fp);
IMPORT	W	rrewind(RFILE *fp);
IMPORT	W	rflush(RFILE *fp);
IMPORT	W	rclose(RFILE *fp);
IMPORT	W	rread(B *buf, W size, W count, RFILE *fp);
IMPORT	W	rwrite(B *buf, W size, W count, RFILE *fp);
IMPORT	W	rgetseg(B *buf, W size, RFILE *fp);
IMPORT	W	rputseg(B *buf, RFILE *fp);
IMPORT	W	runget(RFILE *fp);
IMPORT	W	rgets(B *byf, W size, RFILE *fp);
IMPORT	W	rputs(B *buf, RFILE *fp);
IMPORT	W	rputc(TC ch, RFILE *fp);

IMPORT	W	ropen_err; /* rcreat,ropen,rfdopen のエラーステータス */

#endif
