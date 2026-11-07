/* SPDX-License-Identifier: BSD-2-Clause */
/* Jupiter 2 platform adapter: fail closed until the root of trust and secure
 * randomness sources are explicitly integrated. No test keys or PRNG. */
#include "../../platform-hook.h"
#include <sbi/sbi_hart.h>
#include <sbi/sbi_console.h>
#include "k3_b1_handoff.h"
#include "k3_dev_embedded_key.h"
#include <crypto.h>
extern byte dev_public_key[PUBLIC_KEY_SIZE];
#include <sbi/sbi_string.h>

/* Symbols are emitted by OpenSBI firmware/fw_base.ldS, not by a separate
 * SM linker script. The range is half-open and includes static SM sections. */
extern char _fw_start[];
extern char _fw_end[];

int k3_sm_firmware_range(uintptr_t *base, size_t *size)
{
    uintptr_t begin = (uintptr_t)_fw_start;
    uintptr_t end = (uintptr_t)_fw_end;
    if (!base || !size || !begin || end <= begin)
        return -1;
    *base = begin;
    *size = end - begin;
    return 0;
}

/* K3 PMP-only mode: neither root keys nor entropy nor DMA setup belongs
 * in the SM PMP bootstrap. The K3 SBI adapter rejects crypto operations. */
unsigned long platform_init_global_once(void)
{
    return 0;
}

unsigned long platform_init_global(void)
{
    return 0;
}

void platform_init_enclave(struct enclave *e) { (void)e; }
void platform_destroy_enclave(struct enclave *e) { (void)e; }
unsigned long platform_create_enclave(struct enclave *e)
{
    (void)e;
    return 0;
}
void platform_switch_to_enclave(struct enclave *e) { (void)e; }
void platform_switch_from_enclave(struct enclave *e) { (void)e; }

/* These functions cannot return insecure data, even if called accidentally. */
int k3_platform_random_checked(uint64_t *value)
{
    if (value) *value = 0;
    sbi_printf("[SM/K3] NOT IMPLEMENTED: secure entropy source unavailable\n");
    return -1;
}
/* Keystone legacy hook cannot signal errors, so it must never provide bytes
 * to an enabled security service on K3. The SBI adapter returns ENOTSUP. */
uint64_t platform_random(void)
{
    uint64_t unused;
    (void)k3_platform_random_checked(&unused);
    return 0;
}

int sm_copy_key(void)
{
#ifdef KEYSTONE_K3_DEV_EMBED_KEY
    /* TODO(K3-Security-Engine): replace with a verified hardware key provider.
     * Embedded development keys are extractable from ELF/BIN/FIT and are
     * never a legitimate device Root of Trust. Do not certify these reports. */
    if (k3_dev_embedded_key_copy(sm_private_key, sm_public_key)) {
        sbi_printf("[SM/K3] embedded DEV key missing\n");
        return -1;
    }
    /* No measured SM certificate or device endorsement is available. */
    sbi_memset(sm_hash, 0, sizeof(sm_hash));
    sbi_memset(sm_signature, 0, sizeof(sm_signature));
    sbi_memset(dev_public_key, 0, sizeof(dev_public_key));
    sbi_printf("[SM/K3] DEV-only embedded key loaded; NOT hardware attestation\n");
    return 0;
#else
    sbi_printf("[SM/K3] trusted root-of-trust key unavailable\n");
    return -1;
#endif
}
