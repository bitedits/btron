//
//	vobjs.h (偽仮身一覧/仮身/付箋群管理系ヘッダ)
//
//	(C) Copyright 2002 by Personal Media Corporation.
//

#ifndef	_I_DLED_VOBJS_H
#define	_I_DLED_VOBJS_H

#include	<basic.h>
#include	<btron/btron.h>

#include	<vector>

#include	"vobjope.h"


// ---------------------------------------------------------------- class VOBJS
class	VOBJS {
	typedef	struct {		// oexe_vmn() 時の仮身領域情報群
		W	vid;		// 操作対象仮身 ID
		RECT	r;		// 仮身の矩形領域
	} OE_RLIST;
	typedef	struct {		// oexe_vmn() 時の続柄情報群
		W	vid;		// 操作対象仮身 ID
		W	idx;		// 続柄番号
	} OE_RIDX;
	typedef	struct {		// oexe_vmn() 時の新規実身情報群
		W	vid;		// 操作元仮身 ID
		W	nvid;		// 新規仮身 ID
	} OE_NOBJ;

	typedef	struct {		// 整頓時の整頓候補の情報
		W	vid;		// 対象仮身 ID
		const	VOBJ*	ptr;	// 対象仮身情報
		RECT	r;		// 仮身領域
	} TLIST;

public:
	VOBJS();			// constructor
	~VOBJS();			// destructor

	// 基本操作系
	// 登録先基本 ID の設定
	void	set_regid(W id) {rid = id;}

	// 仮身群の取得
	const	VOBJ*	get_vobj() {return vobj;}

	// 仮身群の先頭の差し替え
	void	set_vobj(VOBJ* ptr) {vobj = ptr;}

	// 仮身の追加
	void	add_vobj(const VLINK* lnk, const VP dat, W len);

	// 外部からの仮身を追加する
	void	ins_vobj(W nvid);

	// 全仮身の登録解除
	void	del_allvobj() throw();

	// 仮身 ID から仮身を探す
	const	VOBJ*	srch_vobj(W vid) throw();

	// 一時削除状態の仮身を抹消する
	void	dest_delvobj() throw();

	// 総数を取得する
	const	UW	get_cnt(bool del);

	// 取得系(vid を鍵に検索する)
	// 種別の取得
	const	bool	get_type(W vid) {return (const_cast<VOBJ*>(srch_vobj(vid)))->get_type();}

	// 固定化状態の取得
	const	bool	get_hold(W vid) {return (const_cast<VOBJ*>(srch_vobj(vid)))->get_hold();}

	// 背景化状態の取得
	const	bool	get_back(W vid) {return (const_cast<VOBJ*>(srch_vobj(vid)))->get_back();}

	// 一時削除状態の取得
	const	bool	get_del(W vid) {return (const_cast<VOBJ*>(srch_vobj(vid)))->get_del();}

	// 選択状態の取得
	const	bool	get_sel(W vid) {return (const_cast<VOBJ*>(srch_vobj(vid)))->get_sel();}

	// VLINK の取得
	const	VLINK*	vlnk(W vid) {return (const_cast<VOBJ*>(srch_vobj(vid)))->get_vlnk();}

	// segment 長さの取得
	const	W	get_seglen(W vid) {return (const_cast<VOBJ*>(srch_vobj(vid)))->get_seglen();}

	// segment の内容の取得(VOBJSEG 形式)
	const	VOBJSEG*	get_vobjseg(W vid) {return (const_cast<VOBJ*>(srch_vobj(vid)))->get_vobjseg();}

	// segment の内容の取得(FUSENSEG 形式)
	const	FUSENSEG*	get_fusenseg(W vid) {return (const_cast<VOBJ*>(srch_vobj(vid)))->get_fusenseg();}

	// 設定系(vid を鍵に検索する)
	// 固定化状態の設定
	void	set_hold(W vid, bool flg) {(const_cast<VOBJ*>(srch_vobj(vid)))->set_hold(flg);}
 
	// 背景化状態の設定
	void	set_back(W vid, bool flg) {(const_cast<VOBJ*>(srch_vobj(vid)))->set_back(flg);}
 
