//
//	appl.cc (壁紙変更/application 基幹)
//
//	(C) Copyright 2001 by Personal Media Corporation.
//

#include	<basic.h>
#include	<btron/btron.h>
#include	<btron/vobj.h>
#include	<btron/libapp.h>
#include	<btron/cnvend.h>
#include	<errcode.h>

#include	<new>
#include	<memory>

#include	"val.h"
#include	"cval.h"
#include	"except.h"
#include	"dbox.h"
#include	"err.h"
#include	"debug.h"

#include	"appl.h"
#include	"fusen.h"
#include	"sendmsg.h"
#include	"wallchg.h"
#include	"maintsk.h"
#include	"subtsk.h"
#include	"setuppnl.h"


// ------------------------------------------------ WALLCHG_APPL 内 public 関数
//
// constructor
//
WALLCHG_APPL::WALLCHG_APPL(MESSAGE* msg)
	: endprc(true)
{
	// little endian を宣言
	bigEndian = False;

	// 基本的な初期化
	getscreen();
	initstdpnl();
	chg_wrk(NULL);

	// 自 application 情報の取得(小物起動以外も考えられる為)
	mydat.pid = prc_sts(0, NULL, NULL);
	prc_inf(0, PI_LINK, (LINK*)&mydat.lnk, sizeof(LINK));

	// databox を開ける
	if (opendatabox(&mydat.lnk, DBOX::WALLCHG_DTYP, DBOX::WALLCHG_DNUM, 1) < ER_OK) {
		throw EXCEPT_INITERR(WALLERR::DBOXOPEN);
	}

	// 起動方法の確認
	DPRINT(("execution : "));
	switch (msg->msg_type) {
		case EXECREQ:		// 仮身のオープン起動
			DPRINT(("EXECREQ\n"));
			mydat.msg.exreq = (M_EXECREQ*)msg;
			if (!(mydat.msg.exreq->mode & 0x0002)) {
				// 小物起動のみ認める
				oend_prc(mydat.msg.exreq->vid, NULL, 0);
				throw EXCEPT_EXECMSG(ER_NOSPT);
			}
			break;
		case FUSENREQ:		// 付箋のオープン起動
			DPRINT(("FUSENREQ\n"));
			mydat.msg.fsnreq = (M_FUSENREQ*)msg;
			break;
		case DISPREQ:		// 開いた仮身の表示起動
			DPRINT(("DISPREQ\n"));
			oend_req(((M_DISPREQ*)msg)->vid, 1);
			throw EXCEPT_EXECMSG(ER_NOSPT);
			break;
		case PASTEREQ:		// データ貼り込み起動
			DPRINT(("PASTEREQ\n"));
			oend_req(((M_PASTEREQ*)msg)->vid, 1);
			throw EXCEPT_EXECMSG(ER_NOSPT);
			break;
		case TADREQ:		// 開いた仮身の TAD データ作成起動
			DPRINT(("TADREQ\n"));
			oend_req(((M_TADREQ*)msg)->vid, 1);
			throw EXCEPT_EXECMSG(ER_NOSPT);
			break;
		default:		// その他(cli起動など)
			DPRINT(("other\n"));
			throw EXCEPT_EXECMSG(ER_NOSPT);
			break;
	}
}


//
// destructor
//
WALLCHG_APPL::~WALLCHG_APPL()
{
	closedbox();

	if (endprc) {
		if (mydat.msg.msg->msg_type == EXECREQ) {
			oend_prc(mydat.msg.exreq->vid, NULL, 0);
		} else {
			oend_prc(mydat.msg.fsnreq->vid, NULL, 0);
		}
	}
}


