#
# BTRON 3.20 Conformance Verifier Makefile
#

CC ?= gcc
SDL_CFLAGS ?= $(shell sdl2-config --cflags 2>/dev/null || echo "-I/usr/include/SDL2")
SDL_LIBS ?= $(shell sdl2-config --libs 2>/dev/null || echo "-lSDL2")
CFLAGS ?= -O2 -Wall -Wextra -std=c99 -I../include -I../include/drivers -I../src/kernel -D_GNU_SOURCE $(SDL_CFLAGS)
LDFLAGS ?= -lpthread -lm $(SDL_LIBS)

VERIFY_SRCS = btron_verify.c \
              report/results.c \
              suite/v_types.c \
              suite/v_errors.c \
              suite/v_proc.c \
              suite/v_itron.c \
              suite/v_memory.c \
              suite/v_clk.c \
              suite/v_device.c \
              suite/v_fs.c \
              suite/v_sys.c \
              suite/v_message.c \
              suite/v_vobj.c \
              suite/v_dp.c \
              suite/v_wnd.c \
              suite/v_hmi.c \
              suite/v_font_mgr.c \
              suite/v_tcpip.c \
              suite/v_omgr.c \
              ../src/cores/core_posix.c \
              ../src/kernel/ipc_msg.c \
              ../src/kernel/proc_mgr.c \
              ../src/kernel/mem_mgr.c \
              ../src/kernel/clk_mgr.c \
              ../src/kernel/dev_mgr.c \
              ../src/kernel/sys_mgmt.c \
              ../src/kernel/tcpip.c \
              ../src/window/event.c \
              ../src/graphics/dp_core.c \
              ../src/window/wnd.c \
              ../src/vobject/vobj.c \
              ../src/vobject/omgr.c \
              ../src/hmi/hmi_core.c \
              ../src/hmi/hmi_switch.c \
              ../src/hmi/hmi_selector.c \
              ../src/hmi/hmi_volume.c \
              ../src/hmi/hmi_meter.c \
              ../src/hmi/hmi_controller.c \
              ../src/hmi/hmi_panel.c \
              ../src/font/troncode.c \
              ../src/font/jis_fonts.c \
              ../src/font/tibetan_fonts.c \
              ../src/font/font_mgr.c \
              ../src/fs/blk_mem.c \
              ../src/fs/blk_file.c \
              ../src/fs/blk_qcow2.c \
              ../src/fs/blk_part.c \
              ../src/fs/vol.c \
              ../src/kernel/fs_record.c

VERIFY_OBJS = $(VERIFY_SRCS:.c=.o)
VERIFY_BIN = btron_verify

.PHONY: all clean run

all: $(VERIFY_BIN)

$(VERIFY_BIN): $(VERIFY_OBJS)
	$(CC) $(VERIFY_OBJS) -o $@ $(LDFLAGS)

%.o: %.c btron_verify.h ../include/btron/*.h
	$(CC) $(CFLAGS) -c $< -o $@

run: $(VERIFY_BIN)
	@./$(VERIFY_BIN) || true

clean:
	rm -f $(VERIFY_BIN) $(VERIFY_OBJS)
	rm -f *.o suite/*.o report/*.o ../src/font/*.o ../src/kernel/*.o ../src/vobject/*.o ../src/window/*.o ../src/graphics/*.o ../src/hmi/*.o ../src/fs/*.o ../src/cores/*.o
