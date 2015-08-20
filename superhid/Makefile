obj-m += superhid.o
KVERSION := $(shell uname -r)

all:
	make -C /lib/modules/${KVERSION}/build M=$(PWD) modules -I$(PWD) EXTRA_CFLAGS="-g -I$(PWD)/include -I$(PWD)"

module_install:
	install -d ${DESTDIR}/lib/modules/${KVERSION}/kernel/driver/xen
	install -m 0644 superhid.ko ${DESTDIR}/lib/modules/${KVERSION}/kernel/driver/xen
	depmod -a

modules:
	$(MAKE) -C $(KERNELDIR) M=`pwd` modules EXTRA_CFLAGS="-DXC_KERNEL=1 -I$(PWD)/include -I$(PWD)"

modules_install:
	$(MAKE) -C $(KERNELDIR) M=`pwd` modules_install

clean:
	$(MAKE) -C $(KERNELDIR) M=`pwd` clean
