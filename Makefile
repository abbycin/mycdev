BINARY 		:= mycdev
KERNEL      := /lib/modules/$(shell uname -r)/build
KMOD_DIR    := $(shell pwd)
TARGET_PATH := /lib/modules/$(shell uname -r)/kernel/drivers/char
CXX := g++ -std=gnu++17

obj-m += $(BINARY).o
$(BINARY)-objs := main.o mcdev.o

all: driver test ioctl poll

driver:
	make -C $(KERNEL) M=$(KMOD_DIR) modules

install:
	cp -f $(BINARY).ko $(TARGET_PATH)
	depmod -a
test: test.cc
	$(CXX) -o $@ $^
ioctl: ioctl.cc
	$(CXX) -o $@ $^
poll: poll.cc
	$(CXX) -o $@ $^
clean:
	rm -f *.ko *.order *.symvers
	rm -f *.o *.mod*
	rm -f test ioctl
