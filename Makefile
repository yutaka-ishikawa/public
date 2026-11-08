# Host build/install entry point. Component tools check their own dependencies.
.DEFAULT_GOAL := all
.NOTPARALLEL:
ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
PREFIX ?= $(ROOT)/install/keystone
K3_BUILD_DIR ?= $(ROOT)/.build-k3
KEYSTONE_BUILD_DIR ?= $(ROOT)/.build-keystone
SAMPLE_DIRS := $(patsubst %/Makefile,%,$(wildcard $(ROOT)/samples/*/Makefile))
JOBS ?= $(shell nproc)
CROSS_COMPILE ?= riscv64-linux-gnu-
ROOTFS_ARCHIVE ?=
ROOTFS_SHA256 ?=
export CROSS_COMPILE
K3_ARGS = --prefix "$(PREFIX)" --build-dir "$(K3_BUILD_DIR)" --jobs "$(JOBS)"
SDK_ARGS = --build-dir "$(KEYSTONE_BUILD_DIR)" --jobs "$(JOBS)"
VERIFY_ARGS = KEYSTONE_BUILD_DIR="$(KEYSTONE_BUILD_DIR)" KERNEL_OUT="$(K3_BUILD_DIR)/linux"
.PHONY: all install qemu firmware linux sdk rng verifications samples guest run help
all: install
install: qemu firmware linux sdk rng verifications samples guest
qemu:
	bash "$(ROOT)/scripts/build-install-k3.sh" --only qemu $(K3_ARGS)
firmware:
	KEYSTONE_RNG_BACKEND=auto bash "$(ROOT)/scripts/build-install-k3.sh" --only firmware $(K3_ARGS)
linux:
	bash "$(ROOT)/scripts/build-install-k3.sh" --only linux $(K3_ARGS)
sdk:
	bash "$(ROOT)/scripts/build-install-keystone.sh" --only sdk $(SDK_ARGS)
rng: sdk
	bash "$(ROOT)/scripts/build-verification-rng.sh" --skip-qemu --skip-sdk --prefix "$(PREFIX)" --build-dir "$(KEYSTONE_BUILD_DIR)/rng" --sdk-build-dir "$(KEYSTONE_BUILD_DIR)" --qemu-build-dir "$(K3_BUILD_DIR)" --jobs "$(JOBS)"
verifications: linux sdk
	$(MAKE) -C "$(ROOT)/verifications" guest $(VERIFY_ARGS)
	$(MAKE) -C "$(ROOT)/verifications" kernel-modules $(VERIFY_ARGS)
samples: sdk
	@set -e; for directory in $(foreach dir,$(SAMPLE_DIRS),"$(dir)"); do \
	    $(MAKE) -C "$$directory" KEYSTONE_BUILD_DIR="$(KEYSTONE_BUILD_DIR)"; \
	done
guest: linux sdk
	bash "$(ROOT)/scripts/build-install-keystone.sh" --only guest $(SDK_ARGS) --prefix "$(PREFIX)" --k3-build-dir "$(K3_BUILD_DIR)" --rootfs-archive "$(ROOTFS_ARCHIVE)" --rootfs-sha256 "$(ROOTFS_SHA256)"
run:
	bash "$(ROOT)/scripts/run-k3.sh" --prefix "$(PREFIX)"
help:
	@echo 'make [JOBS=4]                 Build/install Keystone stack, RNG variants, verifications and sample'
	@echo 'make firmware|qemu|sdk|linux  Build/install a selected component'
	@echo 'make guest                   Build matching kernel/SDK, driver/tools and create/update image'
	@echo 'make run                     Boot installed Keystone profile'
	@echo 'Stop QEMU and unmount the guest image before install. No guest tests run automatically.'
