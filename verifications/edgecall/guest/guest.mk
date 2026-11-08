.DEFAULT_GOAL := guest
ROOT := $(abspath ../..)
KEYSTONE_BUILD_DIR ?= $(ROOT)/.build-keystone
SDK_DIR ?= $(KEYSTONE_BUILD_DIR)/sdk-install
RUNTIME_DIR ?= $(KEYSTONE_BUILD_DIR)/examples/hello-native
KERNEL_OUT ?= $(ROOT)/.build-k3/linux
CROSS_COMPILE ?= riscv64-linux-gnu-
GCC := $(CROSS_COMPILE)gcc
GXX := $(CROSS_COMPILE)g++
CCFLAGS ?= -O2 -g
OUT := build/guest
SDK_LIBS := $(SDK_DIR)/lib/libkeystone-host.a $(SDK_DIR)/lib/libkeystone-edge.a
.PHONY: guest kernel-module kernel-object
guest: $(OUT)/edgecall.ke
$(OUT):
	mkdir -p $@
$(OUT)/eapp: guest/eapp.c guest/protocol.h guest/guest.mk $(SDK_DIR)/lib/libkeystone-eapp.a
	mkdir -p $(OUT)
	$(GCC) $(CPPFLAGS) $(CCFLAGS) -Wall -Wextra -Werror -fno-stack-protector -static -nostdlib -Wl,-e,main -Iguest -I$(SDK_DIR)/include $< $(SDK_DIR)/lib/libkeystone-eapp.a -o $@
$(OUT)/runner: guest/host.cpp guest/protocol.h kernel/probe_abi.h guest/guest.mk $(SDK_LIBS) | $(OUT)
	$(GXX) $(CPPFLAGS) $(CCFLAGS) -Wall -Wextra -Werror -std=c++17 -include cstdint -Iguest -I$(SDK_DIR)/include $< $(SDK_LIBS) -o $@
$(OUT)/edgecall.ke: $(OUT)/eapp $(OUT)/runner $(RUNTIME_DIR)/eyrie-rt $(RUNTIME_DIR)/loader.bin
	mkdir -p $(OUT)/pkg
	cp $(OUT)/eapp $(OUT)/runner $(RUNTIME_DIR)/eyrie-rt $(RUNTIME_DIR)/loader.bin $(OUT)/pkg/
	makeself --noprogress $(OUT)/pkg $@ 'Keystone edgecall verification' ./runner eapp eyrie-rt loader.bin
kernel-module:
	@test -s "$(KERNEL_OUT)/Module.symvers" || { echo 'A complete matching kernel build and Module.symvers are required.' >&2; exit 1; }
	$(MAKE) -C $(ROOT)/linux-k3 O=$(KERNEL_OUT) ARCH=riscv CROSS_COMPILE=$(CROSS_COMPILE) M=$(CURDIR)/kernel modules
kernel-object:
	$(MAKE) -C $(ROOT)/linux-k3 O=$(KERNEL_OUT) ARCH=riscv CROSS_COMPILE=$(CROSS_COMPILE) M=$(CURDIR)/kernel edgecall_pmp_probe.o
