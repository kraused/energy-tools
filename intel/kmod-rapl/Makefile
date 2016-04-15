
VERSION    = 1
PATCHLEVEL = 0

KVER ?= $(shell uname -r)
KDIR  = /lib/modules/$(KVER)/build

obj-m += rapl.o

all:
	make -C $(KDIR) M=$(PWD) modules

clean:
	make -C $(KDIR) M=$(PWD) clean

tar:
	python2 tar.py kmod-rapl $(VERSION).$(PATCHLEVEL)

install:
	install -m0755 -d $(PREFIX)/lib/modules/$(KVER)/extra
	install -m0644 rapl.ko $(PREFIX)/lib/modules/$(KVER)/extra/rapl.ko

