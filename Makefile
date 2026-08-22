apds9999-objs := src/apds9999.o
obj-m += apds9999.o

KERNELDIR ?= /lib/modules/6.19.10+deb13-amd64/build
PWD := $(shell pwd)

BUFFER ?= 1
ifeq ($(BUFFER),1)
  ccflags-y += -DAPDS9999_BUFFER
endif

all: buffer

buffer:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) BUFFER=1 modules

no-buffer:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) BUFFER=0 modules

clean:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) clean

.PHONY: all buffer no-buffer clean
