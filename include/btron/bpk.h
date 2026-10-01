/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	bpk.h
 *	BTRON archives (書庫, BPK), binary TAD and TRON code (design 17.16)
 *
 *	A BTRON archive is a binary TAD whose 指定付箋 (application ID
 *	8000 C003 8000) carries it: a global header of 30 bytes, then a
 *	stream, LH5-compressed or stored, that holds an extension (the
 *	root's virtual object), one local header of 96 bytes for each real
 *	object, and then each object's records in turn, each a record header
 *	(type, subtype, size) and its bytes. All of it is little endian.
 *
 *	lib/libbpk reads that: bpk_parse cuts an archive into its objects and
 *	records, lh5_decode inflates the stream, bpk_tad_to_xml turns a TAD
 *	record into xmlTAD, and the TRON calls turn TRON characters into
 *	Unicode and back. The layouts are all here, and the two headers can
 *	be written as well as read.
 *
 *	None of it makes a system call: lh5_decode uses nothing at all (the
 *	kernel's tests link it), the rest only the C library's malloc and
 *	printf family.
 */

#ifndef __TS_BPK_H__
#define __TS_BPK_H__

#include <tk/typedef.h>
#include <tk/errno.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================ TRON code */

#define TC_TAB		0x0009
#define TC_NL		0x000A
#define TC_FF		0x000C
#define TC_CR		0x000D
#define TC_LANG		0xFE00		/* FE21..FE7E, FE80..FEFE: to plane n (FE21 is plane 1) */
#define TC_SPEC		0xFF00		/* above it, a word starts a segment */
#define TC_GETA		0x222E		/* 〓 in plane 1 */
#define UC_GETA		0x3013		/* 〓 in Unicode, for a character with none */

/*
 * The Unicode of one TRON character in the plane *p_plane (1 at the
 * start). Plane 1 is the system script: in its A zone JIS X 0208 with
 * row 3's letters as ASCII (2320..237E) and JIS X 0213's first plane in
 * the gaps; in its B zone JIS X 0213's second plane (first byte 87..A0)
 * and JIS X 0212 (A1..ED); in its C zone GB 2312 and in its D zone KS X
 * 1001 (from B7), both 94 by 94 laid 126 to a first byte. Planes 16 and
 * 17 are the Unicode BMP in zone order. A plane switch changes *p_plane
 * and gives 0, as does a code that is no character; a character with no
 * Unicode (the GT fonts, 大漢和, a place no table fills) gives 〓.
 */
IMPORT UINT bpk_tron_ucs( UINT ch, INT *p_plane );

/*
 * The same, whole: the one or two Unicode characters (a letter and a
 * combining mark, for a few places of JIS X 0213) in out; answers how
 * many, 0 for a plane switch or no character, and BPK_TRON_NOUCS for a
 * character that has no Unicode, out[0] being 〓 then.
 */
#define BPK_TRON_NOUCS	(-1)
IMPORT INT  bpk_tron_chars( UINT ch, INT *p_plane, UINT out[2] );

/*
 * The other way: the TRON characters of one Unicode character, a plane
 * switch before them when *p_plane has to change. Answers how many were
 * written (at most 2), 0 when max is too small. Both the JIS forms and
 * the Windows code page's forms of the few characters where they part
 * (〜 301C and FF5E, − 2212 and FF0D, ‖ ¢ £ ¬) go to the same place.
 */
IMPORT INT  bpk_ucs_tron( UINT cp, UH *out, INT max, INT *p_plane );

/* A TRON string of at most n characters, to its first 0, as UTF-8 */
IMPORT void bpk_tron_utf8( const UH *s, INT n, char *out, INT max );

/* A UTF-8 string as TRON characters, at most max, the rest 0; how many */
IMPORT INT  bpk_utf8_tron( const char *s, UH *out, INT max );

IMPORT INT  bpk_utf8( UINT cp, char *out );		/* bytes written, at most 4 */

/*
 * Plane 1's A zone: row and cell from 1, at [(row - 1) * 94 + cell - 1];
 * 0 none, 0xD800 + n the character(s) of bpk_tron_multi[n]
 */
IMPORT const UH   bpk_jis_char[94 * 94];
/* Its characters of the BMP in Unicode order: (character << 16) | (row << 8) | cell */
IMPORT const UINT bpk_jis_rev[];
IMPORT const INT  bpk_jis_nrev;
/* Zone B (first byte 87..ED), C and D, as bpk_jis_char holds them */
IMPORT const UH   bpk_tron_zb[( 0xED - 0x87 + 1 ) * 94];
IMPORT const UH   bpk_tron_zc[94 * 94];
IMPORT const UH   bpk_tron_zd[94 * 94];
/* A character past the BMP (the second 0), or a letter and its mark */
IMPORT const UINT bpk_tron_multi[][2];
IMPORT const INT  bpk_tron_nmulti;
/* The way back for what zone A does not hold, in Unicode order: character, plane 1 code */
IMPORT const UINT bpk_tron_rev_cp[];
IMPORT const UH   bpk_tron_rev_code[];
IMPORT const INT  bpk_tron_nrev;
/* The letters with a mark that have a place of their own: letter, mark, code */
IMPORT const UINT bpk_tron_seq[][3];
IMPORT const INT  bpk_tron_nseq;

/* ================================================================ LH5 */

#define LH5_DICBIT	13		/* an 8 KB window */
#define LH5_MAXMATCH	256
#define LH5_THRESHOLD	3
#define LH5_NC		510		/* 256 literals and the lengths 3..256 */
#define LH5_NT		19		/* the code lengths of the code lengths */
#define LH5_NP		14		/* the distances, as their bit counts */
#define LH5_NPT		19

typedef struct {
	const UB	*in;
	UINT		inlen, inpos;
	UINT		over;		/* bytes wanted past the end of the input */
	UINT		bitbuf;		/* 16 bits, the next to come at the top */
	UINT		subbitbuf;
	INT		bitcount;
	UINT		blocksize;
	BOOL		bad;
	UH		left[2 * LH5_NC - 1];
	UH		right[2 * LH5_NC - 1];
	UH		c_table[4096];
	UH		pt_table[256];
	UB		c_len[LH5_NC];
	UB		pt_len[LH5_NPT];
} BPKLH5;

/*
 * The compressed bytes in[0..inlen) decoded into out[0..outlen), with z
 * the decoder's state. Answers the bytes made, outlen when the data was
 * whole, or -1 when it is not LH5 data at all.
 */
IMPORT INT lh5_decode( BPKLH5 *z, const UB *in, UINT inlen, UB *out, UINT outlen );

/* ================================================================ binary TAD */

/* Segment ids */
#define TS_INFO		0xFFE0		/* 管理情報 */
#define TS_TEXT		0xFFE1		/* 文章開始 */
#define TS_TEXTEND	0xFFE2
#define TS_FIG		0xFFE3		/* 図形開始 */
#define TS_FIGEND	0xFFE4
#define TS_IMAGE	0xFFE5		/* 画像 */
#define TS_VOBJ		0xFFE6		/* 仮身 */
#define TS_DFUSEN	0xFFE7		/* 指定付箋 */
#define TS_FFUSEN	0xFFE8		/* 機能付箋 */
#define TS_SFUSEN	0xFFE9		/* 設定付箋 */
#define TS_TPAGE	0xFFA0		/* 文章: ページ割付け */
#define TS_TRULER	0xFFA1		/* 行書式 */
#define TS_TFONT	0xFFA2		/* 文字指定 */
#define TS_TCHAR	0xFFA3		/* 特殊文字 */
#define TS_TATTR	0xFFA4		/* 文字割付け */
#define TS_TSTYLE	0xFFA5		/* 文字修飾 */
#define TS_TVAR		0xFFAD		/* 変数参照 */
#define TS_TMEMO	0xFFAE		/* 文章メモ */
#define TS_TAPPL	0xFFAF		/* 文章アプリケーション */
#define TS_FPRIM	0xFFB0		/* 図形要素 */
#define TS_FDEF		0xFFB1		/* データ定義 */
#define TS_FGRP		0xFFB2		/* グループ */
#define TS_FMAC		0xFFB3		/* マクロ */
#define TS_FATTR	0xFFB4		/* 図形修飾 */
#define TS_FPAGE	0xFFB5		/* 図形ページ割付け */
#define TS_FMEMO	0xFFBE		/* 図形メモ */
#define TS_FAPPL	0xFFBF		/* 図形アプリケーション */
#define TS_EXT		0xFFFE		/* an id past FFFF follows */
#define TS_LONG		0xFFFF		/* as a length: a 32-bit length follows */

/* A segment's sub id: the high byte of its first word */
#define TS_SUB(w)	( ( (w) >> 8 ) & 0xFF )

/* ---------------------------------------------------------------- the archive's layouts */

/* The 指定付箋 (data of TS_DFUSEN), byte offsets */
#define DF_VIEW		0		/* RECT */
#define DF_CHSZ		8
#define DF_FRCOL	10		/* COLORs */
#define DF_CHCOL	14
#define DF_TBCOL	18
#define DF_PICT		22
#define DF_APPL		24		/* UH[3] */
#define DF_NAME		30		/* TC[16] */
#define DF_DLEN		62		/* UW */
#define DF_DATA		66

#define BPK_APPL_HI	0x8000		/* the first and last words of an application ID */
#define BPK_APPL_ARCHIVE 0xC003		/* the middle word of the archive's */

/* The global header, at DF_DATA */
#define BPK_GLOBALHEAD	30
#define GH_HEADTYPE	0		/* B */
#define GH_CHECKSUM	1		/* B: the sum of the bytes from GH_VERSION on */
#define GH_VERSION	2		/* H */
#define GH_CRC		4		/* H */
#define GH_NFILES	6		/* H */
#define GH_COMPMETHOD	8		/* H: BPK_LH0 or BPK_LH5 */
#define GH_TIME		10		/* W */
#define GH_FILESIZE	14		/* W */
#define GH_ORIGSIZE	18		/* W: the stream inflated */
#define GH_COMPSIZE	22		/* W: the stream as stored */
#define GH_EXTSIZE	26		/* W: the extension at the stream's start */

#define BPK_LH0		0		/* stored */
#define BPK_LH5		5

/* A local header, one for each real object, after the extension */
#define BPK_LOCALHEAD	96
#define LH_F_TYPE	0		/* UH */
#define LH_F_ATYPE	2		/* UH */
#define LH_NAME		4		/* TC[20] */
#define LH_ORIGID	44		/* H */
#define LH_COMPMETHOD	46		/* H */
#define LH_ORIGSIZE	48		/* W */
#define LH_COMPSIZE	52		/* W */
#define LH_RESERVE	56		/* H[4] */
#define LH_F_NLINK	64		/* H */
#define LH_CRC		66		/* H */
#define LH_F_SIZE	68		/* W */
#define LH_OFFSET	72		/* W: where the object's records start in the stream */
#define LH_F_NREC	76		/* W */
#define LH_F_LTIME	80		/* W */
#define LH_F_ATIME	84
#define LH_F_MTIME	88
#define LH_F_CTIME	92

/* A record header, before each record's bytes */
#define BPK_RECHEAD	8
#define RH_TYPE		0		/* H */
#define RH_SUBTYPE	2		/* UH */
#define RH_SIZE		4		/* W */

/* Record types */
#define BPK_RT_LINK	0		/* a link record */
#define BPK_RT_TAD	1
#define BPK_RT_EXEC	8		/* 実行機能付箋 */

/* A link record: its target is the object of local header LK_F_ID */
#define BPK_LINKREC	52
#define LK_FS_NAME	0		/* TC[20] */
#define LK_F_ID		40		/* UH, from 0 */
#define LK_ATR		42		/* UH[5] */

/* An entry of a 実行機能付箋 record: 96 bytes, then its data */
#define BPK_EXECHEAD	0x60
#define EX_APPL		0x18		/* UH[3] */
#define EX_NAME		0x1E		/* TC[16] */
#define EX_TYPE		0x3E		/* TC[16] */
#define EX_DLEN		0x5E		/* UH */

/* ---------------------------------------------------------------- an archive read */

typedef struct {
	UINT	headtype, checksum, version, crc, nfiles, method;
	UINT	time, filesize, origsize, compsize, extsize;
} BPKGHEAD;

typedef struct {
	UINT	f_type, f_atype;
	UH	name[20];
	INT	origid, compmethod;
	UINT	origsize, compsize;
	UINT	f_nlink, crc, f_size, offset, f_nrec;
	UINT	f_ltime, f_atime, f_mtime, f_ctime;
} BPKLHEAD;

/* The two headers from and to their bytes; the checksum a global header should carry */
IMPORT void bpk_ghead_get( const UB *p, BPKGHEAD *h );
IMPORT void bpk_ghead_put( const BPKGHEAD *h, UB *p );
IMPORT UINT bpk_ghead_sum( const UB *p );
IMPORT void bpk_lhead_get( const UB *p, BPKLHEAD *h );
IMPORT void bpk_lhead_put( const BPKLHEAD *h, UB *p );

typedef struct {
	UH		type, sub;
	UINT		size;
	const UB	*data;		/* into the stream */
} BPKREC;

/* What a 実行機能付箋 said */
typedef struct {
	UH		appl[3];
	INT		wl, wt, wr, wb;		/* the window, when the data had it */
	BOOL		haswin;
	UINT		bgraw;			/* its background COLOR */
	BOOL		hasbg;
	INT		sv[50];			/* マイクロスクリプト's $SV */
	BOOL		hassv;
} BPKEXEC;

typedef struct {
	BPKLHEAD	head;
	char		name[128];		/* UTF-8 */
	BPKREC		*rec;			/* head.f_nrec of them */
	INT		nrec;
	INT		nlink;
	INT		*link;			/* each link record's target, a file index; -1 none */
	INT		nexec;
	BPKEXEC		*exec;
	INT		ntad;			/* TAD records */
} BPKFILE;

typedef struct {
	const UB	*raw;			/* the archive file, as given */
	UINT		rawlen;
	char		name[64];		/* the 指定付箋's name */
	BPKGHEAD	head;
	UINT		nfiles, method, origsize, compsize, extsize;
	UB		*plain;			/* the stream inflated */
	UINT		plainlen;
	BPKFILE		*file;
	INT		root;			/* the file the archive opens on */

	/*
	 * The root's virtual object as the extension before the local
	 * headers describes it: how a link to the root is to look where
	 * the archive is taken out. hasroot is FALSE when there was none.
	 */
	BOOL		hasroot;
	UINT		rootattr;		/* the link's attr: BPK_V_* */
	INT		rootview[4];		/* its box: left, top, right, bottom */
	UINT		rootchsz;		/* its CHSIZE, as written (bpk_chsize_pt) */
	UINT		rootcol[4];		/* frcol, chcol, tbcol, bgcol: COLOR as written */
} BPKARC;

/* What a link's attr leaves out of its frame, and whether it opens by itself */
#define BPK_V_NONAME	0x0001
#define BPK_V_NORELN	0x0002
#define BPK_V_NOTYPE	0x0004
#define BPK_V_NOTIME	0x0008
#define BPK_V_NOPICT	0x0040
#define BPK_V_NOFDISP	0x0080
#define BPK_V_AUTEXE	0x4000

/*
 * A CHSIZE in points, rounded: 1/20 point, 1/20 Q, or dots -- those of
 * the text or figure it belongs to taken as 120 to the inch, as they
 * are when nothing says otherwise.
 */
IMPORT INT  bpk_chsize_pt( UINT chsize );

/*
 * An archive file cut up: its headers read, the stream inflated, each
 * object's records found. E_OK, or an error and what was wrong (in
 * Japanese) in err. bpk_free lets go of what it took, either way.
 */
IMPORT ER   bpk_parse( BPKARC *a, const UB *raw, UINT rawlen, char *err, INT errmax );
IMPORT void bpk_free( BPKARC *a );

/* The program id (and name) a 実行機能付箋's application ID names, or NULL */
IMPORT const char *bpk_appl_prog( const UH *appl, const char **p_name );

/* ---------------------------------------------------------------- text that grows */

typedef struct {
	char	*s;
	INT	n, max;
	BOOL	fail;			/* an allocation failed: the text is short */
} BPKBUF;

IMPORT void bpk_buf_init( BPKBUF *b );
IMPORT void bpk_buf_free( BPKBUF *b );
IMPORT void bpk_buf_putn( BPKBUF *b, const char *s, INT n );
IMPORT void bpk_buf_puts( BPKBUF *b, const char *s );
IMPORT void bpk_buf_putc( BPKBUF *b, char c );
IMPORT void bpk_buf_printf( BPKBUF *b, const char *fmt, ... ) __attribute__((format(printf, 2, 3)));
IMPORT void bpk_buf_xml( BPKBUF *b, const char *s );	/* escaped for XML */
IMPORT void bpk_buf_json( BPKBUF *b, const char *s );	/* escaped for a JSON string */
IMPORT void bpk_buf_cp( BPKBUF *b, UINT cp );		/* one character, UTF-8 */
IMPORT void bpk_buf_num( BPKBUF *b, INT milli );	/* a number kept in thousandths */

/* ---------------------------------------------------------------- binary TAD to xmlTAD */

#define BPK_CMAP_MAX	256

/*
 * What the converter asks of whoever runs it. It knows nothing of where
 * the records come from: an object's virtual objects take its link
 * records in turn, the n-th TS_VOBJ of the object (counted over its TAD
 * records, from 0) the n-th link record, and link_target says what the
 * n-th link record's target became.
 */
typedef struct {
	/* the UUID (36 characters) of the n-th link record's target; NULL none */
	const char *(*link_target)( void *ctx, INT n );
	/* a new UUID for a virtual object */
	void (*new_vobjid)( void *ctx, char *out37 );
	/* the n-th picture of the record as PNG, to keep as "_<recno>_<n>.png" */
	ER (*picture)( void *ctx, INT recno, INT n, const UB *png, INT len );
	void	*ctx;
} BPKHOOK;

/* Segments the converter kept as <tadseg> for want of an element, counted by kind */
#define BPK_SKIP_KINDS	32
typedef struct {
	UINT	id;			/* segment id and sub id: 0xFFA2 << 8 | sub */
	UINT	count;
} BPKSKIP;

typedef struct {
	const BPKHOOK	*hook;
	INT		recno;			/* the record being converted */
	INT		linkno;			/* the object's link records used so far */
	INT		nimage;
	INT		plane;
	UINT		cmap[BPK_CMAP_MAX];
	INT		ncmap;
	BPKSKIP		skip[BPK_SKIP_KINDS];	/* counted over every record converted */
	INT		nskip;
	UINT		nskipped;
	ER		er;			/* the first error a hook gave */
} BPKCONV;

IMPORT void bpk_conv_init( BPKCONV *cv, const BPKHOOK *hook );

/* The records of another object follow: its link records are counted from 0 */
IMPORT void bpk_conv_object( BPKCONV *cv );

/*
 * A TAD record as xmlTAD into out: <tad filename="name"> with <document>
 * for a text, <figure> for a figure, the virtual objects as <link
 * id="target_0.xtad">, the pictures as <image href="self_recno_n.png">,
 * each given to hook->picture. self is the UUID of the object the
 * record is going to, recno the record it is going to be.
 *
 * The forms beyond the one-for-one elements (design 17.16):
 *   <paper-overlay-define N P>paragraphs</paper-overlay-define>, <docoverlay active="0, 1"/>
 *   <line-head-kinsoku kind="0xKL" ch="..."/>, <line-tail-kinsoku .../>
 *   <page-number num step/> for variable 200, <variable id="n"/> or <variable name="..."/>
 *   <font rotation rotabs/>, <font baseshift baseattr/>
 *   <lineTypeDefine id nb mask="hex"/> in a figure
 *   <calcPos cell/>, <calcCell .../> for 基本表計算's cells
 *   <tchar plane="9" code="8b21 8b22">〓〓</tchar>: characters with no Unicode
 *   <docappl appl="8000-0003-8000" data="hex"/>, <figappl .../>: application 付箋
 *   <figoverlay number even odd overlayData/>, <figoverlay active/>
 *   <tadseg id="ffe8" data="hex"/>: any other segment, its bytes as they were
 * A kept element too long for one attribute goes on in more, part="1", "2" ...
 */
IMPORT ER   bpk_tad_to_xml( BPKCONV *cv, const char *name, const char *self, INT recno,
			    const UB *rec, UINT len, BPKBUF *out );

/* The colour map a TAD record defines, as COLORs; how many */
IMPORT INT  bpk_tad_cmap( const UB *rec, UINT len, UINT *cmap, INT max );

/* Whether a TAD record is a figure (its first segment starts one) */
IMPORT BOOL bpk_tad_is_fig( const UB *rec, UINT len );

/*
 * Whether a TAD record is a text or a figure at all: a record of type 1
 * whose first segment starts neither (a program's registration data,
 * say) is better kept as it is than converted.
 */
IMPORT BOOL bpk_tad_is_doc( const UB *rec, UINT len );

/*
 * A COLOR as #rrggbb: bit 31 says transparent, bits 28-30 the mode; mode
 * 1 is RGB, mode 0 an index into cmap (COLORs themselves) and then the
 * system's standard 16.
 */
IMPORT void bpk_colour( UINT raw, const UINT *cmap, INT ncmap, char out[8] );

/* ---------------------------------------------------------------- PNG */

/* w by h pixels 0xAARRGGBB, alpha kept when alpha is TRUE; malloc'd, stored blocks */
IMPORT UB  *bpk_png_encode( const UINT *px, INT w, INT h, BOOL alpha, INT *p_len );

/* ---------------------------------------------------------------- compressed planes */

/* A picture segment's compac: how its planes are kept */
#define BPK_NOCOMPAC	0
#define BPK_MHCOMPAC	1		/* CCITT T.4, one-dimensional */
#define BPK_MR2COMPAC	2		/* T.4 two-dimensional, k = 2 */
#define BPK_MR4COMPAC	3		/* and k = 4 */

/*
 * A one-bit plane of w by h pixels compressed by CCITT T.4, MH or (mr)
 * MR, decoded into h rows of rowbytes, a set bit black, the first pixel
 * in the top bit. Answers the bytes of in used, or -1 when the codes go
 * wrong before the last row.
 */
IMPORT INT  bpk_fax_decode( const UB *in, UINT inlen, BOOL mr, INT w, INT h, UB *out, UINT rowbytes );

#ifdef __cplusplus
}
#endif
#endif /* __TS_BPK_H__ */
