# Compile and package applications only; SDK/Eyrie are prepared separately.
.DEFAULT_GOAL := all
REPO_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)
KEYSTONE_BUILD_DIR ?= $(REPO_DIR)/.build-keystone
SDK_DIR ?= $(KEYSTONE_BUILD_DIR)/sdk-install
RUNTIME_DIR ?= $(KEYSTONE_BUILD_DIR)/examples/hello-native
BUILD_DIR := $(CURDIR)/build
CROSS_COMPILE ?= riscv64-linux-gnu-
ifeq ($(origin CC),default)
CC := $(CROSS_COMPILE)gcc
endif
ifeq ($(origin CXX),default)
CXX := $(CROSS_COMPILE)g++
endif
MAKESELF ?= makeself
CFLAGS ?= -O2 -g -Wall -Wextra
CXXFLAGS ?= -O2 -g -Wall -Wextra
INCLUDES := -I$(SDK_DIR)/include -I$(SDK_DIR)/include/app -I$(SDK_DIR)/include/edge
HOST_INCLUDES := -I$(SDK_DIR)/include -I$(SDK_DIR)/include/host -I$(SDK_DIR)/include/verifier -I$(REPO_DIR)/samples/common
EAPP_LIBS := $(SDK_DIR)/lib/libkeystone-eapp.a $(SDK_DIR)/lib/libkeystone-edge.a
HOST_LIBS := $(SDK_DIR)/lib/libkeystone-host.a $(SDK_DIR)/lib/libkeystone-edge.a $(SDK_DIR)/lib/libkeystone-verifier.a
NATIVE_FLAGS := -fno-stack-protector -fno-builtin -static -nostdlib -Wl,-u,keystone_eapp_start,-e,keystone_eapp_start
RUNTIME_FILES := $(addprefix $(RUNTIME_DIR)/,eyrie-rt loader.bin .options_log)
.PHONY: all check clean
$(BUILD_DIR):
	mkdir -p "$@"
$(BUILD_DIR)/$(HOST): $(HOST_SOURCE) $(REPO_DIR)/samples/common/runner.hpp $(HOST_LIBS) Makefile ../common.mk | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(HOST_INCLUDES) -include cstdint $(CXXFLAGS) -std=c++17 -MMD -MP -MF "$@.d" $(LDFLAGS) "$<" $(HOST_LIBS) $(LDLIBS) -o "$@"
$(sort $(EAPP_LIBS) $(HOST_LIBS) $(RUNTIME_FILES)):
	@echo "Missing $@; prepare SDK/runtime with make sdk at repository root." >&2
	@exit 1
check: all
	@work=$$(mktemp -d); trap 'rm -rf "$$work"' EXIT; \
	    bash "$(REPO_DIR)/samples/common/extract-package.sh" "$(BUILD_DIR)/$(PACKAGE)" "$$work"
clean:
	rm -rf "$(BUILD_DIR)"
-include $(wildcard $(BUILD_DIR)/*.d)
