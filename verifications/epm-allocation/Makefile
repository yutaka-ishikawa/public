.DEFAULT_GOAL := all
CC ?= cc
CFLAGS ?= -O2 -g
SANITIZERS ?= address,undefined
SAN_FLAGS := $(if $(SANITIZERS),-fsanitize=$(SANITIZERS) -fno-omit-frame-pointer,)
ROOT := ../..
INPUTS := $(ROOT)/keystone/linux-keystone-driver/keystone.h $(ROOT)/keystone/linux-keystone-driver/keystone-page.c $(ROOT)/keystone/linux-keystone-driver/keystone.c $(ROOT)/keystone/linux-keystone-driver/keystone-ioctl.c
.PHONY: all test clean guest
all: build/test-607 build/test-608
build/production.inc: extract.py $(INPUTS)
	python3 extract.py
build/test-%: test.c build/production.inc Makefile
	$(CC) $(CFLAGS) -Wall -Wextra -Werror $(SAN_FLAGS) -DTEST_VERSION=$* $< -o $@ $(SAN_FLAGS)
test: all
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ./build/test-607
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ./build/test-608
clean:
	rm -rf build
guest: build/epm-guest
build/epm-guest: guest.c $(ROOT)/keystone/sdk/include/shared/keystone_user.h
	mkdir -p build
	$(CROSS_COMPILE)gcc $(CPPFLAGS) $(CFLAGS) -static -Wall -Wextra -Werror -I$(ROOT)/keystone/sdk/include/shared $< -o $@
CROSS_COMPILE ?= riscv64-linux-gnu-
