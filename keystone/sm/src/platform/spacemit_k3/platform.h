/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef KEYSTONE_SPACEMIT_K3_PLATFORM_H
#define KEYSTONE_SPACEMIT_K3_PLATFORM_H
/* On K3, SM code/rodata/data/BSS are linked into OpenSBI firmware.
 * OpenSBI owns the firmware-protection PMP entries; no SMM_BASE/SIZE.
 * PMP 16 is an explicit QEMU assumption, not a physical A100 claim. */
#ifndef KEYSTONE_K3_PMP_N_REG
#define KEYSTONE_K3_PMP_N_REG 16
#endif
#if KEYSTONE_K3_PMP_N_REG != 16
#error "QEMU Jupiter2 Keystone requires exactly 16 PMP entries per Hart"
#endif
#define PMP_N_REG KEYSTONE_K3_PMP_N_REG
#define PMP_MAX_N_REGION 16
#define MAX_HARTS 16
#define ENCL_MAX 16
#define ENCLAVE_REGIONS_MAX 8
struct platform_enclave_data { unsigned char reserved; };
int sm_copy_key(void);
int k3_sm_firmware_range(uintptr_t *base, size_t *size);
int k3_platform_random_checked(uint64_t *value);
#endif
