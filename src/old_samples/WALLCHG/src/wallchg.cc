//
//	wallchg.cc (壁紙変更/壁紙変更処理主幹部)
//
//	(C) Copyright 2001 by Personal Media Corporation.
//

#include	<basic.h>
#include	<btron/btron.h>
#include	<btron/dp.h>
#include	<btron/libapp.h>
#include	<errcode.h>
#include	<bstdlib.h>
#include	<tstring.h>

#include	<new>
#include	<memory>
#include	<vector>

#include	"dbox.h"
#include	"err.h"
#include	"except.h"
#include	"debug.h"

#include	"wallchg.h"


// -------------------------------------- WALLCHG 内 public 関数(static member)
//
// 乱数の初期化
//
void	WALLCHG::init_rand()
{
	W	t;

	get_tim(&t, NULL);
	srand(t);

	return;
}


// ----------------------------------------------------- WALLCHG 内 public 関数
//
// constructor
//
WALLCHG::WALLCHG()
	: fd(-1)
{
	W	type;

	type = get_lnk((TC*)(getdbox(DBOX::BGS_PATH)), &blnk, F_NORM);
	if (type < ER_OK) {
		throw EXCEPT_WALLCHG(type);
	}

	// 排他な update で開く
	W	l;

	for (l = 0; (l < RETRY_MAX) && (fd < ER_OK); wai_prc(RETRY_WAIT),++l) {
		fd = opn_fil(&blnk, F_UPDATE | F_EXCL, NULL);
	}
	if (fd < ER_OK) {
		throw EXCEPT_WALLCHG(fd);
	}
	DPRINT(("open $$BGSCREEN.BOX\n"));
}


//
// destructor
//
WALLCHG::~WALLCHG()
{
	DPRINT(("destruct WALLCHG\n"));
	if (fd >= 0) {
		DPRINT(("close $$BGSCREEN.BOX\n"));
		cls_fil(fd);
	}
}


//
// 壁紙変更の実行
//
void	WALLCHG::exec()
{
	DPRINT(("wallpaper change start.\n"));

	read_info();
	bginfo.num = get_rand(bginfo.num, 1, bginfo.max);
	if (bginfo.num > 0) {
		try {
			set_bgpat();
		} catch (EXCEPT_WALLCHG& err) {
			DPRINT(("%s : %d\n", err.what(), err.get_err()));
		} catch (std::bad_alloc) {
			throw EXCEPT_WALLCHG(ER_NOMEM);
		} catch (...) {
			throw EXCEPT_WALLCHG(ER_SYS);
		}
	}
	write_info();

	return;
}


// ---------------------------------------------------- WALLCHG 内 private 関数
//
// 管理情報の読み込み
//
void	WALLCHG::read_info()
{

	bginfo.num = 0;
	bginfo.max = 0;
	tc_strset(&bginfo.name[0][0], TNULL, BG_MAX * L_FNM);

	see_rec(fd, 0, 1, NULL);
	DPRINT(("read information data.\n"));
	rea_rec(fd, 0, (B*)&bginfo, sizeof(BGINFO), NULL,NULL);

	return;
}


//
// 管理情報への書き込み
//
void	WALLCHG::write_info()
{
	W	size;

	size = (sizeof(H) * 2) + sizeof(L_FNM) * bginfo.max;
	see_rec(fd, 0, 1, NULL);
	DPRINT(("write information data.\n"));
	wri_rec(fd, 0, (B*)&bginfo, size, NULL, NULL, 0);

	return;
}


//
// 範囲内での乱数の発生
//
H	WALLCHG::get_rand(H now, H min, H max)
{
	H	num;

	if (max <= min) {
		num = max;
	} else {
		W	w;

		w = max - min;
		do {
			// RAND_MAX は bstdlib.h にある
			num = ((rand() * w + (RAND_MAX >> 1)) / RAND_MAX) +min;
		} while (num == now);
	}
	DPRINT(("set number : %d\n",num));

	return num;
}


