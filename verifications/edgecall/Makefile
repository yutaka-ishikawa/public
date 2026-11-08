.DEFAULT_GOAL := all
ROOT := $(abspath ../..)
COMMON := $(ROOT)/verifications/common
CC ?= cc
CFLAGS ?= -O2 -g
SANITIZERS ?= address,undefined
SAN_FLAGS := $(if $(SANITIZERS),-fsanitize=$(SANITIZERS) -fno-omit-frame-pointer,)
WARNINGS := -Wall -Wextra -Werror -Wno-unused-function -Wno-unused-parameter -Wno-type-limits -Wno-address-of-packed-member
EDGE_DIR := $(ROOT)/keystone/sdk
EDGE_C := $(EDGE_DIR)/src/edge/edge_call.c $(EDGE_DIR)/src/edge/edge_dispatch.c
EDGE_INCLUDES := -I$(EDGE_DIR)/include/edge -I$(EDGE_DIR)/include/shared -Ibuild
SM_INCLUDES := -Ibuild/include -Ibuild -I$(ROOT)/keystone/sm/src -I$(EDGE_DIR)/include/shared -I$(ROOT)/opensbi/include
SM_INPUTS := $(shell python3 -c 'import sys;sys.path.insert(0,"$(COMMON)");import extract_sources;print(" ".join(str(extract_sources.ROOT/p) for p in extract_sources.INPUTS))')
.PHONY: all test clean guest kernel-module kernel-object
all: build/test build/sm-test
build/ocall.inc: extract_runtime.py $(ROOT)/keystone/runtime/call/syscall.c $(EDGE_C) $(wildcard $(EDGE_DIR)/include/edge/*.h)
	python3 extract_runtime.py build
build/production.inc: $(COMMON)/extract_sources.py $(SM_INPUTS)
	python3 $(COMMON)/extract_sources.py build
build/test: test.c build/ocall.inc $(EDGE_C) Makefile
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) $(SAN_FLAGS) $(EDGE_INCLUDES) test.c $(EDGE_C) -o $@ $(LDFLAGS) $(SAN_FLAGS)
build/sm-test: sm-test.c $(COMMON)/harness.c build/production.inc Makefile
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) $(SAN_FLAGS) $(SM_INCLUDES) sm-test.c -o $@ $(LDFLAGS) $(SAN_FLAGS)
test: all
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ./build/test
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ./build/sm-test
clean:
	rm -rf build
guest:
	$(MAKE) -f guest/guest.mk
kernel-module:
	$(MAKE) -f guest/guest.mk kernel-module
kernel-object:
	$(MAKE) -f guest/guest.mk kernel-object
