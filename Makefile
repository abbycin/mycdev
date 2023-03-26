KERNEL      := /lib/modules/$(shell uname -r)/build
KMOD_DIR    := $(shell pwd)
CFLAGS += -I $(KMOD_DIR)
OBJECTS := $(patsubst %.c, %.o, $(wildcard *.c))

obj-m += mcdev.o

all: driver test

driver:
	make -C $(KERNEL) M=$(KMOD_DIR) modules

install:
	cp $(BINARY).ko $(TARGET_PATH)
	depmod -a
test: test.cc
	g++ -o $@ $^
clean:
	rm -f *.ko *.order *.symvers
	rm -f *.o *.mod*
	rm -f test
