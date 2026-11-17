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
SRCS    := src/loongvpu_drv_video.c
OBJS    := $(SRCS:.c=.o)

# libva on this system looks for drivers under /usr/lib/dri
DRIVERDIR ?= /usr/lib/dri
PREFIX    ?= /usr/local

.PHONY: all install clean test

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(shell pkg-config --libs libva)

%.o: %.c src/loongvpu_abi.h
	$(CC) $(CFLAGS) -c -o $@ $<

# Minimal smoke test: load the .so directly and call the entry point.
tests/load_test: tests/load_test.c
	$(CC) $(CFLAGS) -o $@ $< $(shell pkg-config --libs libva) -ldl

test: tests/load_test $(TARGET)
	LD_LIBRARY_PATH=. ./tests/load_test ./$(TARGET)

install: $(TARGET)
	install -d $(DESTDIR)$(DRIVERDIR)
	install -m 755 $(TARGET) $(DESTDIR)$(DRIVERDIR)/$(TARGET)
	@echo "Installed to $(DESTDIR)$(DRIVERDIR)/$(TARGET)"
	@echo "To use it, point libva at this driver dir, e.g.:"
	@echo "  LIBVA_DRIVERS_PATH=$(DESTDIR)$(DRIVERDIR) LIBVA_DRIVER_NAME=loongvpu vainfo"

clean:
	rm -f $(OBJS) $(TARGET) tests/load_test
