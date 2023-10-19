.PHONY: all modules install modules_install clean

# external KDIR specification is supported
KDIR ?= /lib/modules/$(shell uname -r)/build

SOURCES := gigabyte_waterforce.c

all: modules

install: modules_install

modules modules_install clean:
	make -C $(KDIR) M=$$PWD $@

checkpatch:
	$(KDIR)/scripts/checkpatch.pl --strict --no-tree $(SOURCES)

dev:
	make clean
	make
	sudo rmmod gigabyte_waterforce || true
	sudo insmod gigabyte_waterforce.ko
