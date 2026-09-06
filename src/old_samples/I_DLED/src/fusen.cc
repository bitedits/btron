//
//	fusen.cc (偽仮身一覧/付箋管理系)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#include	<basic.h>
#include	<btron/btron.h>
#include	<btron/vobj.h>
#include	<btron/cnvend.h>

#include	"cval.h"		// macro.h で必要
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
	I_DLED_FUSEN	buf;
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
				rv = oget_fsn(exreq->vid, fd, &buf, sizeof(I_DLED_FUSEN));
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
			// 実身仮身マネージャの管理化から取得
			W	size;	// omgr 管理下の内容の大きさ(byte 単位)

			size = *ptr + sizeof(UH);
			if (size > sizeof(I_DLED_FUSEN)) {
				// 範囲を超えては取り出さない
				size = sizeof(I_DLED_FUSEN);
			}
			memcpy(&buf, ptr, size);
			break;
		default:		// cli 起動などのその他の場合
			// 特に何もしない
			break;
	}

	// 付箋の版の確認と付箋の読み込み(読めていなければ初期値のまま)
	if ((buf.dlen >= I_DLED_FUSEN_DLEN) &&
	    (ConvEndianH(buf.ver) == I_DLED_FUSEN_VERSION)) {
		ConvEndianStruct(&fsn, &buf, I_DLED_FUSEN_STRUCT, sizeof(I_DLED_FUSEN));
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
// eflg == true : oend_prc() を発行
//
W	FUSEN::write_fusen(const MESSAGE* msg, W vid, bool eflg)
{
	W	upd;
	W	vgid;

	upd = (memcmp(&fsn, &org, sizeof(I_DLED_FUSEN)) != 0) ? 1 : 0;
	vgid = 0;

	switch (msg->msg_type) {
		case EXECREQ:		// 仮身のオープン起動
			if (upd == 1) {
				// 付箋固有データの更新
				W	fd;
				const	M_EXECREQ*	exreq = reinterpret_cast<const M_EXECREQ*>(msg);

				fd = opn_fil((LINK*)&exreq->lnk,F_UPDATE,NULL);
				if (fd < ER_OK) {
					if (eflg) {
						// omgr管理下だけでも更新はする
						vgid = oend_prc(vid, &fsn,upd);
					}
					throw EXCEPT_FUSEN(fd);
				}
				fsn.dlen = I_DLED_FUSEN_DLEN;
				oput_fsn(vid, fd, &fsn);
				cls_fil(fd);
			}
		case FUSENREQ:		// 付箋のオープン起動
			if (eflg) {
				vgid = oend_prc(vid, &fsn, upd);
			}
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

	// 現在の状態を元々の内容として記憶
	org = fsn;

	return vgid;
}
