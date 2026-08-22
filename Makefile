apds9999-objs := src/apds9999.o
obj-m += apds9999.o

KERNELDIR ?= /lib/modules/6.19.10+deb13-amd64/build
PWD := $(shell pwd)

all:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) clean

.PHONY: all clean
