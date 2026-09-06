//
//	fusen.cc (壁紙変更/付箋管理系)
//
//	(C) Copyright 2001 by Personal Media Corporation.
//

#include	<basic.h>
#include	<btron/btron.h>
#include	<btron/vobj.h>
#include	<btron/cnvend.h>
#include	<bstdlib.h>

#include	"cval.h"
#include	"struct.h"
#include	"err.h"
#include	"except.h"
#include	"macro.h"

#include	"fusen.h"


// ------------------------------------------------------- FUSEN 内 public 関数
//
// constructor
//
FUSEN::FUSEN(MESSAGE* msg)
{
	WALLCHG_FUSEN	buf;

	fsn = DEF_FUSEN;

	if (msg->msg_type == EXECREQ) {
		// 仮身のオープン起動時の取り出し
		M_EXECREQ*	exreq;

		exreq = reinterpret_cast<M_EXECREQ*>(msg);
		if ((exreq->mode & 0x03) != 0x00) {
			// 付箋固有データの読み込み
			W	fd;
			WERR	rv;

			fd = opn_fil(&exreq->lnk, F_READ, NULL);
			if (fd < ER_OK) {
				throw EXCEPT_FUSEN(fd);
			}
			rv = oget_fsn(exreq->vid, fd,
						&buf, sizeof(WALLCHG_FUSEN));
			cls_fil(fd);
			if (rv < ER_OK) {
				throw EXCEPT_FUSEN(rv);
			}
		} else {
			memcpy(&buf, (VP)exreq->info, sizeof(WALLCHG_FUSEN));
		}
	} else {
		// 付箋のオープン起動時の取り出し
		memcpy(&buf, (VP)((M_FUSENREQ*)msg)->info,
							sizeof(WALLCHG_FUSEN));
	}

	// 付箋の版の確認と付箋の読み込み(読めていなければ初期値のまま)
	if ((buf.dlen >= WALLCHG_FUSEN_DLEN) &&
	    (ConvEndianH(buf.ver) == WALLCHG_FUSEN_VERSION)) {
		ConvEndianStruct(&fsn, &buf,
				WALLCHG_FUSEN_STRUCT, sizeof(WALLCHG_FUSEN));
	}

	// 元々の内容を記憶しておく
	org = fsn;
}


//
// destructor
//
FUSEN::~FUSEN()
{
}


//
// 付箋固有データを書き込む(保存する)
//
void	FUSEN::write_fusen(MESSAGE* msg)
{
	W	upd;
	W	vid;

	upd = (memcmp(&fsn, &org, sizeof(WALLCHG_FUSEN)) != 0) ? 1 : 0;
	if (msg->msg_type == EXECREQ) {
		// 仮身のオープン起動
		M_EXECREQ*	exreq;

		exreq = reinterpret_cast<M_EXECREQ*>(msg);
		vid = exreq->vid;
		if (upd == 1) {
			// 起動元仮身/付箋の更新・omgr 管理部分の更新・終了通知
			W	fd;

			fd = opn_fil(&exreq->lnk, F_UPDATE, NULL);
			if (fd < ER_OK) {
				throw EXCEPT_FUSEN(fd);
			}
			fsn.dlen = WALLCHG_FUSEN_DLEN;
			oput_fsn(vid, fd, &fsn);
			cls_fil(fd);
		}
	} else {
		// 付箋のオープン起動
		vid = ((M_FUSENREQ*)msg)->vid;
	}
	oend_prc(vid, &fsn, upd);

	return;
}
