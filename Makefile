KERNEL      := /lib/modules/$(shell uname -r)/build
KMOD_DIR    := $(shell pwd)
CFLAGS += -I $(KMOD_DIR)
OBJECTS := $(patsubst %.c, %.o, $(wildcard *.c))

obj-m += mycdev.o
mycdev-objs := main.o mcdev.o

all: driver test ioctl

driver:
	make -C $(KERNEL) M=$(KMOD_DIR) modules

install:
	cp $(BINARY).ko $(TARGET_PATH)
	depmod -a
test: test.cc
	g++ -o $@ $^
ioctl: ioctl.cc
	g++ -o $@ $^
clean:
	rm -f *.ko *.order *.symvers
	rm -f *.o *.mod*
	rm -f test ioctl
