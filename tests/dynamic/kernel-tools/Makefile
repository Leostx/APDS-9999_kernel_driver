# SPDX-License-Identifier: GPL-2.0
# Standalone Makefile for IIO userspace tools
# Usage: make -f Makefile.simple
#        make -f Makefile.simple clean

CC      ?= gcc
CFLAGS  += -O2 -Wall -g -D_GNU_SOURCE
LDFLAGS +=

TARGETS := iio_event_monitor iio_generic_buffer

all: $(TARGETS)

iio_event_monitor: iio_utils.c iio_event_monitor.c iio_utils.h
	$(CC) $(CFLAGS) $(LDFLAGS) iio_utils.c iio_event_monitor.c -o $@

iio_generic_buffer: iio_utils.c iio_generic_buffer.c iio_utils.h
	$(CC) $(CFLAGS) $(LDFLAGS) iio_utils.c iio_generic_buffer.c -o $@

clean:
	rm -f $(TARGETS)

.PHONY: all clean
