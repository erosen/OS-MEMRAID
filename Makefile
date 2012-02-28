obj-m	:= vmemraid.o
vmemraid-objs := vmemraid_main.o vmemraid_binary.o


KERNELDIR ?= /lib/modules/$(shell uname -r)/build
PWD       := $(shell pwd)

default: build

build:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

clean:
	mv vmemraid_binary.o vmemraid_binary.preserve
	rm -rf *.o *~ core .depend .*.cmd *.ko *.mod.c .tmp_versions Module.* modules.*
	mv vmemraid_binary.preserve vmemraid_binary.o
