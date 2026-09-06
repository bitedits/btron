#
#	@(#)Makefile (sched)
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
CFLAGS+= -Wall
# ----------------------------------------------------------------------------

# 作成対象
TARGET = sched

# ソースファイルのサーチパス
S = ../src
VPATH = $(S)

# ヘッダファイルのディレクトリ追加
HEADER := $(S) $(HEADER)

# ソースファイル
SRC =	adr.C		\
	cal.c		\
	disp.C		\
	evtetc.c	\
	fileio.c	\
	lib.c		\
	load.c		\
	main.c		\
	menu.c		\
	save.c		\
	scdsmain.c	\
	scexec.c	\
	sch.c		\
	scnode.c	\
	scparts.c	\
	scprint.c	\
	sctray.c	\
	search.c

OBJ = $(addsuffix .o, $(basename $(SRC)))

WC_SRC = $(filter %.C, $(SRC))

# ----------------------------------------------------------------------------

.PHONY: all clean install

INST = $(addprefix $(TARGET), .bz .map)
ALL = $(addprefix $(TARGET), .out .dbx .fsn .form)

all: $(INST)

$(TARGET).out: $(OBJ)
	$(LINK.o) $(LDOBJS) $^ $(LOADLIBES) $(LDLIBS) -lbms $(OUTPUT_OPTION)

clean:
	$(RM) $(OBJ)
	$(RM) $(WC_SRC:%.C=%.c)
	$(RM) $(INST) $(ALL) *.lst txed.fsn $(DEPS)

install: $(INST:%=$(EXE_INSTALLDIR)/%)

# 原紙
FORM_NAME = 手帳用紙

$(TARGET).form: $(TARGET).fsn $(EXE_INSTALLDIR)/txed.fsn ../src/form.tad
	$(MKBTF) -o$@ -a0x001c -t$(FORM_NAME) \
		8.0.$(TARGET).fsn \
		8.0.$(EXE_INSTALLDIR)/txed.fsn \
		1.0.../src/form.tad

# 圧縮アーカイブ
$(TARGET).bz: $(ALL)
	$(CP) $(TARGET).out _t
	$(STRIP) _t
	$(MKBTF) -o$@ -a0x8003 -c -tSCHED \
		9.0x163._t \
		7.$(version).$(TARGET).fsn \
		10.0.$(TARGET).dbx \
		@0.0.0x8001.0.0x800c.$(TARGET).form
	$(RM) _t

# ソースの依存関係

$(WC_SRC:%.C=%.c):

ifdef DEPENDENCIES_OUTPUT
$(DEPS):	; touch $(DEPS)
else
$(DEPS): $(SRC)	; $(MAKEDEPS) $@ $?
endif

include $(DEPS)
