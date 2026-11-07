/* SPDX-License-Identifier: BSD-2-Clause */
/* K3 A1-E: read-only local PMP shape preflight, NOT an ownership proof.
 * This is deliberately conservative. Hardware capabilities, CSR snapshots,
 * cross-Hart agreement, address-specific priorities and M-mode holds still
 * require independent verification before any PMP write or S-mode entry.
 */
#ifndef __K3_A1_CSR_PREFLIGHT_H__
#define __K3_A1_CSR_PREFLIGHT_H__
#include <stdint.h>
#define K3_A1_CSR_MAX 16U
#define K3_A1_CSR_A 0x18U
#define K3_A1_CSR_NAPOT 0x18U
#define K3_A1_CSR_RWX 0x07U
#define K3_A1_CSR_LOCK 0x80U
/* Bit meanings from RISC-V Smepmp mseccfg, not an extension-presence test. */
#define K3_A1_CSR_MSECCFG_POLICY 0x07U

enum k3_a1_preflight_result {
 K3_A1_PF_OK=0, K3_A1_PF_BAD_INPUT=-1,
 K3_A1_PF_ACTIVE_SMEPMP=-2, K3_A1_PF_NO_GLOBAL_ALLOW=-3,
 K3_A1_PF_SHADOWED=-4, K3_A1_PF_LOCKED=-5,
 K3_A1_PF_NO_SLOTS=-6, K3_A1_PF_AMBIGUOUS=-7
};
struct k3_a1_csr_measured_entry {
 uint64_t prot, addr;
 unsigned int log2len;
};
struct k3_a1_csr_measured {
 unsigned int count, granularity;
 unsigned int pmp_addrbits; /* implemented PMPADDR bits, not RV64 XLEN */
 unsigned int mseccfg_valid;
 uint64_t mseccfg;
 struct k3_a1_csr_measured_entry entry[K3_A1_CSR_MAX];
};
struct k3_a1_local_candidate {
 unsigned int old_allow, fallback;
 uint16_t firmware_entries, free_keystone_entries;
};
/* This gate only establishes structural suitability of a *local* legacy-PMP
 * image. No caller may treat PF_OK as permission to transfer ownership.
 * If anything is ambiguous, reject and preserve the existing build gate.
 */
static inline int k3_a1_preflight_local(
 const struct k3_a1_csr_measured *m, struct k3_a1_local_candidate *out)
{
 unsigned int i, old=K3_A1_CSR_MAX, fallback;
 uint16_t fw=0, free_mask=0;
 if (!m || !out || m->count < 5 || m->count > K3_A1_CSR_MAX ||
     m->granularity > 64 ||
     m->pmp_addrbits == 0 || m->pmp_addrbits > 62)
  return K3_A1_PF_BAD_INPUT;
#ifdef K3_QEMU_PMP16
 /* Emulator contract: all 16 harts expose exactly 16 PMP entries.
  * This is deliberately not the physical K3 A100 PMP count. */
 if (m->count != 16U) return K3_A1_PF_BAD_INPUT;
#endif
 /* Check *active* mode bits. Having the Smepmp extension is not denial. */
 if (m->mseccfg_valid && (m->mseccfg & K3_A1_CSR_MSECCFG_POLICY))
  return K3_A1_PF_ACTIVE_SMEPMP;
 for (i=0;i<m->count;i++) {
  const struct k3_a1_csr_measured_entry *e=&m->entry[i];
  unsigned int mode=(unsigned int)e->prot & K3_A1_CSR_A;
  if (e->prot & ~(uint64_t)0xffU) return K3_A1_PF_BAD_INPUT;
  if (mode!=0 && mode!=0x08U && mode!=0x10U && mode!=0x18U)
   return K3_A1_PF_BAD_INPUT;
  /* pmp_get() can decode a full-coverage NAPOT as order 57
   * on a machine with 54 implemented PMPADDR bits. Comparing against
   * RV64 XLEN (64) incorrectly rejects this valid fallback.
   */
  if (mode==K3_A1_CSR_NAPOT && e->addr==0 &&
      e->log2len >= m->pmp_addrbits + 2U &&
      ((unsigned int)e->prot & K3_A1_CSR_RWX)==K3_A1_CSR_RWX) {
   if (old!=K3_A1_CSR_MAX) return K3_A1_PF_AMBIGUOUS;
   if (e->prot & K3_A1_CSR_LOCK) return K3_A1_PF_LOCKED;
   old=i;
  }
 }
 if (old==K3_A1_CSR_MAX) return K3_A1_PF_NO_GLOBAL_ALLOW;
 /* A1-E root-domain layout: all OpenSBI PMP entries (grants or denies),
  * then untouched Keystone slots, with all-range fallback last. */
 if (old == m->count - 1U) {
  unsigned int seen_free = 0;
  for (i=0; i<old; i++) {
   const struct k3_a1_csr_measured_entry *e=&m->entry[i];
   if (e->prot & K3_A1_CSR_LOCK) return K3_A1_PF_LOCKED;
   if (e->prot & K3_A1_CSR_A) {
    if (seen_free)
     return K3_A1_PF_SHADOWED;
    fw |= (uint16_t)(1U << i);
   } else {
    seen_free = 1;
    free_mask |= (uint16_t)(1U << i);
   }
  }
  if (!fw || __builtin_popcount((unsigned int)free_mask)<3)
   return K3_A1_PF_NO_SLOTS;
  out->old_allow = old;
  out->fallback = old;
  out->firmware_entries = fw;
  out->free_keystone_entries = free_mask;
  return K3_A1_PF_OK;
 }
 /* Denies must take priority over the old broad allow, and all active
  * entries after it would be unknown aliases for the Keystone PMP plan. */
 for (i=0;i<old;i++) {
  const struct k3_a1_csr_measured_entry *e=&m->entry[i];
  if (!((unsigned int)e->prot & K3_A1_CSR_A) ||
      (e->prot & K3_A1_CSR_RWX)) return K3_A1_PF_SHADOWED;
  fw |= (uint16_t)(1U<<i);
 }
 if (!fw) return K3_A1_PF_SHADOWED;
 for (i=old+1;i<m->count;i++) {
  const struct k3_a1_csr_measured_entry *e=&m->entry[i];
  if ((e->prot & K3_A1_CSR_A) || (e->prot & K3_A1_CSR_LOCK))
   return K3_A1_PF_NO_SLOTS;
  free_mask |= (uint16_t)(1U<<i);
 }
 fallback=m->count-1;
 /* Need a fallback after all Keystone deny slots and at least two
  * independent slots for SM + dynamic Enclave denial. */
 if (fallback <= old+2 || __builtin_popcount((unsigned int)free_mask)<3)
  return K3_A1_PF_NO_SLOTS;
 out->old_allow=old;
 out->fallback=fallback;
 out->firmware_entries=fw;
 out->free_keystone_entries=free_mask;
 return K3_A1_PF_OK;
}
#endif
