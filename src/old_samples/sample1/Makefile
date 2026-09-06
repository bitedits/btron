#
#	@(#)Makefile (sample)
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
mode =

# ソースの依存関係ファイル (自動生成)
DEPS = Dependencies
DEPENDENCIES_OUTPUT := $(DEPS)

# アプリケーション標準ルール
include ../../etc/makerules

#----------------------------------------------------------------------
# 作成対象
TARGET = sample

# ソースファイルのサーチパス
S = ../src
VPATH = $(S)

# ヘッダファイルのディレクトリ追加
HEADER := $(S) $(HEADER)

# ソースファイル
SRC =	main.c	\
	exec.c	\
	disp.c	\
	act.c	\
	menu.c	\
	press.c

# データボックスソースファイル
DBSRC = dbox.c

SRC += $(DBSRC)

WC_SRC = $(filter %.C, $(SRC))

# オブジェクトファイル
OBJ = $(addsuffix .o, $(basename $(SRC)))

# コンパイルオプション
ifneq ($(mode), debug)
  CFLAGS += -Wall
endif

#----------------------------------------------------------------------
.PHONY: all clean install

INST = $(addprefix $(TARGET), .bz .map)
NOINST = $(addprefix $(TARGET), .out .fsn)

all: $(INST)

$(TARGET).out: $(OBJ)
	$(LINK.o) $(LDOBJS) $^ $(LOADLIBES) $(LDLIBS) $(OUTPUT_OPTION)

clean:
	$(RM) $(OBJ)
	$(RM) $(WC_SRC:%.C=%.c) $(DBSRC)
	$(RM) $(INST) $(NOINST) *.lst $(DEPS)

install: $(INST:%=$(EXE_INSTALLDIR)/%)

$(EXE_INSTALLDIR)/$(TARGET).out: $(EXE_INSTALLDIR)/%: %
	$(EXE_INSTALL_STRIP)

# 圧縮アーカイブ
# -f0x40 は、小物登録用にアプリケーション属性 1 を設定する指定
#
$(TARGET).bz: $(NOINST)
	$(CP) $(TARGET).out _t
	$(STRIP) _t
	$(MKBTF) -o$@ -a0x8003 -c -tSAMPLE -f0x40 \
		9.0x163._t \
		7.$(version).$(TARGET).fsn
	$(RM) _t

# ソースの依存関係

$(WC_SRC:%.C=%.c):

$(DBSRC):

ifdef DEPENDENCIES_OUTPUT
$(DEPS):	; touch $(DEPS)
else
$(DEPS): $(SRC)	; $(MAKEDEPS) $@ $?
endif

include $(DEPS)
