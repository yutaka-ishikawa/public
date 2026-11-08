/* SPDX-License-Identifier: BSD-2-Clause */
#include "security.h"
#include "crypto.h"
#include "enclave.h"
#include "cpu.h"
#include "sm_call.h"
#include "sm_err.h"
#include <sbi/riscv_asm.h>
#include <sbi/riscv_locks.h>
#include <sbi/sbi_console.h>
#include <sbi/sbi_string.h>
#include <sbi/sbi_trap.h>
#include <sbi/sbi_unpriv.h>

#ifdef KEYSTONE_CRYPTO_QEMU_DEV
#include "keystone_dev_root.inc"
#endif
extern unsigned char _text_start[], _text_end[];
extern unsigned char _rodata_start[], _keystone_rodata_hash_end[];
static int ready;
static int attestation_ready;
static int sealing_ready;
static spinlock_t security_lock = SPIN_LOCK_INITIALIZER;

static struct {
    unsigned char id[KEYSTONE_KEY_ID_MAX];
    unsigned char info[64 + KEYSTONE_KEY_ID_MAX];
    struct report report;
    struct sealing_key seal;
} workspace;

static void erase(void *buffer, size_t size)
{
    volatile unsigned char *p;

    p = buffer;
    while (size--) {
	*p++ = 0;
    }
}

int keystone_security_ready(void)
{
    return ready;
}

int keystone_security_init(void)
{
#ifdef KEYSTONE_CRYPTO_QEMU_DEV
    static const char label[] = "KEYSTONE-SM-IDENTITY-v1";
    unsigned char device_secret[64];
    unsigned char seed[32];
    unsigned char info[sizeof(label) - 1 + 64];
    unsigned char certificate[96];
    uint64_t lengths[2];
    hash_ctx hash;
    int rc;
    size_t i;

    lengths[0] = _text_end - _text_start;
    lengths[1] = _keystone_rodata_hash_end - _rodata_start;
    if (!lengths[0] || !lengths[1]) {
	return -1;
    }
    hash_init(&hash);
    hash_extend(&hash, lengths, sizeof lengths);
    hash_extend(&hash, _text_start, lengths[0]);
    hash_extend(&hash, _rodata_start, lengths[1]);
    hash_finalize(sm_hash, &hash);
    ed25519_create_keypair(dev_public_key, device_secret, keystone_dev_root);
    sbi_memcpy(info, label, sizeof(label) - 1);
    sbi_memcpy(info + sizeof(label) - 1, sm_hash, 64);
    rc = kdf(NULL, 0, keystone_dev_root, 32, info, sizeof info, seed, 32);
    if (!rc) {
	ed25519_create_keypair(sm_public_key, sm_private_key, seed);
	sbi_memcpy(certificate, sm_hash, 64);
	sbi_memcpy(certificate + 64, sm_public_key, 32);
	sign(sm_signature, certificate, sizeof certificate, dev_public_key,
	     device_secret);
	sealing_ready = 1;
	attestation_ready = 1;
	ready = 1;
    }
    erase(device_secret, sizeof device_secret);
    erase(seed, sizeof seed);
    erase(info, sizeof info);
    erase(certificate, sizeof certificate);
    erase(&hash, sizeof hash);
    erase(keystone_dev_root, sizeof keystone_dev_root);
    sbi_printf(
	"[SM/CRYPTO] provider=qemu-dev attestation=%d sealing=%d DEV-ONLY\n",
	ready, ready);
    sbi_printf("[SM/CRYPTO] sm-hash=");
    for (i = 0; i < 64; i++) {
	sbi_printf("%02x", sm_hash[i]);
    }
    sbi_printf("\n[SM/CRYPTO] certificate=");
    for (i = 0; i < 32; i++) {
	sbi_printf("%02x", sm_public_key[i]);
    }
    for (i = 0; i < 64; i++) {
	sbi_printf("%02x", sm_signature[i]);
    }
    sbi_printf("\n");
    return rc;
#else
    sbi_printf("[SM/CRYPTO] provider=unavailable attestation=0 sealing=0\n");
    return 0;
#endif
}

static int in_epm(uintptr_t address, size_t length, uintptr_t base,
		  uint64_t size)
{
    return !length || (address >= base && length <= size &&
		       address - base <= size - length);
}

/* OpenSBI traps and restores each MPRV byte access. The SBI uses EPM PA,
 * so temporarily select Bare addressing, retaining the active enclave PMP. */
