# SPDX-License-Identifier: GPL-2.0
#
# Makefile for building APDS-9999 device tree overlays.
#

DTC ?= dtc
DT_OVERLAY_SRC := apds9999-rpi-overlay.dts
DT_OVERLAY_BIN  := apds9999-rpi-overlay.dtbo

# Use the kernel's dtc if available, otherwise fall back to system dtc
ifeq ($(KERNELDIR),)
  KERNELDIR ?= /lib/modules/$(shell uname -r)/build
endif

DTC_FLAGS += -@ -H epapr -W no-unit_address_format \
             -W no-simple_bus_reg -W no-avoid_unnecessary_addr_size \
             -W no-alias_paths -W no-pci_bridge -W no-pci_device_reg

all: $(DT_OVERLAY_BIN)

$(DT_OVERLAY_BIN): $(DT_OVERLAY_SRC)
	$(DTC) -I dts -O dtb -o $@ -b 0 $(DTC_FLAGS) $<

clean:
	rm -f $(DT_OVERLAY_BIN)

install: $(DT_OVERLAY_BIN)
	mkdir -p /boot/overlays
	cp $(DT_OVERLAY_BIN) /boot/overlays/

uninstall:
	rm -f /boot/overlays/$(DT_OVERLAY_BIN)

load: $(DT_OVERLAY_BIN)
	sudo dtoverlay $(DT_OVERLAY_BIN)

unload:
	sudo dtoverlay -r $(DT_OVERLAY_BIN:.dtbo=)

.PHONY: all clean install uninstall load unload