//
// application 主幹への入り口
//
ERR	WALLCHG_APPL::main()
{
	ERR	er;

	er = ER_OK;

	// 起動の通知(多重起動可能で window もないのでここで呼び出す)
	if (mydat.msg.msg->msg_type == EXECREQ) {
		osta_prc(mydat.msg.exreq->vid, -1);
	} else {
		osta_prc(mydat.msg.fsnreq->vid, -1);
	}

	// 前のものの確認
	if (chk_front()) {
		goto EXIT;
	}

	// 各管理下 class の生成
	try {
		WALLCHG::init_rand();
		fsn = std::auto_ptr<FUSEN>(new FUSEN(mydat.msg.msg));

		// 各 option に応じた処理
		bool	chg;		// 変更してもよいか
		bool	cont;		// 処理を続行するかどうか
		const	UW	opt = fsn->get_option();

		chg = true;
		cont = true;
		if (opt & CVAL::OPT_CHANGE) {
			// 起動時に壁紙変更が有効
			WALLCHG	wchg;

			wchg.exec();
			chg = false;
		}
		if (opt & CVAL::OPT_PANEL) {
			// パネルを開けることになっている
			SETUPPNL	pnl;

			cont = (pnl.exec() == 1);
		}
		if (cont) {
			if ((fsn->get_option() & CVAL::OPT_CHANGE) && (chg)) {
				// 設定で壁紙変更が有効になった
				WALLCHG	wchg;

				wchg.exec();
			}
			if ((fsn->get_min() > 0) || (fsn->get_sec() > 0)) {
				// 各 task の起動
				mtsk = std::auto_ptr<MAINTSK>(new MAINTSK);
				create_subtsk();

				// 待ちに入る
				mtsk->recv_msgloop();
			}
		}
	} catch (EXCEPT_FUSEN& err) {	// 付箋の読み込み時の障害
		er = err.get_err();
		DPRINT(("can't read fusen : %d(%d)\n", er, er >> 16));
		errpanel(DBOX::EPNL_FREAD, er);
		throw EXCEPT_INITERR(er);
	} catch (EXCEPT_MAINTSK& err) {	// 主幹部での障害
		er = err.get_err();
		DPRINT(("%s : %d(%d)\n", err.what(), er, er >> 16));
		if (er == WALLERR::NOREGFEP) {
			errpanel(DBOX::EPNL_REGFEP, er);
		} else {
			errpanel(DBOX::EPNL_MAINTSK, er);
		}
		throw EXCEPT_INITERR(er);
	} catch (EXCEPT_SUBTSK& err) {	// 時間待ち subtask での障害
		er = err.get_err();
		DPRINT(("%s : %d(%d)\n", err.what(), er, er >> 16));
		errpanel(DBOX::EPNL_SUBTSK, er);
		throw EXCEPT_INITERR(er);
	} catch (EXCEPT_WALLCHG& err) {	// 壁紙変更での障害
		er = err.get_err();
		DPRINT(("%s : %d(%d)\n", err.what(), er, er >> 16));
		errpanel(DBOX::EPNL_WALLCHG, er);
		throw EXCEPT_INITERR(er);
	} catch (EXCEPT_SETUP& err) {	// 設定パネルでの障害
		er = err.get_err();
		DPRINT(("%s : %d(%d)\n", err.what(), er, er >> 16));
		errpanel(DBOX::EPNL_SETUP, er);
		throw EXCEPT_INITERR(er);
	} catch (std::bad_alloc) {	// メモリ不足
		DPRINT(("memory allocation error.\n"));
		errpanel(DBOX::EPNL_MEMORY, ER_NOMEM);
		throw EXCEPT_INITERR(ER_NOMEM);
	} catch (...) {			// 不定
		throw;
	}

	if (er >= ER_OK) {
                // 付箋の更新
		try {
			fsn->write_fusen(mydat.msg.msg);
			endprc = false;
		} catch (EXCEPT_FUSEN& err) {
			DPRINT(("can't write fusen : %d\n",err.get_err()>>16));
			errpanel(DBOX::EPNL_FWRITE, err.get_err());
			er = err.get_err();
                }
	}

EXIT:
	DPRINT(("appl exit.\n"));
	return er;
}


//
// 時間待ち subtask の生成
//
void	WALLCHG_APPL::create_subtsk()
{
	UW	time;

	time = (fsn->get_min() * 60 + fsn->get_sec()) * 1000;
	stsk = std::auto_ptr<SUBTSK>(new SUBTSK(time));

	return;
}
 

//
// 時間待ち subtask の立ち上げ
//
void	WALLCHG_APPL::start_subtsk()
{
	stsk->wakeup_subtask();

	return;
}


// ----------------------------------------------- WALLCHG_APPL 内 private 関数
//
// 前のものの存在確認と設定要求
//
// 前のものがあった場合、true を返す
//
bool	WALLCHG_APPL::chk_front()
{
	bool	flg;
	TC*	name;

	flg = false;
	name = reinterpret_cast<TC*>(getdbox(DBOX::GLOBAL_NM));

	// 既に起動しているかを確認
	W	dat;

	if (get_nam(name, &dat) >= ER_OK) {
		if (prc_sts(dat, NULL, NULL) == dat) {
			// 起動していたら、前の分に要求をだす
			SENDMSG::send_mesg(dat, SENDMSG::CHGTYPE_2);
			DPRINT(("setup panel request(%d).\n", dat));
			flg = true;
		}
	}

	// 誰も居なかったのなら、自分の PID で上書き
	if (!(flg)) {
		cre_nam(name, mydat.pid, N_FORCE | DELEXIT);
		DPRINT(("set pid : %d\n", mydat.pid));
	}

	return flg;
}
