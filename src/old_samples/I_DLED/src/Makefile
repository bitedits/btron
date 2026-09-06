#
#	@(#)Makefile (I_DLED)
#		GNU make 用
#
#	make の方法
#	・make
#		リリース用の正式版を作成
#	・make mode=debug
#		デバッグ用に作成
#	・make clean
#		make で作成した全てのファイルを削除
#	・make install
#		規定の場所にインストール (実機用のみ)
#

# バージョン
version = 0x1000

# リリース用をデフォルトにする
#mode =

# マルチタスク対応ライブラリを使用
#USE_MT = 1

# C++ アプリケーションライブラリを使用
USE_CPP = 1

# ソースの依存関係ファイル (自動生成)
DEPS = Dependencies
DEPENDENCIES_OUTPUT := $(DEPS)

# アプリケーション標準ルール
include ../../etc/makerules
#include $(BD)/appl/etc/makerules

# ターゲット
ifeq ($(MACHINE), sun4)
  btron = no
else
  btron = yes
endif

# ----------------------------------------------------------------------------
# 作成対象
TARGET = i_dled

# ソースファイルのサーチパス
S = ./ ../src
VPATH = $(S)

# ヘッダファイルのディレクトリ追加
HEADER := $(S) $(HEADER)

DATABOX_HEADER := $(S)

# ソースファイル
SRC =	main.cc		\
	appl.cc		\
	appl_ex.cc	\
	appl_dsp.cc	\
	appl_pst.cc	\
	editobj.cc	\
	vobjope.cc	\
	vobjs.cc	\
	vs_menu.cc	\
	vs_vreq.cc	\
	vs_tidy.cc	\
	vs_sel.cc	\
	opndsp.cc	\
	fusen.cc	\
	guiope.cc	\
	evtope.cc	\
	mainwin.cc	\
	pd_ope.cc	\
	pd_nprs.cc	\
	pd_hprs.cc	\
	pd_vprs.cc	\
	pd_vqp.cc	\
	pd_clk.cc	\
	pd_dclk.cc	\
	kb_can.cc	\
	kb_del.cc	\
	kb_move.cc	\
	kb_scrl.cc	\
	selfrm.cc	\
	mainmenu.cc	\
	ud_ope.cc	\
	ud_move.cc	\
	ud_add.cc	\
	ud_del.cc	\
	ud_tadd.cc	\
	ud_rsz.cc	\
	ud_hold.cc	\
	ud_uhold.cc	\
	ud_back.cc	\
	ud_uback.cc	\
	ud_renum.cc	\
	trayope.cc	\
	tadload.cc	\
	saveope.cc	\
	tadsave.cc	\
	tidypnl.cc	\
	misc.cc

ALL = $(INST)
EXT = .out
CFLAGS += $(CFLAGS_WARING) -Wno-uninitialized
DATABOX_HEADER += $(BD)/include
LOADLIBES += -lstlport

OBJ = $(addsuffix .o, $(basename $(SRC)))

WC_SRC = $(filter %.C, $(SRC))

#ifneq ($(mode), debug)
#  CFLAGS += -W -Wall
#endif

ifeq ($(MACHINE), sun4)
  ifeq ($(mode), debug)
    CFLAGS += -Wno-unused
  endif
  LDLIBS = -lg -llang -ldbg
  LDOBJS =
endif

# ----------------------------------------------------------------------------
.PHONY: all clean install

INST = $(addprefix $(TARGET), .bz .map)
INST2 = $(addprefix $(TARGET), $(EXT) .dbx .form) $(TARGET).fsn

all: $(ALL)

$(TARGET)$(EXT): $(OBJ)
	$(LINK.o) $(LDOBJS) $^ $(LOADLIBES) $(LDLIBS) -lapp -lg $(OUTPUT_OPTION)

clean:
	$(RM) $(OBJ)
	$(RM) $(WC_SRC:%.C=%.c) $(DBSRC)
	$(RM) $(INST) $(INST2) *.lst $(DEPS) template.fsn

install: $(INST:%=$(EXE_INSTALLDIR)/%)

$(EXE_INSTALLDIR)/$(TARGET).out: $(EXE_INSTALLDIR)/%: %
	$(EXE_INSTALL_STRIP)

# 圧縮アーカイブ
$(TARGET).bz: $(INST2)
	$(CP) $(TARGET).out _t
	$(STRIP) _t
	$(MKBTF) -o$@ -a0x8003 -c -tI_DLED \
		9.0x163._t \
		10.0.$(TARGET).dbx \
		7.$(version).$(TARGET).fsn \
		@0.0.0x8001.0.0x800c.$(TARGET).form
	$(RM) _t

# ソースの依存関係

$(WC_SRC:%.C=%.c):

$(TARGET).dbx: $(TARGET).d
$(TARGET).fsn: $(TARGET).f

$(TARGET).form: $(TARGET).fsn
	$(MKBTF) -o$@ -a6 -t偽キャビネット \
		8.0.$(TARGET).fsn

ifdef DEPENDENCIES_OUTPUT
$(DEPS):	; touch $(DEPS)
else
$(DEPS): $(SRC)	; $(MAKEDEPS) $@ $?
endif

include $(DEPS)