//
// 番号に応じた背景画像の読み込み・設定の発行
//
void	WALLCHG::set_bgpat()
{
	// record の移動・容量の取得
	W	cnt;
	W	mode;
	W	rtype;
	W	bsize;			// 必要容量

	for (mode = F_TOPEND, cnt = -1, rtype = ER_OK;
	     (rtype >= ER_OK) && (cnt < bginfo.num);
	     mode = F_NFWD) {
		rtype = fnd_rec(fd, mode,
				BGDATA_RMASK | BGINFO_RMASK, 0x0000, NULL);
		if ((rtype == BGDATA_RTYPE) || (rtype == BGINFO_RTYPE)) {
			++cnt;
		}
	}
	if (rtype == BGINFO_RTYPE) {
		throw EXCEPT_WALLCHG(WALLERR::NOBGREC);
	}

	bsize = 0;
	rea_rec(fd, 0, NULL, 0, &bsize, NULL);
	DPRINT(("data size : %d\n", bsize));

	// 内容の読み込み
	W	l;
	W	idx;
	BMP*	bmp;
	CSPEC*	csp;
	std::vector<B>	data(bsize);	// 読み込んだ内容

	rea_rec(fd, 0, data.begin(), bsize, NULL, NULL);
	bmp = reinterpret_cast<BMP*>(data.begin());
	idx = sizeof(BMP) + sizeof(B*) * (bmp->planes - 1)
		+ bmp->rowbytes * rectheight(bmp->bounds) * bmp->planes;
	if (idx < bsize) {
		csp = reinterpret_cast<CSPEC*>(&data[idx]);
	} else {
		csp = NULL;
	}
	for (l = 0; l < static_cast<W>(bmp->planes); ++l) {
		bmp->baseaddr[l] += reinterpret_cast<W>(bmp);
	}

	// 変換の必要性を検証する
	bool	cnv;

	cnv = ((SCREEN.pixbits != bmp->pixbits) ||
	       (SCREEN.planes != static_cast<W>(bmp->planes)));
	if (csp != NULL) {
		// 形式は同じでも壁紙が color map 有りなら、必ず変換する
		if ((csp->attr & DA_HAVECMAP) == DA_HAVECMAP) {
			cnv = true;
			if (idx + static_cast<W>(sizeof(CSPEC)) + csp->info[0] <= bsize) {
				csp->colmap = reinterpret_cast<COLOR*>(&data[idx + sizeof(CSPEC)]);
			} else {
				csp = NULL;	// 足りなければ不定に落とす
			}
#ifdef	DEBUG
{
	if (csp != NULL) {
		W	l;
		COLOR*	cmap;

		cmap = csp->colmap;
		for (l = 0; l < 10; ++l) {
			printf("color map[%d] : %08x\n", l, cmap[l]);
		}
		printf("\n");
	}
}
#endif	// DEBUG
		}
	}

	// 変換する
	BMP	cbmp;
	std::vector<UB>	wbmp;		// plane の内容用

	if (cnv) {
		CSPEC	scsp;
		CSPEC	dmy;

		GetStdCSPEC(&scsp, SCREEN.planes, SCREEN.pixbits);
		cbmp.planes = SCREEN.planes;	// B-right/V では 1 しかない
		cbmp.pixbits = SCREEN.pixbits;
		cbmp.bounds = bmp->bounds;
		cbmp.rowbytes = ((((cbmp.pixbits >> 8) * rectwidth(cbmp.bounds)) + 15) >> 4) << 1;
		wbmp.resize(cbmp.rowbytes * rectheight(cbmp.bounds));
		cbmp.baseaddr[0] = wbmp.begin();
		if (csp == NULL) {
			// 不定の場合は適当な CSPEC で変換
			GetStdCSPEC(&dmy, bmp->planes, bmp->pixbits);
			csp = &dmy;
		}
		ConvColorBmp(&cbmp, &scsp, bmp, csp, NULL);
	}

	// wset_inf() の発行(背景として登録)
	PAT	pat;

	pat.mpat.kind = 1;
	pat.mpat.hsize = rectwidth(bmp->bounds);
	pat.mpat.vsize = rectheight(bmp->bounds);
	pat.mpat.mask = NULL;
	pat.mpat.bmap = (cnv) ? &cbmp : bmp;
#ifdef	DEBUG
	DPRINT(("wset_inf(pattern registrate) : %d\n", wset_inf(WI_WORKBACK, &pat, sizeof(PAT))));
#else	// DEBUG
	wset_inf(WI_WORKBACK, &pat, sizeof(PAT));
#endif	// DEBUG

	return;
}
