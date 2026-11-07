/* SPDX-License-Identifier: BSD-2-Clause */
#include "k3_b1_handoff.h"

#if defined(K3_KEY_PROVIDER_HW_ROT_A) || defined(K3_KEY_PROVIDER_TEST_ONLY)
#error "K3 B1 backend cannot be built as HW RoT or TEST_ONLY provider"
#endif

static int valid_range(uint64_t base, uint64_t size)
{
    return size != 0 && base <= UINT64_MAX - size;
}

int k3_b1_validate_descriptor(const struct k3_b1_descriptor *d,
                              const struct k3_b1_expected_layout *trusted)
{
    uint64_t region_end, key_end, reserved_end;

    if (!d || !trusted)
        return -1;
    if (d->magic != K3_B1_MAGIC || d->version != K3_B1_VERSION ||
        d->header_bytes != sizeof(*d) ||
        d->key_purpose != K3_B1_KEY_ATTESTATION)
        return -1;
    if (!trusted->expected_key_bytes ||
        d->key_bytes != trusted->expected_key_bytes ||
        d->boot_epoch < trusted->min_boot_epoch)
        return -1;
    if ((d->key_physical_address & (K3_B1_PAGE_SIZE - 1)) != 0 ||
        (trusted->reserved_base & (K3_B1_PAGE_SIZE - 1)) != 0 ||
        (trusted->reserved_bytes & (K3_B1_PAGE_SIZE - 1)) != 0 ||
        (d->protected_region_bytes & (K3_B1_PAGE_SIZE - 1)) != 0)
        return -1;
    if (!valid_range(d->key_physical_address, d->protected_region_bytes) ||
        !valid_range(trusted->reserved_base, trusted->reserved_bytes) ||
        !valid_range(d->key_physical_address, d->key_bytes))
        return -1;
    region_end = d->key_physical_address + d->protected_region_bytes;
    key_end = d->key_physical_address + d->key_bytes;
    reserved_end = trusted->reserved_base + trusted->reserved_bytes;
    if (d->key_physical_address < trusted->reserved_base ||
        region_end > reserved_end || key_end > region_end)
        return -1;
    return 0;
}

int k3_b1_platform_provision(void)
{
    /* NOT YET AUTHORIZED: a software descriptor does not establish trust.
     * Missing: authenticated prior-stage handoff, proven physical CPU/IOPMP
     * isolation before key placement, entropy, CSR/IOPMP state readback,
     * and the single-owner all-hart handoff. No key is touched here. */
    return -1;
}

int k3_key_provider_initialize(void)
{
    return k3_b1_platform_provision();
}
