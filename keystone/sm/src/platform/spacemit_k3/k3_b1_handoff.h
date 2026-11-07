/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef KEYSTONE_K3_B1_HANDOFF_H
#define KEYSTONE_K3_B1_HANDOFF_H

#include <stdint.h>
#include <stddef.h>

/* Metadata only. This structure NEVER contains plaintext key bytes. */
#define K3_B1_MAGIC UINT32_C(0x3142314b) /* "K1B1" wire marker */
#define K3_B1_VERSION UINT16_C(1)
#define K3_B1_KEY_ATTESTATION UINT32_C(1)
#define K3_B1_PAGE_SIZE UINT64_C(4096)

struct k3_b1_descriptor {
    uint32_t magic;
    uint16_t version;
    uint16_t header_bytes;
    uint64_t key_physical_address;
    uint64_t protected_region_bytes;
    uint32_t key_bytes;
    uint32_t key_purpose;
    uint64_t boot_epoch;
};

/* Values here must come from independently authenticated boot evidence.
 * This structure is NEVER accepted as a security proof from an OS caller. */
struct k3_b1_expected_layout {
    uint64_t reserved_base;
    uint64_t reserved_bytes;
    uint64_t min_boot_epoch;
    uint32_t expected_key_bytes;
};

/* Pure, side-effect-free preflight. Return 0 on valid syntax and bounds.
 * Returning 0 DOES NOT AUTHORIZE reading a plaintext key. */
int k3_b1_validate_descriptor(const struct k3_b1_descriptor *d,
                              const struct k3_b1_expected_layout *trusted);

/* The platform is fail-closed until verified boot, CPU+DMA isolation,
 * full-hart ownership and secure entropy have been independently proven.
 * There is intentionally no caller-controlled "verified" flag. */
int k3_b1_platform_provision(void);

/* Shared provider entry point: future A provider can reuse the caller ABI. */
int k3_key_provider_initialize(void);

#endif
