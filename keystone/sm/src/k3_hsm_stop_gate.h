/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef K3_HSM_STOP_GATE_H
#define K3_HSM_STOP_GATE_H
/* Shared by the HSM STOP transition and global PMP broadcast. A STOP
 * transition is serialized with the entire update + ACK collection.
 * Never sleep/spin while holding this gate or while servicing an IPI. */
struct k3_hsm_stop_gate { unsigned int busy; };
static inline int k3_hsm_gate_acquire(struct k3_hsm_stop_gate *g)
{
  unsigned int zero = 0;
  if (!g) return -1;
  return __atomic_compare_exchange_n(&g->busy, &zero, 1U, 0,
                                     __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE) ? 0 : -1;
}
static inline void k3_hsm_gate_release(struct k3_hsm_stop_gate *g)
{
  __atomic_store_n(&g->busy, 0U, __ATOMIC_RELEASE);
}
#endif
