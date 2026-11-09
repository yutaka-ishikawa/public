.DEFAULT_GOAL := guest
ROOT := $(abspath ../..)
KEYSTONE_BUILD_DIR ?= $(ROOT)/.build-keystone
SDK_DIR ?= $(KEYSTONE_BUILD_DIR)/sdk-install
CROSS_COMPILE ?= riscv64-linux-gnu-
CC := $(CROSS_COMPILE)gcc
INCLUDES := -I. -I$(SDK_DIR)/include
WARN := -Wall -Wextra -Werror
CFLAGS ?= -O2 -g
CFLAGS += -std=gnu11
RUNTIME_SOURCES := $(shell find $(ROOT)/keystone/runtime -type f ! -path '*/.git/*')
C_LIBS := $(SDK_DIR)/lib/libkeystone-edge.a $(SDK_DIR)/lib/libkeystone-verifier-eapp.a
SOURCES := coordinator.c common/client.c common/report_check.c workers/lifecycle.c workers/raw.c
.PHONY: guest test clean
guest: build/fault-10 build/fault-11 build/fault-12 build/fault-13 build/fault-11-seal build/fault-12-seal build/coordinator build/eapp build/loader.bin build/eyrie-rt
build/coordinator: $(SOURCES) common/client.h protocol.h ../sbi-4000/protocol.h kernel/abi.h Makefile $(C_LIBS)
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) $(INCLUDES) $(SOURCES) $(C_LIBS) -pthread -o $@
build/eapp: enclave/workload.c protocol.h ../sbi-4000/protocol.h Makefile $(SDK_DIR)/lib/libkeystone-eapp.a $(SDK_DIR)/lib/libkeystone-verifier-eapp.a
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) $(INCLUDES) -static -nostdlib -fno-stack-protector -fno-builtin -Wl,-u,keystone_eapp_start,-e,keystone_eapp_start $< $(SDK_DIR)/lib/libkeystone-eapp.a $(SDK_DIR)/lib/libkeystone-verifier-eapp.a -o $@
	python3 $(ROOT)/scripts/check-keystone-native-entry.py $(CROSS_COMPILE)nm $@
build/runtime.stamp: $(RUNTIME_SOURCES) ../sbi-4000/protocol.h Makefile
	mkdir -p build/runtime-src
	cp -a $(ROOT)/keystone/runtime/. build/runtime-src/
	cp ../sbi-4000/protocol.h build/runtime-src/include/multimem_protocol.h
	cmake -S build/runtime-src -B build/runtime -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=riscv64 -DCMAKE_C_COMPILER=$(CC) -DCMAKE_ASM_COMPILER=$(CC) -DKEYSTONE_SDK_DIR=$(SDK_DIR) -DLINUX_SYSCALL=ON -DENV_SETUP=OFF -DMULTIMEM_VERIFY=ON
	cmake --build build/runtime --parallel $${JOBS:-4}
	touch $@
build/loader.bin build/eyrie-rt: build/runtime.stamp
	cp build/runtime-src/$(notdir $@) $@
test:
	bash run-guest.sh $(TEST_ARGS)
clean:
	rm -rf build

KERNEL_BUILD ?= $(ROOT)/.build-k3/linux
.PHONY: kernel-module
kernel-module:
	$(MAKE) -C $(KERNEL_BUILD) M=$(CURDIR)/kernel ARCH=riscv CROSS_COMPILE=$(CROSS_COMPILE) modules

build/fault-%: enclave/fault.c protocol.h ../sbi-4000/protocol.h Makefile $(SDK_DIR)/lib/libkeystone-eapp.a
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) $(INCLUDES) -DFAULT_STAGE=$(firstword $(subst -, ,$*)) $(if $(findstring seal,$*),-DFAULT_SEAL) -static -nostdlib -fno-stack-protector -fno-builtin -Wl,-u,keystone_eapp_start,-e,keystone_eapp_start $< $(SDK_DIR)/lib/libkeystone-eapp.a -o $@
	python3 $(ROOT)/scripts/check-keystone-native-entry.py $(CROSS_COMPILE)nm $@

.PHONY: kernel-object firmware
kernel-object:
	$(MAKE) -C $(KERNEL_BUILD) M=$(CURDIR)/kernel ARCH=riscv CROSS_COMPILE=$(CROSS_COMPILE) probe.o
firmware:
	bash build-firmware.sh $(CRYPTO_PROFILE)