static int copy_pa(void *local, uintptr_t remote, size_t length, int output)
{
    struct sbi_trap_info trap;
    unsigned char *p;
    unsigned long status;
    unsigned long satp;
    size_t i;
    int rc;

    p = local;
    status = csr_read(CSR_MSTATUS);
    csr_clear(CSR_MSTATUS, MSTATUS_MIE);
    satp = csr_swap(CSR_SATP, 0);
    __asm__ volatile("sfence.vma" ::: "memory");
    rc = 0;
    for (i = 0; i < length; i++) {
	if (output) {
	    sbi_store_u8((uint8_t *)(remote + i), p[i], &trap);
	} else {
	    p[i] = sbi_load_u8((const uint8_t *)(remote + i), &trap);
	}
	if (trap.cause) {
	    rc = -1;
	    break;
	}
    }
    csr_write(CSR_SATP, satp);
    __asm__ volatile("sfence.vma" ::: "memory");
    csr_write(CSR_MSTATUS, status);
    return rc;
}

unsigned long keystone_security_call(unsigned long fid, uintptr_t output,
				     uintptr_t input, size_t length)
{
    unsigned char measurement[64];
    uintptr_t base;
    uint64_t size;
    void *result;
    size_t result_size;
    size_t limit;
    unsigned long rc;

    if (!cpu_is_enclave_context()) {
	return SBI_ERR_SM_ENCLAVE_SBI_PROHIBITED;
    }
    if ((fid == SBI_SM_ATTEST_ENCLAVE && !attestation_ready) ||
	(fid == SBI_SM_GET_SEALING_KEY && !sealing_ready)) {
	return SBI_ERR_SM_NOT_IMPLEMENTED;
    }
    if (fid != SBI_SM_ATTEST_ENCLAVE && fid != SBI_SM_GET_SEALING_KEY) {
	return SBI_ERR_SM_NOT_IMPLEMENTED;
    }
    limit =
	fid == SBI_SM_ATTEST_ENCLAVE ? ATTEST_DATA_MAXLEN : KEYSTONE_KEY_ID_MAX;
    result_size = fid == SBI_SM_ATTEST_ENCLAVE ? sizeof(struct report)
					       : sizeof(struct sealing_key);
    if (length > limit ||
	keystone_security_snapshot(cpu_get_enclave_id(), measurement, &base,
				   &size) ||
	!in_epm(input, length, base, size) ||
	!in_epm(output, result_size, base, size)) {
	return SBI_ERR_SM_ENCLAVE_ILLEGAL_ARGUMENT;
    }
    spin_lock(&security_lock);
    sbi_memset(&workspace, 0, sizeof workspace);
    rc = SBI_ERR_SM_ENCLAVE_ILLEGAL_ARGUMENT;
    if (copy_pa(workspace.id, input, length, 0)) {
	goto out;
    }
    if (fid == SBI_SM_ATTEST_ENCLAVE) {
	sbi_memcpy(workspace.report.enclave.hash, measurement, 64);
	workspace.report.enclave.data_len = length;
	sbi_memcpy(workspace.report.enclave.data, workspace.id, length);
	sbi_memcpy(workspace.report.sm.hash, sm_hash, 64);
	sbi_memcpy(workspace.report.sm.public_key, sm_public_key, 32);
	sbi_memcpy(workspace.report.sm.signature, sm_signature, 64);
	sbi_memcpy(workspace.report.dev_public_key, dev_public_key, 32);
	sm_sign(workspace.report.enclave.signature, &workspace.report.enclave,
		64 + sizeof(uint64_t) + length);
	result = &workspace.report;
    } else {
	sbi_memcpy(workspace.info, measurement, 64);
	sbi_memcpy(workspace.info + 64, workspace.id, length);
	if (kdf(NULL, 0, sm_private_key, 64, workspace.info, 64 + length,
		workspace.seal.key, SEALING_KEY_SIZE)) {
	    rc = SBI_ERR_SM_ENCLAVE_UNKNOWN_ERROR;
	    goto out;
	}
	sm_sign(workspace.seal.signature, workspace.seal.key, SEALING_KEY_SIZE);
	result = &workspace.seal;
    }
    if (!copy_pa(result, output, result_size, 1)) {
	rc = SBI_ERR_SM_ENCLAVE_SUCCESS;
    }
out:
    erase(&workspace, sizeof workspace);
    erase(measurement, sizeof measurement);
    spin_unlock(&security_lock);
    return rc;
}
