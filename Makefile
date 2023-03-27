BINARY 		:= mycdev
KERNEL      := /lib/modules/$(shell uname -r)/build
KMOD_DIR    := $(shell pwd)
OBJECTS 	:= $(patsubst %.c, %.o, $(wildcard *.c))
TARGET_PATH := /lib/modules/$(shell uname -r)/kernel/drivers/char

obj-m += $(BINARY).o
$(BINARY)-objs := main.o mcdev.o

all: driver test ioctl

driver:
	make -C $(KERNEL) M=$(KMOD_DIR) modules

install:
	cp -f $(BINARY).ko $(TARGET_PATH)
	depmod -a
test: test.cc
	g++ -o $@ $^
ioctl: ioctl.cc
	g++ -o $@ $^
clean:
	rm -f *.ko *.order *.symvers
	rm -f *.o *.mod*
	rm -f test ioctl
