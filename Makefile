# Build outputs stay in build directories; the dev root persists in private
# user state. Install publishes already-built artifacts without compilation.
.DEFAULT_GOAL := build
.NOTPARALLEL:
ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
PREFIX ?= $(ROOT)/install/keystone
K3_BUILD_DIR ?= $(ROOT)/.build-k3
KEYSTONE_BUILD_DIR ?= $(ROOT)/.build-keystone
STAGE := $(abspath $(KEYSTONE_BUILD_DIR))/stage
JOBS ?= $(shell nproc)
CROSS_COMPILE ?= riscv64-linux-gnu-
ROOTFS_ARCHIVE ?=
ROOTFS_SHA256 ?=
export CROSS_COMPILE JOBS
K3_ARGS = --prefix "$(STAGE)" --build-dir "$(K3_BUILD_DIR)" --jobs "$(JOBS)"
SDK_ARGS = --build-dir "$(KEYSTONE_BUILD_DIR)" --jobs "$(JOBS)"
VERIFY_ARGS = KEYSTONE_BUILD_DIR="$(KEYSTONE_BUILD_DIR)" KERNEL_OUT="$(K3_BUILD_DIR)/linux"
COMPONENTS := qemu opensbi linux eyrie keystone rng crypto verifications samples
.PHONY: all build install build-install $(COMPONENTS) sm sdk firmware guest erie openSBI install-erie install-openSBI run help $(addprefix install-,$(COMPONENTS))
all: build
build: $(COMPONENTS)
build-install: build install
qemu:
	bash "$(ROOT)/scripts/build-install-k3.sh" --only qemu $(K3_ARGS)
sm:
	KEYSTONE_CRYPTO_PROVIDER=unavailable KEYSTONE_RNG_BACKEND=auto bash "$(ROOT)/scripts/build_keystone_sm.sh" "$(ROOT)/keystone" "$(ROOT)/opensbi" "$(K3_BUILD_DIR)/firmware"
opensbi: sm
	KEYSTONE_CRYPTO_PROVIDER=unavailable bash "$(ROOT)/scripts/build_opensbi_link.sh" "$(ROOT)/opensbi" "$(K3_BUILD_DIR)/firmware"
	mkdir -p "$(STAGE)/firmware/k3"
	cp "$(K3_BUILD_DIR)/firmware/build-opensbi-k3-keystone/platform/generic/firmware/"fw_dynamic.* "$(STAGE)/firmware/k3/"
	cp "$(K3_BUILD_DIR)/firmware/sm-combined.o" "$(STAGE)/firmware/k3/"
firmware: opensbi
openSBI: opensbi
install-openSBI: install-opensbi
install-erie: install-eyrie
linux:
	bash "$(ROOT)/scripts/build-install-k3.sh" --only linux $(K3_ARGS)
sdk:
	bash "$(ROOT)/scripts/build-install-keystone.sh" --only sdk $(SDK_ARGS)
# Eyrie and examples are built together by the existing SDK workflow.
eyrie: sdk
erie: eyrie
keystone: sm linux eyrie
	bash "$(ROOT)/scripts/build-install-keystone.sh" --only overlay $(SDK_ARGS) --prefix "$(STAGE)" --k3-build-dir "$(K3_BUILD_DIR)"
guest: keystone
rng: eyrie
	KEYSTONE_CRYPTO_PROVIDER=unavailable bash "$(ROOT)/scripts/build-verification-rng.sh" --skip-qemu --skip-sdk --prefix "$(STAGE)" --build-dir "$(KEYSTONE_BUILD_DIR)/rng" --sdk-build-dir "$(KEYSTONE_BUILD_DIR)" --qemu-build-dir "$(K3_BUILD_DIR)" --jobs "$(JOBS)"
crypto: eyrie
	bash "$(ROOT)/scripts/build-keystone-crypto.sh" "$(KEYSTONE_BUILD_DIR)" "$(STAGE)"
verifications: linux eyrie
	$(MAKE) -C "$(ROOT)/verifications" guest $(VERIFY_ARGS)
	$(MAKE) -C "$(ROOT)/verifications" kernel-modules $(VERIFY_ARGS)
samples: eyrie
	@set -e; for directory in "$(ROOT)"/samples/*; do \
	    if test -f "$$directory/Makefile"; then $(MAKE) -C "$$directory" KEYSTONE_BUILD_DIR="$(KEYSTONE_BUILD_DIR)"; fi; \
	done
install: $(addprefix install-,$(COMPONENTS))
$(addprefix install-,$(COMPONENTS)):
	bash "$(ROOT)/scripts/install-built-keystone.sh" "$(@:install-%=%)" "$(STAGE)" "$(PREFIX)" "$(KEYSTONE_BUILD_DIR)" "$(ROOTFS_ARCHIVE)" "$(ROOTFS_SHA256)"
run:
	bash "$(ROOT)/scripts/run-k3.sh" --prefix "$(PREFIX)"
help:
	@echo 'make / make build           Build only (no install/ or rootfs updates)'
	@echo 'make install                Install already-built outputs; no compilation'
	@echo 'make build-install JOBS=4    Build then install in one command'
	@echo 'make qemu|opensbi|sm|eyrie|keystone|crypto|verifications|linux|rng|samples'
	@echo 'make install-qemu|install-opensbi|install-eyrie|install-keystone|install-crypto|install-verifications'
	@echo 'Stop QEMU/unmount rootfs before install-keystone. See doc/make-build-install.md.'
