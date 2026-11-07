/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef K3_HSM_START_POLICY_H
#define K3_HSM_START_POLICY_H
/* Conservative admission barrier: UNKNOWN (initial state) is never admitted.
 * A trusted publisher must hold the same gate as begin/finish and MUST NOT
 * publish INACTIVE until all Keystone PMP effects have been removed.
 * This header does not supply such a publisher; no auto-certification. */
enum k3_pmp_protection { K3_PMP_UNKNOWN=0, K3_PMP_INACTIVE=1,
                         K3_PMP_ACTIVE=2, K3_PMP_TRANSITION=3 };
struct k3_hsm_start_policy {
  unsigned int gate;
  unsigned int protection;
  unsigned int inflight;
  unsigned int reserved_hart_mask;
};
static inline int k3_policy_lock(struct k3_hsm_start_policy *p) {
  unsigned int zero=0;
  return __atomic_compare_exchange_n(&p->gate,&zero,1U,0,
                                     __ATOMIC_ACQUIRE,__ATOMIC_RELAXED)?0:-1;
}
static inline void k3_policy_unlock(struct k3_hsm_start_policy *p) {
  __atomic_store_n(&p->gate,0U,__ATOMIC_RELEASE);
}
/* lock must be held */
static inline int k3_policy_begin_hart_locked(struct k3_hsm_start_policy *p, unsigned int idx) {
  if (idx >= 16U || (p->reserved_hart_mask & (1U << idx))) return -1;
  if (p->protection!=K3_PMP_INACTIVE || p->inflight==~0U) return -1;
  ++p->inflight; p->reserved_hart_mask |= 1U << idx; return 0;
}
/* Called if HSM admission failed, or by the started Hart before S-mode. */
static inline int k3_policy_finish_hart_locked(struct k3_hsm_start_policy *p, unsigned int idx) {
  if (idx >= 16U || !p->inflight || !(p->reserved_hart_mask & (1U << idx))) return -1;
  p->reserved_hart_mask &= ~(1U << idx); --p->inflight; return 0;
}
/* lock must be held; ACTIVE or TRANSITION blocked while a start is pending */
static inline int k3_policy_set_protection_locked(struct k3_hsm_start_policy *p,
                                                   unsigned int state) {
  if (state>K3_PMP_TRANSITION || state==K3_PMP_UNKNOWN) return -1;
  if (p->inflight && state!=K3_PMP_INACTIVE) return -1;
  p->protection=state;return 0;
}
#endif
