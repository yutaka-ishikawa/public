.DEFAULT_GOAL := guest
ROOT := $(abspath ../..)
KEYSTONE_BUILD_DIR ?= $(ROOT)/.build-keystone
SDK_DIR ?= $(KEYSTONE_BUILD_DIR)/sdk-install
CROSS_COMPILE ?= riscv64-linux-gnu-
ENV_RUNTIME ?= $(KEYSTONE_BUILD_DIR)/examples/hello
OUT := build
SDK_LIBS := $(SDK_DIR)/lib/libkeystone-host.a $(SDK_DIR)/lib/libkeystone-edge.a
RUNTIME_SRC := $(shell find $(ROOT)/keystone/runtime -type f ! -path '*/.git/*')
.PHONY: guest test clean
guest: $(OUT)/random.ke
$(OUT)/runtime.stamp: $(RUNTIME_SRC) Makefile $(SDK_DIR)/include/shared/sm_err.h
	mkdir -p $(OUT)/runtime-src
	cp -a $(ROOT)/keystone/runtime/. $(OUT)/runtime-src/
	cmake -S $(OUT)/runtime-src -B $(OUT)/runtime -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=riscv64 -DCMAKE_C_COMPILER=$(CROSS_COMPILE)gcc -DCMAKE_ASM_COMPILER=$(CROSS_COMPILE)gcc -DKEYSTONE_SDK_DIR=$(SDK_DIR) -DLINUX_SYSCALL=ON -DENV_SETUP=OFF
	cmake --build $(OUT)/runtime --parallel $${JOBS:-4}
	touch $@
$(OUT)/eapp: eapp.c protocol.h Makefile $(SDK_DIR)/lib/libkeystone-eapp.a
	mkdir -p $(OUT)
	$(CROSS_COMPILE)gcc $(CPPFLAGS) -O2 -g -Wall -Wextra -Werror -fno-stack-protector -static -nostdlib -Wl,-u,keystone_eapp_start,-e,keystone_eapp_start -I$(SDK_DIR)/include $< $(SDK_DIR)/lib/libkeystone-eapp.a -o $@
	python3 "$(ROOT)/scripts/check-keystone-native-entry.py" "$(CROSS_COMPILE)nm" "$@"
$(OUT)/eapp-error: eapp.c protocol.h Makefile $(SDK_DIR)/lib/libkeystone-eapp.a
	mkdir -p $(OUT)
	$(CROSS_COMPILE)gcc $(CPPFLAGS) -DEXPECT_ERROR -O2 -g -Wall -Wextra -Werror -fno-stack-protector -static -nostdlib -Wl,-u,keystone_eapp_start,-e,keystone_eapp_start -I$(SDK_DIR)/include $< $(SDK_DIR)/lib/libkeystone-eapp.a -o $@
	python3 "$(ROOT)/scripts/check-keystone-native-entry.py" "$(CROSS_COMPILE)nm" "$@"
$(OUT)/runner: host.cpp protocol.h Makefile $(SDK_LIBS)
	mkdir -p $(OUT)
	$(CROSS_COMPILE)g++ $(CPPFLAGS) -O2 -g -Wall -Wextra -Werror -std=c++17 -include cstdint -I$(SDK_DIR)/include $< $(SDK_LIBS) -o $@
$(OUT)/env: env.c protocol.h Makefile $(SDK_DIR)/lib/libkeystone-eapp.a
	mkdir -p $(OUT)
	$(CROSS_COMPILE)gcc $(CPPFLAGS) -O2 -g -Wall -Wextra -Werror -static -I$(SDK_DIR)/include $< $(SDK_DIR)/lib/libkeystone-eapp.a -o $@
$(OUT)/random.ke: $(OUT)/env $(OUT)/eapp $(OUT)/eapp-error $(OUT)/runner $(OUT)/runtime.stamp $(ENV_RUNTIME)/eyrie-rt $(ENV_RUNTIME)/loader.bin
	mkdir -p $(OUT)/pkg
	cp $(OUT)/eapp $(OUT)/eapp-error $(OUT)/runner $(OUT)/runtime-src/eyrie-rt $(OUT)/runtime-src/loader.bin $(OUT)/pkg/
	cp $(OUT)/env $(OUT)/pkg/
	cp $(ENV_RUNTIME)/eyrie-rt $(OUT)/pkg/env-runtime
	cp $(ENV_RUNTIME)/loader.bin $(OUT)/pkg/env-loader.bin
	makeself --noprogress $(OUT)/pkg $@ 'Keystone SBI 3001 verification' ./runner eapp eyrie-rt loader.bin 0
test:
	bash run-guest.sh
clean:
	rm -rf $(OUT)
