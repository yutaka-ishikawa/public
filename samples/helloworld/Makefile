# Application build only: reuse an already built Keystone SDK and hello runtime.
.DEFAULT_GOAL := all
SAMPLE_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
REPO_DIR := $(abspath $(SAMPLE_DIR)/../..)
KEYSTONE_BUILD_DIR ?= $(REPO_DIR)/.build-keystone
SDK_DIR ?= $(KEYSTONE_BUILD_DIR)/sdk-install
RUNTIME_DIR ?= $(KEYSTONE_BUILD_DIR)/examples/hello
BUILD_DIR := $(SAMPLE_DIR)/build
CROSS_COMPILE ?= riscv64-linux-gnu-
# GNU make supplies built-in CC/CXX defaults, so ?= would select host compilers.
ifeq ($(origin CC),default)
CC := $(CROSS_COMPILE)gcc
endif
ifeq ($(origin CXX),default)
CXX := $(CROSS_COMPILE)g++
endif
MAKESELF ?= makeself
CFLAGS ?= -O2 -g -Wall -Wextra
CXXFLAGS ?= -O2 -g -Wall -Wextra
SDK_LIBS := $(SDK_DIR)/lib/libkeystone-host.a $(SDK_DIR)/lib/libkeystone-edge.a
RUNTIME_FILES := $(addprefix $(RUNTIME_DIR)/,eyrie-rt loader.bin .options_log)
PACKAGE := $(BUILD_DIR)/hello.ke

.PHONY: all package check clean
all: $(PACKAGE)
package: $(PACKAGE)

$(BUILD_DIR):
	mkdir -p "$@"

$(BUILD_DIR)/hello: $(SAMPLE_DIR)/eapp/hello.c $(SAMPLE_DIR)/Makefile | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -MF "$@.d" $(LDFLAGS) -static "$<" -o "$@" $(LDLIBS)

$(BUILD_DIR)/hello-runner: $(SAMPLE_DIR)/host/host.cpp $(SDK_LIBS) $(SAMPLE_DIR)/Makefile | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) -I"$(SDK_DIR)/include" -include cstdint $(CXXFLAGS) -MMD -MP -MF "$@.d" $(LDFLAGS) "$<" -o "$@" $(SDK_LIBS) $(LDLIBS)

# Make missing prerequisites actionable without invoking CMake implicitly.
$(SDK_LIBS) $(RUNTIME_FILES):
	@echo "Missing $@" >&2
	@echo "Prepare SDK/runtime: bash scripts/build-install-keystone.sh --only sdk (from repository root)" >&2
	@echo "For another build tree, set KEYSTONE_BUILD_DIR, SDK_DIR or RUNTIME_DIR." >&2
	@exit 1

$(PACKAGE): $(BUILD_DIR)/hello $(BUILD_DIR)/hello-runner $(RUNTIME_FILES) $(SAMPLE_DIR)/Makefile
	mkdir -p "$(BUILD_DIR)/pkg"
	cp "$(BUILD_DIR)/hello" "$(BUILD_DIR)/hello-runner" $(foreach file,$(RUNTIME_FILES),"$(file)") "$(BUILD_DIR)/pkg/"
	$(MAKESELF) --noprogress "$(BUILD_DIR)/pkg" "$@" "Keystone Enclave Package" ./hello-runner hello eyrie-rt loader.bin

check: $(PACKAGE)
	bash "$(PACKAGE)" --check

clean:
	rm -f "$(PACKAGE)" "$(BUILD_DIR)/hello" "$(BUILD_DIR)/hello-runner" "$(BUILD_DIR)/hello.d" "$(BUILD_DIR)/hello-runner.d"
	rm -f $(addprefix "$(BUILD_DIR)/pkg/,$(addsuffix ",hello hello-runner eyrie-rt loader.bin .options_log))
	-rmdir "$(BUILD_DIR)/pkg" "$(BUILD_DIR)"

-include $(BUILD_DIR)/hello.d $(BUILD_DIR)/hello-runner.d
