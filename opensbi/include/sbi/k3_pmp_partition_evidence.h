/* SPDX-License-Identifier: BSD-2-Clause */
/* K3 Stage 207: pure, fail-closed PMP partition evidence validator.
 * No PMP writes; NOT an authorization to hand off ownership or boot SM.
 * This checks caller-supplied snapshots only. An M-mode CSR inventory and
 * cross-hart synchronization must supply and attest the evidence in future.
 */
#ifndef K3_PMP_PARTITION_EVIDENCE_H
#define K3_PMP_PARTITION_EVIDENCE_H

#include <stdint.h>

#define K3_PMP_EVIDENCE_MAX_HARTS 16U
#define K3_PMP_EVIDENCE_MAX_REGS  16U

enum k3_pmp_evidence_result {
 K3_PMP_EVIDENCE_OK = 0,
 K3_PMP_EVIDENCE_BAD_INPUT = -1,
 K3_PMP_EVIDENCE_UNVERIFIED = -2,
 K3_PMP_EVIDENCE_OVERLAP = -3,
 K3_PMP_EVIDENCE_OUT_OF_RANGE = -4,
 K3_PMP_EVIDENCE_ACTIVE_UNOWNED = -5,
 K3_PMP_EVIDENCE_LOCKED_KEYSTONE = -6,
 K3_PMP_EVIDENCE_NONUNIFORM = -7,
 K3_PMP_EVIDENCE_NO_CAPACITY = -8,
 K3_PMP_EVIDENCE_PRIORITY_UNKNOWN = -9,
 K3_PMP_EVIDENCE_PRIORITY_CONFLICT = -10,
 K3_PMP_EVIDENCE_DYNAMIC_PRIORITY_UNKNOWN = -11,
 K3_PMP_EVIDENCE_DYNAMIC_PRIORITY_CONFLICT = -12
};

/* Bit positions are PMP entry numbers, not hart IDs. No CSR is read here. */
struct k3_pmp_hart_evidence {
 uint8_t present;
 uint8_t pmp_count;
 uint8_t csr_snapshot_verified;
 uint8_t legacy_pmp_mode_verified;
 uint16_t opensbi_reserved; /* Includes every active pre-handoff entry. */
 uint16_t keystone_reserved; /* Disjoint from OpenSBI, fixed for this boot. */
 uint16_t active_entries;   /* PMP A!=OFF from verified CSR snapshot. */
 uint16_t locked_entries;   /* PMP L bit from verified CSR snapshot. */
 /* Measurements of FIRST-MATCH PMP priority over the exact SM range.
  * Never infer them from allocation masks or DTB. */
 uint8_t sm_priority_verified;
 uint16_t prior_su_allow_sm_overlap; /* active earlier permissive rules */
 /* Does any lower-index OpenSBI permissive PMP match a possible dynamic
  * Enclave address before the Keystone deny region? Include all-RAM allow.
  * Requires actual root domain/PMP range analysis, not just CSR mask bits. */
 uint8_t dynamic_priority_verified;
 uint16_t prior_su_allow_dynamic_overlap;
};

struct k3_pmp_partition_evidence {
 uint8_t hart_count; /* K3: exactly 16 Hart indices; no assumptions on IDs */
 struct k3_pmp_hart_evidence hart[K3_PMP_EVIDENCE_MAX_HARTS];
};

