.DEFAULT_GOAL := guest
KERNEL_OUT ?= $(abspath ../../.build-k3/linux)
ROOT := $(abspath ../..)
KEYSTONE_BUILD_DIR ?= $(ROOT)/.build-keystone
SDK_DIR ?= $(KEYSTONE_BUILD_DIR)/sdk-install
RUNTIME_DIR := $(CURDIR)/build/runtime-src
RUNTIME_SRC := $(shell find $(ROOT)/keystone/runtime -type f ! -path "*/.git/*")
CROSS_COMPILE ?= riscv64-linux-gnu-
OUT := build
EAPP_LIBS := $(SDK_DIR)/lib/libkeystone-eapp.a
HOST_LIBS := $(SDK_DIR)/lib/libkeystone-host.a $(SDK_DIR)/lib/libkeystone-edge.a
.PHONY: guest clean
# Dedicated real-SM multimem integration.
guest: $(OUT)/multimem.ke $(OUT)/test
$(OUT)/test: test.c kernel/abi.h Makefile
	mkdir -p $(OUT)
	$(CROSS_COMPILE)gcc $(CPPFLAGS) -O2 -g -Wall -Wextra -Werror -I$(SDK_DIR)/include $< -o $@
.PHONY: kernel-module kernel-object
kernel-module:
	$(MAKE) -C $(ROOT)/linux-k3 O=$(KERNEL_OUT) ARCH=riscv CROSS_COMPILE=$(CROSS_COMPILE) M=$(CURDIR)/kernel modules

kernel-object:
	$(MAKE) -C $(ROOT)/linux-k3 O=$(KERNEL_OUT) ARCH=riscv CROSS_COMPILE=$(CROSS_COMPILE) M=$(CURDIR)/kernel probe.o

$(OUT)/runtime.stamp: $(RUNTIME_SRC) protocol.h Makefile $(SDK_DIR)/include/shared/sm_call.h
	mkdir -p $(OUT)/runtime-src
	cp -a $(ROOT)/keystone/runtime/. $(OUT)/runtime-src/
	cp protocol.h $(OUT)/runtime-src/include/multimem_protocol.h
	cmake -S $(OUT)/runtime-src -B $(OUT)/runtime -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=riscv64 -DCMAKE_C_COMPILER=$(CROSS_COMPILE)gcc -DCMAKE_ASM_COMPILER=$(CROSS_COMPILE)gcc -DKEYSTONE_SDK_DIR=$(SDK_DIR) -DMULTIMEM_VERIFY=ON
	cmake --build $(OUT)/runtime --parallel $${JOBS:-4}
	touch $@
$(OUT)/eapp: eapp.c protocol.h Makefile $(EAPP_LIBS)
	mkdir -p $(OUT)
	$(CROSS_COMPILE)gcc $(CPPFLAGS) -O2 -g -Wall -Wextra -Werror -fno-stack-protector -static -nostdlib -Wl,-u,keystone_eapp_start,-e,keystone_eapp_start -I$(SDK_DIR)/include $< $(EAPP_LIBS) -o $@
	python3 "$(ROOT)/scripts/check-keystone-native-entry.py" "$(CROSS_COMPILE)nm" "$@"
$(OUT)/runner: host.cpp protocol.h Makefile $(HOST_LIBS)
	mkdir -p $(OUT)
	$(CROSS_COMPILE)g++ $(CPPFLAGS) -O2 -g -Wall -Wextra -Werror -std=c++17 -include cstdint -I$(SDK_DIR)/include $< $(HOST_LIBS) -o $@
$(OUT)/multimem.ke: $(OUT)/eapp $(OUT)/runner $(OUT)/runtime.stamp
	mkdir -p $(OUT)/pkg
	cp $(OUT)/eapp $(OUT)/runner $(RUNTIME_DIR)/eyrie-rt $(RUNTIME_DIR)/loader.bin $(OUT)/pkg/
	makeself --noprogress $(OUT)/pkg $@ 'Keystone FID 4000 verification' ./runner eapp eyrie-rt loader.bin
clean:
	rm -rf $(OUT)
