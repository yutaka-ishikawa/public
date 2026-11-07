.DEFAULT_GOAL := all
COMMON := $(abspath ../common)
ROOT := $(abspath ../..)
CC ?= cc
PYTHON ?= python3
CFLAGS ?= -O2 -g
SANITIZERS ?= address,undefined
WARNINGS := -Wall -Wextra -Werror -Wno-unused-function -Wno-unused-parameter -Wno-type-limits -Wno-address-of-packed-member
SAN_FLAGS := $(if $(SANITIZERS),-fsanitize=$(SANITIZERS) -fno-omit-frame-pointer,)
INPUTS := $(shell $(PYTHON) -c 'import sys;sys.path.insert(0,"$(COMMON)");import extract_sources;print(" ".join(str(extract_sources.ROOT/p) for p in extract_sources.INPUTS))')
INCLUDES := -Ibuild/include -Ibuild -I$(ROOT)/keystone/sm/src -I$(ROOT)/keystone/sdk/include/shared -I$(ROOT)/opensbi/include
.PHONY: all test clean
all: build/test
build/production.inc: $(COMMON)/extract_sources.py $(INPUTS)
	$(PYTHON) $(COMMON)/extract_sources.py build
build/test: test.c $(COMMON)/harness.c build/production.inc $(COMMON)/verification.mk Makefile
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) $(SAN_FLAGS) $(INCLUDES) -MMD -MP -MF build/test.d test.c -o $@ $(LDFLAGS) $(SAN_FLAGS)
test: build/test
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ./build/test
clean:
	rm -rf build

-include build/test.d
