#
#	@(#)Makefile (IMGVIEW)
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
TARGET = imgview

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
	appl_tad.cc	\
	opndsp.cc	\
	imgope.cc	\
	fusen.cc	\
	guiope.cc	\
	evtope.cc	\
	mainwin.cc	\
	mainmenu.cc	\
	savedat.cc	\
	trayope.cc	\
	misc.cc

ALL = $(INST)
EXT = .out
CFLAGS += $(CFLAGS_WARING) -Wno-uninitialized
DATABOX_HEADER += $(BD)/include
LOADLIBES += -lstlport -limg

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
INST2 = $(addprefix $(TARGET), $(EXT) .dbx) $(TARGET).fsn

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
	$(MKBTF) -o$@ -a0x8003 -c -tIMGVIEW \
		9.0x163._t \
		10.0.$(TARGET).dbx \
		7.$(version).$(TARGET).fsn
	$(RM) _t

# ソースの依存関係

$(WC_SRC:%.C=%.c):

$(TARGET).dbx: $(TARGET).d
$(TARGET).fsn: $(TARGET).f

ifdef DEPENDENCIES_OUTPUT
$(DEPS):	; touch $(DEPS)
else
$(DEPS): $(SRC)	; $(MAKEDEPS) $@ $?
endif

include $(DEPS)