	// 一時削除状態の設定
	void	set_del(W vid, bool flg) {(const_cast<VOBJ*>(srch_vobj(vid)))->set_del(flg);}

	// 選択状態を設定する
	void	set_sel(W vid, bool flg) {(const_cast<VOBJ*>(srch_vobj(vid)))->set_sel(flg);}

	// VLINK の内容の差し替え(更新)
	void	set_vlnk(W vid, const VLINK* lnk) {(const_cast<VOBJ*>(srch_vobj(vid)))->set_vlnk(lnk);}

	// segment の内容の差し替え(更新)
	void	set_segdat(W vid, const VP dat, W len) {(const_cast<VOBJ*>(srch_vobj(vid)))->set_segdat(dat, len);}

	// 全仮身の固定化状態の設定
	void	set_allhold(bool flg, bool sel = false);

	// 全仮身の背景化状態の設定
	void	set_allback(bool flg, bool sel = false);

	// 全仮身の一時削除状態の設定
	void	set_alldel(bool flg, bool sel = false);

	// 全仮身の選択状態の設定
	void	set_allsel(bool flg, bool sel = false);

	// hmi/GUI 関連
	// 隠蔽仮身の表示状態の取得
	const	bool	get_hidden()	{return hidden;}

	// 隠蔽仮身の表示状態の切り替え
	void	set_hidden(bool flg) {hidden = flg;}

	// 仮身の全領域を取得する
	const	RECT	get_allarea();

	// PD 位置に対応する仮身を探す
	const	W	fnd_posvobj(PNT p);

	// 仮身の再表示(vid = -1 : すべて)
	void	dsp_vobj(W vid, const RECT* r);

	// 自動起動の実行
	void	do_autoexec();

	// 仮身の default application での実行
	void	exec_vobj(W vid) {(const_cast<VOBJ*>(srch_vobj(vid)))->exec_vobj();}

	// vs_menu.cc 内
	// 仮身操作系メニューの実行処理部
	void	mn_vobj(W par);

	// vs_vreq.cc 内
	// 仮身要求イベント処理部
	void	vobj_fn();

	// vs_tidy.cc 内
	// 自動配置の実行
	void	do_autotidy(const RECT vrect);

	// 整頓の実行
	void	do_selftidy();

	// vs_sel.cc 内
	// 選択中の仮身 ID を取得する
	const	W	get_selvid();

	// 選択中の仮身の個数を取得する
	const	W	get_selcnt();

	// 範囲内の仮身の選択状態の設定/取得
	bool	chg_rectsel(RECT r, bool sel, bool chk);

	// 選択中の仮身の変形
	void	rsz_selvobj(SIZE ds, W type);

	// 仮身の複製/移動処理
	void	copy_selvobj(bool copy, SIZE dl);

	// 仮身の順位移動
	void	renum_selvobj(bool top);

private:
	W	rid;			// 登録先基本 ID(WID か GID)
	bool	hidden;			// 隠蔽仮身の表示(true : 表示)
	VOBJ*	vobj;			// 仮身群(環)

	// vs_menu.cc 内
	// [仮身操作]-[開く]/[閉じる]/[属性変更] に対する挙動
	void	mn_vobj_attr(const OE_RLIST* lst);

	// [実身操作]-[実身複製] に対する挙動
	void	mn_vobj_new(const OE_NOBJ* lst);

	// [仮身操作]-[続柄変更] に対する挙動
	void	mn_vobj_chgrname(const OE_RIDX* lst);

	// [実身操作]-[実身名変更]/[ディスク操作]-[切り離し]/[フォーマット]
	// に対する挙動
	void	mn_vobj_disk(const W* lst);

	// 付箋起動 に対する挙動
	void	mn_vobj_exec(const W* lst);

	// vs_vreq.cc 内
	// 一時ファイル生成要求
	void	make_tmpfile();

	// vs_tidt.cc 内
	// 整頓対象の仮身群の生成
	void	gen_tidylist(std::vector<TLIST>& tlst);
};

#endif	// _I_DLED_VOBJS_H
