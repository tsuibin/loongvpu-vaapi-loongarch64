# Makefile for the open-source LoongVPU VA-API driver scaffold.
#
# Build:   make
# Install: make install PREFIX=/usr/local   (installs into libva's driver dir)
# Clean:   make clean

CC      ?= gcc
CFLAGS  += -O2 -Wall -Wextra -fPIC
CFLAGS  += $(shell pkg-config --cflags libva)
LDFLAGS += -shared

TARGET  := loongvpu_drv_video.so
SRCS    := src/loongvpu_drv_video.c src/vpu_buffer.c src/vcmd.c src/work_buffer.c src/hevc_work_buffer.c
OBJS    := $(SRCS:.c=.o)
HDRS    := src/loongvpu_abi.h src/vpu_buffer.h src/vcmd.h src/work_buffer.h src/h264_decode_params.h src/h264_regs.h src/hevc_decode_params.h src/hevc_regs.h src/hevc_work_buffer.h

# libva on this system looks for drivers under /usr/lib/dri
DRIVERDIR ?= /usr/lib/dri
PREFIX    ?= /usr/local

.PHONY: all install clean test

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(shell pkg-config --libs libva)

%.o: %.c $(HDRS)
	$(CC) $(CFLAGS) -c -o $@ $<

# Minimal smoke test: load the .so directly and call the entry point.
tests/load_test: tests/load_test.c
	$(CC) $(CFLAGS) -o $@ $< $(shell pkg-config --libs libva) -ldl

tests/cmd_test: tests/cmd_test.c src/vcmd.c src/loongvpu_abi.h src/vcmd.h
	$(CC) $(CFLAGS) -Isrc -o $@ tests/cmd_test.c src/vcmd.c

tests/workbuf_test: tests/workbuf_test.c src/work_buffer.c src/h264_cabac_init.h src/work_buffer.h src/h264_decode_params.h
	$(CC) $(CFLAGS) -Isrc -o $@ tests/workbuf_test.c src/work_buffer.c

tests/hevcwb_test: tests/hevcwb_test.c src/hevc_work_buffer.c src/hevc_work_buffer.h src/hevc_decode_params.h
	$(CC) $(CFLAGS) -Isrc -o $@ tests/hevcwb_test.c src/hevc_work_buffer.c

test: tests/load_test tests/cmd_test tests/workbuf_test tests/hevcwb_test $(TARGET)
	LD_LIBRARY_PATH=. ./tests/load_test ./$(TARGET)
	./tests/cmd_test
	./tests/workbuf_test
	./tests/hevcwb_test

install: $(TARGET)
	install -d $(DESTDIR)$(DRIVERDIR)
	install -m 755 $(TARGET) $(DESTDIR)$(DRIVERDIR)/$(TARGET)
	@echo "Installed to $(DESTDIR)$(DRIVERDIR)/$(TARGET)"
	@echo "To use it, point libva at this driver dir, e.g.:"
	@echo "  LIBVA_DRIVERS_PATH=$(DESTDIR)$(DRIVERDIR) LIBVA_DRIVER_NAME=loongvpu vainfo"

clean:
	rm -f $(OBJS) $(TARGET) tests/load_test tests/cmd_test tests/workbuf_test tests/hevcwb_test
