//******************************************************************************
// Copyright (c) 2018, The Regents of the University of California (Regents).
// All Rights Reserved. See LICENSE for license details.
//------------------------------------------------------------------------------
#include "ipi.h"
#include "sm.h"
#include "pmp.h"
#include <crypto.h>
#include "enclave.h"
#include "platform-hook.h"
#include "sm-sbi-opensbi.h"
#include <sbi/sbi_string.h>
#include <sbi/riscv_locks.h>
#include <sbi/riscv_barrier.h>
#include <sbi/sbi_console.h>
#include <sbi/sbi_hart.h>
#include <sbi/sbi_hsm.h>
#include <sbi/sbi_scratch.h>

static int sm_init_done = 0;
/* The cold Hart publishes global initialization before entering S-mode.
 * For K3, each secondary Hart completes sm_init(false) before OpenSBI
 * publishes HSM STARTED. Therefore HSM STARTED is the single authoritative
 * per-Hart bootstrap signal; no duplicate local bitmask is necessary. */
#ifndef KEYSTONE_K3_PMP_N_REG
static int sm_enclave_admission_ready = 0;
#endif
int sm_is_ready(void)
{
#ifdef KEYSTONE_K3_PMP_N_REG
  /* Existing OpenSBI startup order:
   * local PMP + sm_init(false) -> HSM STARTED -> first S-mode entry.
   * Do not duplicate the HSM state with a separate Hart bitmap.
   * The actual EPM deny + remote CSR ACK remain mandatory in CREATE.
   * Reject until all configured QEMU Harts are STARTED. */
  unsigned int i;
  if (!__atomic_load_n(&sm_init_done, __ATOMIC_ACQUIRE) ||
      !k3_pmp_partition_is_prepared() || !k3_pmp_sync_healthy() ||
      sbi_scratch_last_hartindex() != MAX_HARTS - 1)
    return 0;
  for (i = 0; i < MAX_HARTS; ++i) {
    if (!sbi_hartindex_to_scratch(i) ||
        sbi_hartindex_to_hartid(i) >= MAX_HARTS ||
        __sbi_hsm_hart_get_state(i) != SBI_HSM_STATE_STARTED)
      return 0;
  }
  return 1;
#else
  return __atomic_load_n(&sm_enclave_admission_ready, __ATOMIC_ACQUIRE);
#endif
}
/* K3 SM text and static data are part of the OpenSBI firmware image.
 * OpenSBI owns its PMP deny rule; Keystone must not allocate or rewrite it.
 * The OS-wide switchable region remains Keystone-owned for enclave entry. */
#ifndef KEYSTONE_K3_PMP_N_REG
static int sm_region_id = 0;
#endif
static int os_region_id = 0;

#ifndef TARGET_PLATFORM_HEADER
#error "SM requires a defined platform to build"
#endif

// Special target platform header, set by configure script
#include TARGET_PLATFORM_HEADER

byte sm_hash[MDSIZE] = { 0, };
byte sm_signature[SIGNATURE_SIZE] = { 0, };
byte sm_public_key[PUBLIC_KEY_SIZE] = { 0, };
byte sm_private_key[PRIVATE_KEY_SIZE] = { 0, };
byte dev_public_key[PUBLIC_KEY_SIZE] = { 0, };

int osm_pmp_set(uint8_t perm)
{
  /* in case of OSM, PMP cfg is exactly the opposite.*/
  return pmp_set_keystone(os_region_id, perm);
}

#ifndef KEYSTONE_K3_PMP_N_REG
static int smm_init(void)
{
  int region = -1;
  int ret = pmp_region_init_atomic(SMM_BASE, SMM_SIZE, PMP_PRI_TOP, &region, 0);
  if(ret)
    return -1;

  return region;
}

#endif /* !KEYSTONE_K3_PMP_N_REG */

static int osm_init(void)
{
  int region = -1;
  int ret = pmp_region_init_atomic(0, -1UL, PMP_PRI_BOTTOM, &region, 1);
  if(ret)
    return -1;

  return region;
}

void sm_sign(void* signature, const void* data, size_t len)
{
  sign(signature, data, len, sm_public_key, sm_private_key);
}

int sm_derive_sealing_key(unsigned char *key, const unsigned char *key_ident,
                          size_t key_ident_size,
                          const unsigned char *enclave_hash)
{
  unsigned char info[MDSIZE + key_ident_size];

  sbi_memcpy(info, enclave_hash, MDSIZE);
  sbi_memcpy(info + MDSIZE, key_ident, key_ident_size);

  /*
   * The key is derived without a salt because we have no entropy source
   * available to generate the salt.
   */
  return kdf(NULL, 0,
             (const unsigned char *)sm_private_key, PRIVATE_KEY_SIZE,
             info, MDSIZE + key_ident_size, key, SEALING_KEY_SIZE);
}

static void sm_print_hash(void)
{
  for (int i=0; i<MDSIZE; i++)
  {
    sbi_printf("%02x", (char) sm_hash[i]);
  }
  sbi_printf("\n");
}

