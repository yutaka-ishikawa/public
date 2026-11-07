#
# SPDX-License-Identifier: BSD-2-Clause
#
# Copyright (c) 2020 Western Digital Corporation or its affiliates.
#
# Authors:
#   Anup Patel <anup.patel@wdc.com>
#

# Compiler flags
platform-cppflags-y =
platform-cflags-y =
platform-asflags-y =
platform-ldflags-y =

# Command for platform specific "make run"
platform-runcmd = qemu-system-riscv$(PLATFORM_RISCV_XLEN) -M virt -m 256M \
  -nographic -bios $(build_dir)/platform/generic/firmware/fw_payload.bin

# Objects to build
platform-objs-y += platform.o
platform-objs-y += platform_override_modules.carray.o

# Blobs to build
FW_DYNAMIC=y
FW_JUMP=y
ifeq ($(PLATFORM_RISCV_XLEN), 32)
  # This needs to be 4MB aligned for 32-bit system
  FW_JUMP_OFFSET=0x400000
else
  # This needs to be 2MB aligned for 64-bit system
  FW_JUMP_OFFSET=0x200000
endif
FW_JUMP_FDT_OFFSET=0x2200000
FW_PAYLOAD=y
ifeq ($(PLATFORM_RISCV_XLEN), 32)
  # This needs to be 4MB aligned for 32-bit system
  FW_PAYLOAD_OFFSET=0x400000
else
  # This needs to be 2MB aligned for 64-bit system
  FW_PAYLOAD_OFFSET=0x200000
endif
FW_PAYLOAD_FDT_OFFSET=$(FW_JUMP_FDT_OFFSET)

# Opt-in read-only PMP inventory in NORMAL vendor K3 firmware. Unlike the
# Keystone link probe, this configuration retains the ordinary boot path.
# Never confuse CSR inventory with proof of Keystone/IOPMP isolation.
ifeq ($(K3_PMP_DIAG_READONLY),1)
platform-cflags-y += -DK3_PMP_DIAG_READONLY=1
endif

# Explicit developer-only compile/link diagnostic: the K3 platform's
# normal Keystone #error remains active unless this switch is set AND an
# SM linker input is supplied. The resulting ELF is NOT boot-authorized:
# K3's pre-S-mode PMP gate and SM init both halt unconditionally.
ifeq ($(KEYSTONE_K3_DIAGNOSTIC_LINK),1)
ifeq ($(strip $(KEYSTONE_K3_SM_LINK_PROBE)),)
$(error K3 diagnostic requires KEYSTONE_K3_SM_LINK_PROBE=<SM link-only object>)
endif
platform-cflags-y += -DK3_KEYSTONE_DIAGNOSTIC_LINK=1
platform-cppflags-y += -DK3_KEYSTONE_DIAGNOSTIC_LINK=1
endif

# Stage 195: non-bootable link-analysis option only. Does not enable SM.
# The bundle must be built separately from Keystone's real sources.
ifneq ($(KEYSTONE_K3_SM_LINK_PROBE),)
platform-ldflags-y += -Wl,--undefined=sm_init $(KEYSTONE_K3_SM_LINK_PROBE)
endif
