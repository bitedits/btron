//
//	fusen.cc (画像閲覧/付箋管理系)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#include	<basic.h>
#include	<btron/btron.h>
#include	<btron/vobj.h>
#include	<btron/cnvend.h>

#include	"struct.h"
#include	"err.h"
#include	"except.h"
#include	"macro.h"

#include	"fusen.h"


// ------------------------------------------------------- FUSEN 内 public 関数
//
// constructor
//
FUSEN::FUSEN(const MESSAGE* msg)
{
	UH*	ptr;			// omgr 管理化の内容の pointer
	IMGVIEW_FUSEN	buf;
	const	M_EXECREQ*	exreq =reinterpret_cast<const M_EXECREQ*>(msg);

	fsn = DEF_FUSEN;

	switch (msg->msg_type) {
		case EXECREQ:		// 仮身のオープン起動
			if ((exreq->mode & 0x03) != 0x00) {
				// 付箋固有データの読み込み
				W	fd;
				WERR	rv;

				fd = opn_fil((LINK*)&exreq->lnk, F_READ, NULL);
				if (fd < ER_OK) {
					throw EXCEPT_FUSEN(fd);
				}
				rv = oget_fsn(exreq->vid, fd, &buf, sizeof(IMGVIEW_FUSEN));
				cls_fil(fd);
				if (rv < ER_OK) {
					throw EXCEPT_FUSEN(rv);
				}
				break;
			}
		case DISPREQ:		// 開いた仮身の表示起動
		case TADREQ:		// 開いた仮身の TAD データ生成起動
		case PASTEREQ:		// データ貼り込み起動
			ptr = reinterpret_cast<UH*>(exreq->info);
			goto GET_DATA;
		case FUSENREQ:		// 付箋のオープン起動
			ptr = reinterpret_cast<UH*>((reinterpret_cast<const M_FUSENREQ*>(msg))->info);
GET_DATA:
			// 実身仮身マネージャの管理下から取得
			W	size;	// omgr 管理下の内容の大きさ(byte 単位)

			size = *ptr + sizeof(UH);
			if (size > sizeof(IMGVIEW_FUSEN)) {
				// 範囲を超えては取り出さない
				size = sizeof(IMGVIEW_FUSEN);
			}
			memcpy(&buf, ptr, size);
			break;
		default:		// cli 起動などのその他の場合
			// 特に何もしない
			break;
	}

	// 付箋の版の確認と付箋の読み込み(読めていなければ初期値のまま)
	if ((buf.dlen >= IMGVIEW_FUSEN_DLEN) &&
	    (ConvEndianH(buf.ver) == IMGVIEW_FUSEN_VERSION)) {
		ConvEndianStruct(&fsn, &buf, IMGVIEW_FUSEN_STRUCT, sizeof(IMGVIEW_FUSEN));
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
// 付箋への書き込み
//
W	FUSEN::write_fusen(const MESSAGE* msg, W vid)
{
	W	upd;
	W	vgid;

	upd = (memcmp(&fsn, &org, sizeof(IMGVIEW_FUSEN)) != 0) ? 1 : 0;
	vgid = 0;

	switch (msg->msg_type) {
		case EXECREQ:		// 仮身のオープン起動
			if (upd == 1) {
				// 付箋の更新
				W	fd;
				const	M_EXECREQ*	exreq = reinterpret_cast<const M_EXECREQ*>(msg);

				fd = opn_fil((LINK*)&exreq->lnk,F_UPDATE,NULL);
				if (fd < ER_OK) {
					throw EXCEPT_FUSEN(fd);
				}
				fsn.dlen = IMGVIEW_FUSEN_DLEN;
				oput_fsn(vid, fd, &fsn);
				cls_fil(fd);
			}
		case FUSENREQ:		// 付箋のオープン起動
			vgid = oend_prc(vid, &fsn, upd);
			break;
		case DISPREQ:		// 開いた仮身の表示起動
		case TADREQ:		// 開いた仮身の TAD データ生成起動
		case PASTEREQ:		// データ貼り込み起動
			// 特に何もしない
			break;
		default:		// cli 起動などのその他の場合
			// 特に何もしない
			break;
	}

	return vgid;
}
