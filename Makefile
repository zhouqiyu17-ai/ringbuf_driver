obj-m += ringbuf_driver.o

KDIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

.PHONY: all clean user

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

user:
	gcc -Wall -Wextra writer.c -o writer
	gcc -Wall -Wextra reader.c -o reader
	gcc -Wall -Wextra test.c -o test

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	rm -f writer reader test
