//
//	trayope.cc (画像閲覧/トレー操舵系)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#include	<basic.h>
#include	<btron/btron.h>
#include	<btron/dp.h>
#include	<btron/libapp.h>
#include	<bstring.h>
#include	<tad.h>
#include	<errcode.h>

#include	<memory>
#include	<vector>

#include	"dbox.h"

#include	"trayope.h"


// -------------------------------------- TRAYOPE 内 public 関数(static member)
//
// トレーへ複写
//
// 一時トレーを経由する方法は含まれていません
//
void	TRAYOPE::push_tray(const BMP* bmp, const CSPEC* cspec)
{
	// TS_FIG の部分の生成
	FIGSEG	head;

	head.view = bmp->bounds;
	head.draw = bmp->bounds;
	head.h_unit = -120;
	head.v_unit = -120;
	head.ratio = 0;

	// 容量の算出
	W	isize;
	W	psize;

	isize = bmp->rowbytes * rectheight(bmp->bounds);
	psize = (cspec->attr & DA_HAVECMAP) ? (sizeof(COLOR) * cspec->info[0]) : 0;

	// TS_IMAGE の部分の生成
	std::vector<B>	img(sizeof(IMAGESEG) + isize + psize);
	IMAGESEG	*iseg = reinterpret_cast<IMAGESEG*>(img.begin());

	iseg->view = head.view;
	iseg->draw = head.draw;
	iseg->h_unit = head.h_unit;
	iseg->v_unit = head.v_unit;
	iseg->slope = 0;
	iseg->color = cspec->attr & 0x000f;
	if (cspec->attr & DA_HAVECMAP) {
		// color map を持つ
		iseg->cinfo[0] = psize;
		iseg->cinfo[1] = 0;
		iseg->cinfo[2] = (sizeof(IMAGESEG) + isize) >> 16;
		iseg->cinfo[3] = (sizeof(IMAGESEG) + isize) & 0x0000ffff;
	} else {
		// color map を持たない
		W       l;

		for (l = 0; l < 4; ++l) {
			iseg->cinfo[l] = cspec->info[l];
		}
	}
	iseg->extlen = 0;
	iseg->extend = 0;
	iseg->mask = 0;
	iseg->compac = 0;
	iseg->planes = 1;
	iseg->pixbits = bmp->pixbits;
	iseg->rowbytes = bmp->rowbytes;
	iseg->bounds = bmp->bounds;
	iseg->base_off[0] = sizeof(IMAGESEG);

	// plane 情報の出力
	memcpy(&img[sizeof(IMAGESEG)], bmp->baseaddr[0], isize);

	// palette 情報の出力(無いこともある)
	if ((cspec->attr & DA_HAVECMAP) && (psize > 0)) {
		memcpy(&img[sizeof(IMAGESEG) + isize], cspec->colmap, psize);
	}

	// トレーへ複写
	WERR	rv;
	TRAYREC	trec[2];

	trec[0].id = TS_FIG;
	trec[0].len = sizeof(FIGSEG);
	trec[0].dt = reinterpret_cast<B*>(&head);
	trec[1].id = TS_IMAGE;
	trec[1].len = img.size();
	trec[1].dt = img.begin();
	rv = tpsh_dat(trec, 2, (TC*)getdbox(DBOX::TRAY_NAME));
	if (rv < ER_OK) {
		errpanel(DBOX::EPNL_PUSHTRAY, rv);
	}

	return;
}
