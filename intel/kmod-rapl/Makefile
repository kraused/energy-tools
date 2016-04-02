
KVER ?= $(shell uname -r)
KDIR  = /lib/modules/$(KVER)/build

obj-m += rapl.o

all:
	make -C $(KDIR) M=$(PWD) modules

clean:
	make -C $(KDIR) M=$(PWD) clean

