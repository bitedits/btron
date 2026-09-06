//
//	guiope.h (簡易文字列編集/GUI 操舵系基幹部ヘッダ)
//
//	(C) Copyright 2002 by Personal Media Corporatoin.
//

#ifndef	_T_TXEDIT_GUIOPE_H
#define	_T_TXEDIT_GUIOPE_H

#include	<basic.h>
#include	<btron/btron.h>

#include	<memory>


// 参照 class の宣言
class	EVTOPE;
class	MAINWIN;
class	MAINMENU;


// --------------------------------------------------------------- class GUIOPE
class	GUIOPE {
public:
	GUIOPE();			// constructor
	~GUIOPE();			// destructor

	std::auto_ptr<EVTOPE>	evtope;	// イベント操舵系主幹
	std::auto_ptr<MAINWIN>	mwin;	// 主ウィンドウ管理系
	std::auto_ptr<MAINMENU>	menu;	// メインメニュー管理系

	// 各種管理化 class の生成
	void	init();

	// 各管理化 class の資源の廃棄を通知
	void	dest();

private:
};

#endif	// _T_TXEDIT_GUIOPE_H