/* No trust decision may be made on an absent/partial CSR measurement. */
static inline int k3_pmp_validate_partition_evidence(
 const struct k3_pmp_partition_evidence *e)
{
 unsigned int i;
 if (!e || e->hart_count != K3_PMP_EVIDENCE_MAX_HARTS)
  return K3_PMP_EVIDENCE_BAD_INPUT;
 for (i = 0; i < K3_PMP_EVIDENCE_MAX_HARTS; i++) {
  const struct k3_pmp_hart_evidence *h = &e->hart[i];
  uint32_t valid;
  if (!h->present || h->pmp_count == 0 ||
      h->pmp_count > K3_PMP_EVIDENCE_MAX_REGS)
   return K3_PMP_EVIDENCE_BAD_INPUT;
#ifdef K3_QEMU_PMP16
  /* Reject a mixed or under-provisioned virtual Hart. Fail closed. */
  if (h->pmp_count != 16U) return K3_PMP_EVIDENCE_BAD_INPUT;
#endif
  if (!h->csr_snapshot_verified || !h->legacy_pmp_mode_verified)
   return K3_PMP_EVIDENCE_UNVERIFIED;
  valid = (1U << h->pmp_count) - 1U;
  if (((uint32_t)(h->opensbi_reserved | h->keystone_reserved |
                   h->active_entries | h->locked_entries) & ~valid) != 0)
   return K3_PMP_EVIDENCE_OUT_OF_RANGE;
  if (!h->keystone_reserved || (h->opensbi_reserved & h->keystone_reserved))
   return K3_PMP_EVIDENCE_OVERLAP;
  if (h->active_entries & ~h->opensbi_reserved)
   return K3_PMP_EVIDENCE_ACTIVE_UNOWNED;
  if (h->locked_entries & h->keystone_reserved)
   return K3_PMP_EVIDENCE_LOCKED_KEYSTONE;
 }
 return K3_PMP_EVIDENCE_OK;
}

/* Returns 1 only if a fixed identical Keystone slot set works on all harts.
 * A failure is evidence for *investigation*, not a reason to loosen guards. */
static inline int k3_pmp_partition_has_uniform_keystone_slots(
 const struct k3_pmp_partition_evidence *e)
{
 unsigned int i;
 uint16_t mask;
 if (k3_pmp_validate_partition_evidence(e) != K3_PMP_EVIDENCE_OK)
  return 0;
 mask = e->hart[0].keystone_reserved;
 for (i = 1; i < K3_PMP_EVIDENCE_MAX_HARTS; i++)
  if (e->hart[i].keystone_reserved != mask) return 0;
 return 1;
}

/* Candidate A: both OpenSBI AND Keystone use identical fixed slot numbers.
 * Require an actual CSR-backed check of first-match precedence: a lower
 * OpenSBI PMP allow rule must not shadow the Keystone SM deny region.
 * Synthetic callers can test this validator, but cannot certify hardware. */
static inline int k3_pmp_validate_uniform_partition(
 const struct k3_pmp_partition_evidence *e, uint16_t *keystone_mask)
{
 unsigned int i;
 uint16_t osm, ksm;
 int ret = k3_pmp_validate_partition_evidence(e);
 if (ret != K3_PMP_EVIDENCE_OK) return ret;
 osm = e->hart[0].opensbi_reserved;
 ksm = e->hart[0].keystone_reserved;
 /* SM deny, OS allow and at least one dynamic Enclave region. */
 if (!osm || __builtin_popcount((unsigned int)ksm) < 3)
  return K3_PMP_EVIDENCE_NO_CAPACITY;
 for (i = 0; i < K3_PMP_EVIDENCE_MAX_HARTS; ++i) {
  const struct k3_pmp_hart_evidence *h = &e->hart[i];
  if (h->opensbi_reserved != osm || h->keystone_reserved != ksm)
   return K3_PMP_EVIDENCE_NONUNIFORM;
  if (!h->sm_priority_verified)
   return K3_PMP_EVIDENCE_PRIORITY_UNKNOWN;
  if (h->prior_su_allow_sm_overlap)
   return K3_PMP_EVIDENCE_PRIORITY_CONFLICT;
  if (!h->dynamic_priority_verified)
   return K3_PMP_EVIDENCE_DYNAMIC_PRIORITY_UNKNOWN;
  if (h->prior_su_allow_dynamic_overlap)
   return K3_PMP_EVIDENCE_DYNAMIC_PRIORITY_CONFLICT;
 }
 if (keystone_mask) *keystone_mask = ksm;
 return K3_PMP_EVIDENCE_OK;
}

#endif
