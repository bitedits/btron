//
//	imgope.cc (画像閲覧/表示画像管理系)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#include	<basic.h>
#include	<btron/btron.h>
#include	<btron/dp.h>
#include	<btron/libapp.h>
#include	<libimg.h>
#include	<bstdlib.h>

#include	"err.h"
#include	"except.h"
#include	"debug.h"

#include	"imgope.h"


// 内部構造体
typedef	struct {			// 読み込み情報(libimg 用)
	W	fd;			// 読み込み実身の FD
	W	rofs;			// 実身の record offset
} READINF;


// 内部関数プロトタイプ
LOCAL	WERR	grph_reafn(UB* bytes, UW reqsize, READINF* rinf) throw();


//
// 読み込み関数(libimg 用)
//
LOCAL	WERR	grph_reafn(UB* bytes, UW reqsize, READINF* rinf) throw()
{
	WERR	rv;

	rv = ER_OK;

	if (reqsize > 0) {
		W	size;

		rv = rea_rec(rinf->fd,rinf->rofs,(B*)bytes,reqsize,&size,NULL);
		if (rv >= ER_OK) {
			if (size > reqsize) {
				size = reqsize;
			}
			rinf->rofs += size;

			rv = size;
		}
	}

	return rv;
}


// ----------------------------------------- IMGOPE 内 static member 変数の実体
H	IMGOPE::opt_tbl[] = {		// IMG_COMPACT.opt の table
				LIBIMG_JPEG_OPT_DEFAULT,
				LIBIMG_PNG_OPT_DEFAULT,
				LIBIMG_BMP_OPT_DEFAULT
			};
H	IMGOPE::mthd_tbl[] = {		// IMG_COMPACT.method の table
				LIBIMG_METHOD_JPEG,
				LIBIMG_METHOD_PNG,
				LIBIMG_METHOD_BMP
			};


// ------------------------------------------------------ IMGOPE 内 public 関数
//
// constructor
//
IMGOPE::IMGOPE(const LINK* lnk)
	: sgid(-1), dgid(-1), zfact(-1)
{
	DPRINT(("IMGOPE constructor\n"));

	GetStdCSPEC(&sc_spec, SCREEN.planes, SCREEN.pixbits);
	sbmp.pixbits = SCREEN.pixbits;
	sbmp.baseaddr[0] = NULL;
	dbmp.baseaddr[0] = NULL;
	drect = (RECT){{0, 0, 0, 0}};
	vrect = (RECT){{0, 0, 0, 0}};

	// 対象画像の実身を開ける
	READINF	rinf;

	rinf.fd = opn_fil((LINK*)lnk, F_READ, NULL);
	if (rinf.fd < ER_OK) {
		throw EXCEPT_IMGOPE(rinf.fd);
	}

	// パラメータの初期化
	IMG_BMP	ibmp;
	IMG_COMPACT	icmp;

	ibmp.bmap = &sbmp;
	ibmp.funcptr = reinterpret_cast<FUNCP>(&grph_reafn);
	ibmp.io_src = reinterpret_cast<VOID*>(&rinf);
	ibmp.color_spec = &sc_spec;

	// 読み込み(retry 毎に形式を変えていく)
	W	l;
	ERR	er;

	for (l = 0, er = -1; (l < TYPE_MAX) && (er < ER_OK); ++l) {
		rinf.rofs = 0;
		icmp.opt = opt_tbl[l];
		icmp.method = mthd_tbl[l];

		er = libimg_rea_bmp(&ibmp, &icmp);
	}
	cls_fil(rinf.fd);
	if (er < ER_OK) {
		// 結局どれでも読み込めなかった
		throw EXCEPT_IMGOPE(er);
	}

	// どれかで読み込めたので残りの処理をする
	DPRINT(("type : %d\n", l - 1));
	sgid = gopn_mem(NULL, &sbmp, (B*)&sc_spec);
	if (sgid < ER_OK) {
		throw EXCEPT_IMGOPE(sgid);
	}
}


//
// destructor
//
IMGOPE::~IMGOPE()
{
	DPRINT(("IMGOPE destructor\n"));
	if (sgid >= 0) {
		gcls_env(sgid);
	}
	if (sbmp.baseaddr[0] != NULL) {
		free(sbmp.baseaddr[0]);
	}
	if (dgid >= 0) {
		gcls_env(dgid);
	}
	if (dbmp.baseaddr[0] != NULL) {
		free(dbmp.baseaddr[0]);
	}
}


//
// 画像の取得
//
//	gid   : 描画先の gid(< 0 の場合は画像の生成のみ)
//	vr    : 描画先の矩形枠
//	dr    : 再描画領域
//	vp    : 描画始点(原寸の左上)
//	zfact : 倍率(1000 = 100.0[%])
//
void	IMGOPE::disp_grph(W gid, RECT vr, RECT dr, PNT vp, W dzfact)
{
	if ((!(equalrect(vr, vrect))) ||
	    ((vp.x != drect.c.left) || (vp.y != drect.c.top)) ||
	    (dzfact != zfact) ||
	    (dbmp.baseaddr[0] == NULL)) {
		// 前の表示内容を廃棄する
		if (dgid >= 0) {
			gcls_env(dgid);
			dgid = -1;
		}
		if (dbmp.baseaddr[0] != NULL) {
			free(dbmp.baseaddr[0]);
			dbmp.baseaddr[0] = NULL;
		}

		// 画像の生成をする
		W	w;
		W	h;
		W	vw;
		W	vh;

		w = ((rectwidth(vr) * 1000) / dzfact) + 1;
		h = ((rectheight(vr) * 1000) / dzfact) + 1;
		vw = w * dzfact / 1000;
		vh = h * dzfact / 1000;

		drect.p.lefttop = vp;
		drect.c.right = drect.c.left + w;
		drect.c.bottom = drect.c.top + h;

		dbmp.planes = sbmp.planes;
		dbmp.pixbits = sbmp.pixbits;
		dbmp.rowbytes = ((((sbmp.pixbits >> 8) * vw) + 15) >> 4) << 1;
		dbmp.bounds = (RECT){{0, 0, vw, vh}};
		dbmp.baseaddr[0] = static_cast<UB*>(malloc(dbmp.rowbytes*vh));
		if (dbmp.baseaddr[0] == NULL) {
			throw EXCEPT_IMGOPE(ER_NOMEM);
		}

		dgid = gopn_mem(NULL, &dbmp, (B*)&sc_spec);
		if (dgid < ER_OK) {
			throw EXCEPT_IMGOPE(dgid);
		}

				// 本当は表示領域背景で塗りつぶした方がいい
		gfil_rec(dgid, dbmp.bounds, WHITE0, 0, G_STORE);
		gcop_bmp(sgid, &drect, dgid, &dbmp.bounds, NULL, G_STORE);
	}

	// 表示の諸情報を記憶する
	vrect = vr;
	zfact = dzfact;

	// 表示する
	if (gid >= 0) {	
		gcop_bmp(dgid, &dr, gid, &dr, NULL, G_STORE);
	}
	
	return;
}
