.DEFAULT_GOAL := guest
ROOT := $(abspath ../..)
KERNEL_OUT ?= $(ROOT)/.build-k3/linux
CROSS_COMPILE ?= riscv64-linux-gnu-
CFLAGS ?= -O2 -g
.PHONY: all guest test kernel-module kernel-object clean
all guest: build/test
build/test: test.c kernel/abi.h $(ROOT)/keystone/sdk/include/shared/sm_err.h
	mkdir -p build
	$(CROSS_COMPILE)gcc $(CPPFLAGS) $(CFLAGS) -static -Wall -Wextra -Werror -I$(ROOT)/keystone/sdk/include/shared $< -o $@
test:
	bash run-guest.sh
kernel-module:
	@test -s "$(KERNEL_OUT)/Module.symvers" || { echo 'Complete matching kernel build and Module.symvers required.' >&2; exit 1; }
	$(MAKE) -C $(ROOT)/linux-k3 O=$(KERNEL_OUT) ARCH=riscv CROSS_COMPILE=$(CROSS_COMPILE) M=$(CURDIR)/kernel modules
kernel-object:
	$(MAKE) -C $(ROOT)/linux-k3 O=$(KERNEL_OUT) ARCH=riscv CROSS_COMPILE=$(CROSS_COMPILE) M=$(CURDIR)/kernel keystone_sbi_verify.o
clean:
	rm -rf build
