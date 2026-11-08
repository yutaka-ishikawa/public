.DEFAULT_GOAL := guest
ROOT := $(abspath ../..)
KEYSTONE_BUILD_DIR ?= $(ROOT)/.build-keystone
SDK_DIR ?= $(KEYSTONE_BUILD_DIR)/sdk-install
RUNTIME_DIR := $(CURDIR)/build/runtime-src
RUNTIME_SRC := $(shell find $(ROOT)/keystone/runtime -type f ! -path "*/.git/*")
CROSS_COMPILE ?= riscv64-linux-gnu-
OUT := build
EAPP_LIBS := $(SDK_DIR)/lib/libkeystone-eapp.a $(SDK_DIR)/lib/libkeystone-verifier-eapp.a
HOST_LIBS := $(SDK_DIR)/lib/libkeystone-host.a $(SDK_DIR)/lib/libkeystone-edge.a $(SDK_DIR)/lib/libkeystone-verifier.a
.PHONY: guest clean
# Both FIDs run in the same runtime/eapp measurement.
guest: $(OUT)/crypto.ke
$(OUT)/runtime.stamp: $(RUNTIME_SRC) Makefile $(SDK_DIR)/include/shared/crypto_abi.h
	mkdir -p $(OUT)/runtime-src
	cp -a $(ROOT)/keystone/runtime/. $(OUT)/runtime-src/
	cmake -S $(OUT)/runtime-src -B $(OUT)/runtime -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=riscv64 -DCMAKE_C_COMPILER=$(CROSS_COMPILE)gcc -DCMAKE_ASM_COMPILER=$(CROSS_COMPILE)gcc -DKEYSTONE_SDK_DIR=$(SDK_DIR) -DCRYPTO_VERIFY=ON
	cmake --build $(OUT)/runtime --parallel $${JOBS:-4}
	touch $@
$(OUT)/eapp: eapp.c protocol.h Makefile $(EAPP_LIBS)
	mkdir -p $(OUT)
	$(CROSS_COMPILE)gcc $(CPPFLAGS) -O2 -g -Wall -Wextra -Werror -fno-stack-protector -static -nostdlib -Wl,-e,main -I$(SDK_DIR)/include $< $(EAPP_LIBS) -o $@
$(OUT)/eapp-b: eapp.c protocol.h Makefile $(EAPP_LIBS)
	mkdir -p $(OUT)
	$(CROSS_COMPILE)gcc $(CPPFLAGS) -DVARIANT=1 -O2 -g -Wall -Wextra -Werror -fno-stack-protector -static -nostdlib -Wl,-e,main -I$(SDK_DIR)/include $< $(EAPP_LIBS) -o $@
$(OUT)/eapp-unavailable: eapp.c protocol.h Makefile $(EAPP_LIBS)
	mkdir -p $(OUT)
	$(CROSS_COMPILE)gcc $(CPPFLAGS) -DEXPECT_UNAVAILABLE -O2 -g -Wall -Wextra -Werror -fno-stack-protector -static -nostdlib -Wl,-e,main -I$(SDK_DIR)/include $< $(EAPP_LIBS) -o $@
$(OUT)/runner: host.cpp protocol.h Makefile $(HOST_LIBS)
	mkdir -p $(OUT)
	$(CROSS_COMPILE)g++ $(CPPFLAGS) -O2 -g -Wall -Wextra -Werror -std=c++17 -include cstdint -I$(SDK_DIR)/include -I$(SDK_DIR)/include/verifier $< $(HOST_LIBS) -o $@
$(OUT)/crypto.ke: $(OUT)/eapp $(OUT)/eapp-b $(OUT)/eapp-unavailable $(OUT)/runner $(OUT)/runtime.stamp
	mkdir -p $(OUT)/pkg
	cp $(OUT)/eapp $(OUT)/eapp-b $(OUT)/eapp-unavailable $(OUT)/runner $(RUNTIME_DIR)/eyrie-rt $(RUNTIME_DIR)/loader.bin $(OUT)/pkg/
	makeself --noprogress $(OUT)/pkg $@ 'Keystone attestation/sealing verification' ./runner eapp-unavailable eyrie-rt loader.bin - - -
clean:
	rm -rf $(OUT)