/*
void sm_print_cert()
{
	int i;

	printm("Booting from Security Monitor\n");
	printm("Size: %d\n", sanctum_sm_size[0]);

	printm("============ PUBKEY =============\n");
	for(i=0; i<8; i+=1)
	{
		printm("%x",*((int*)sanctum_dev_public_key+i));
		if(i%4==3) printm("\n");
	}
	printm("=================================\n");

	printm("=========== SIGNATURE ===========\n");
	for(i=0; i<16; i+=1)
	{
		printm("%x",*((int*)sanctum_sm_signature+i));
		if(i%4==3) printm("\n");
	}
	printm("=================================\n");
}
*/

int sm_init(bool cold_boot)
{
#if defined(KEYSTONE_K3_PMP_N_REG)
  /* K3 PMP-only SM bootstrap. Keys, entropy and DMA are deliberately not
   * prerequisites, but no enclave is admitted until PMP ownership and the
   * protected SM physical placement have actually been established. */
  if (!k3_pmp_partition_is_prepared()) {
    sbi_printf("[SM/K3] PMP ownership uncommitted; SM init denied\n");
    return SBI_ERR_SM_NOT_IMPLEMENTED;
  }
  /* No standalone SMM_BASE/SIZE: the linker-owned OpenSBI+SM image is
   * protected by OpenSBI's already-programmed firmware PMP entries.
   * The pre-S-mode OpenSBI A1-E gate verifies that root PMP layout.
   * This check rejects absent/malformed linker bounds, not PMP security. */
  uintptr_t fw_base = 0;
  size_t fw_size = 0;
  if (k3_sm_firmware_range(&fw_base, &fw_size)) {
    sbi_printf("[SM/K3] merged OpenSBI+SM linker range invalid\n");
    return SBI_ERR_SM_NOT_IMPLEMENTED;
  }
#endif

	// initialize SMM
  if (cold_boot) {
    /* only the cold-booting hart will execute these */
    sbi_printf("[SM] Initializing ... hart [%lx]\n", csr_read(mhartid));

    /* A local PMP transition must not race HSM Start. */
    if (k3_keystone_pmp_assert_active()) {
      sbi_printf("[SM] K3 PMP/HSM admission conflict at cold boot\n");
      return SBI_ERR_SM_ENCLAVE_PMP_FAILURE;
    }

#ifndef KEYSTONE_K3_PMP_N_REG
    sm_region_id = smm_init();
    if(sm_region_id < 0) {
      sbi_printf("[SM] SM PMP region allocation failed\n");
      return SBI_ERR_SM_ENCLAVE_PMP_FAILURE;
    }
#else
    /* Merged K3 SM: firmware PMP is exclusively OpenSBI-owned. */
#endif

    os_region_id = osm_init();
    if(os_region_id < 0) {
      sbi_printf("[SM] OS PMP region allocation failed\n");
      return SBI_ERR_SM_ENCLAVE_PMP_FAILURE;
    }

    if (platform_init_global_once() != SBI_ERR_SM_ENCLAVE_SUCCESS) {
      sbi_printf("[SM] PMP-only platform init failed\n");
      return SBI_ERR_SM_NOT_IMPLEMENTED;
    }
    // Copy the keypair from the root of trust
#ifndef KEYSTONE_K3_PMP_N_REG
    sm_copy_key();
#else
    /* Cryptographic services are separately rejected by the K3 SBI adapter.
     * Never derive an identity from zero-initialized SM key buffers. */
    sbi_printf("[SM/K3] PMP-only mode: key/entropy initialization skipped\n");
#endif

    // Init the enclave metadata
    enclave_init_metadata();

    /* Do not expose the SBI interface until SM global state is ready. */
    if (sbi_ecall_register_extension(&ecall_keystone_enclave)) {
      sbi_printf("[SM] unable to register Keystone SBI extension\n");
      return SBI_ERR_SM_NOT_IMPLEMENTED;
    }

    __atomic_store_n(&sm_init_done, 1, __ATOMIC_RELEASE);
    mb();
  }

  /* wait until cold-boot hart finishes */
  while (!sm_init_done)
  {
    mb();
  }

  /* below are executed by all harts */
  if (pmp_init() ||
#ifndef KEYSTONE_K3_PMP_N_REG
      pmp_set_keystone(sm_region_id, PMP_NO_PERM) ||
#endif
      pmp_set_keystone(os_region_id, PMP_ALL_PERM)) {
    sbi_printf("[SM] PMP initialization failed; denying admission\n");
    return SBI_ERR_SM_ENCLAVE_PMP_FAILURE;
  }

  /* Fire platform specific global init */
  if (platform_init_global() != SBI_ERR_SM_ENCLAVE_SUCCESS) {
    sbi_printf("[SM] platform local init failed\n");
    return SBI_ERR_SM_NOT_IMPLEMENTED;
  }

#ifdef KEYSTONE_K3_PMP_N_REG
  /* The OpenSBI HSM finish path publishes STARTED only after this
   * local SM/PMP initialization returns successfully. */
#endif
  sbi_printf("[SM/K3] PMP-only SM initialized (no attestation, sealing, entropy or DMA proof)\n");

#ifndef KEYSTONE_K3_PMP_N_REG
  sm_print_hash();
#endif
  return SBI_ERR_SM_ENCLAVE_SUCCESS;
  // for debug
  // sm_print_cert();
}
