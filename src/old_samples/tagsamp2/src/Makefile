#
#	@(#)Makefile (TAGSAMP2)
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
version = 0x2000

# リリース用をデフォルトにする
mode =

# ソースの依存関係ファイル (自動生成)
DEPS = Dependencies
DEPENDENCIES_OUTPUT := $(DEPS)

# アプリケーション標準ルール
include ../../etc/makerules

# ----------------------------------------------------------------------------
# 作成対象
TARGET = tagsamp2

# ソースファイルのサーチパス
S = ./ ../src
VPATH = $(S)

# ヘッダファイルのディレクトリ追加
HEADER := $(S) $(HEADER)

DATABOX_HEADER := $(S)

# ソースファイル
SRC	=	tsmain.c	\
		tsexmain.c	\
		tsact.c		\
		tsdisp.c	\
		tspage11.c	\
		tspage12.c	\
		tspage21.c	\
		tspage31.c

OBJ = $(addsuffix .o, $(basename $(SRC)))

WC_SRC = $(filter %.C, $(SRC))

ifneq ($(mode), debug)
  CFLAGS += -Wall
 endif

# ----------------------------------------------------------------------------
.PHONY: all clean install

INST = $(addprefix $(TARGET), .bz .map)
INST2 = $(addprefix $(TARGET), .out .fsn .dbx)

all: $(INST)

$(TARGET).out: $(OBJ)
	$(LINK.o) $(LDOBJS) $^ $(LOADLIBES) $(LDLIBS) -lker $(OUTPUT_OPTION)

clean:
	$(RM) $(OBJ)
	$(RM) $(WC_SRC:%.C=%.c) $(DBSRC)
	$(RM) $(INST) $(INST2) *.lst $(DEPS)

install: $(INST:%=$(EXE_INSTALLDIR)/%)	\
	 $(INST2:%=$(EXE_INSTALLDIR)/%)

$(EXE_INSTALLDIR)/$(TARGET).out: $(EXE_INSTALLDIR)/%: %
	$(EXE_INSTALL_STRIP)

# 圧縮アーカイブ
$(TARGET).bz: $(INST2)
	$(CP) $(TARGET).out _t
	$(STRIP) _t
	$(MKBTF) -o$@ -a0x8003 -f0x40 -c -tTAGSAMP2 \
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
