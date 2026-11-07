/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef K3_PMP_MAILBOX_H
#define K3_PMP_MAILBOX_H
#include <stdint.h>
/* One outstanding request per target hart. Sender must serialize global operations.
 * Release publishes the complete request; acquire validates ACK visibility.
 * All fields are accessed through __atomic builtins, including on RV64.
 */
#define K3_PMP_MB_IDLE 0U
#define K3_PMP_MB_PENDING 1U
#define K3_PMP_MB_PROCESSING 2U
#define K3_PMP_MB_DONE 3U
#define K3_PMP_MB_FAILED 4U
struct k3_pmp_mailbox {
  uint64_t generation;
  int region;
  int operation;
  uint8_t permissions;
  unsigned int state;
  int result;
};
typedef int (*k3_pmp_apply_fn)(int region, int operation, uint8_t permissions);
static inline int k3_pmp_mailbox_submit(struct k3_pmp_mailbox *m,
                                       uint64_t generation, int region,
                                       int operation, uint8_t permissions)
{
  unsigned int idle = K3_PMP_MB_IDLE;
  if (!m || !generation ||
      !__atomic_compare_exchange_n(&m->state, &idle, K3_PMP_MB_PROCESSING,
                                   0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return -1;
  /* PROCESSING is a private reservation until PENDING is published. */
  m->generation = generation;
  m->region = region;
  m->operation = operation;
  m->permissions = permissions;
  m->result = -1;
  __atomic_store_n(&m->state, K3_PMP_MB_PENDING, __ATOMIC_RELEASE);
  return 0;
}
static inline void k3_pmp_mailbox_process(struct k3_pmp_mailbox *m,
                                         k3_pmp_apply_fn apply)
{
  unsigned int pending = K3_PMP_MB_PENDING;
  int rc;
  if (!m || !__atomic_compare_exchange_n(&m->state, &pending,
                                         K3_PMP_MB_PROCESSING, 0,
                                         __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return;
  /* Caller must perform CSR write AND readback before returning success. */
  rc = apply ? apply(m->region, m->operation, m->permissions) : -1;
  m->result = rc;
  __atomic_store_n(&m->state, rc ? K3_PMP_MB_FAILED : K3_PMP_MB_DONE,
                   __ATOMIC_RELEASE);
}
static inline int k3_pmp_mailbox_check(struct k3_pmp_mailbox *m,
                                      uint64_t generation)
{
  unsigned int state;
  if (!m || !generation) return -1;
  state = __atomic_load_n(&m->state, __ATOMIC_ACQUIRE);
  if (m->generation != generation) return -1;
  if (state == K3_PMP_MB_FAILED) return -1;
  return state == K3_PMP_MB_DONE ? 1 : 0;
}
/* Only release the mailbox after all sender-side checks have finished. */
static inline int k3_pmp_mailbox_rearm(struct k3_pmp_mailbox *m,
                                      uint64_t generation)
{
  unsigned int state;
  if (!m || m->generation != generation) return -1;
  state = __atomic_load_n(&m->state, __ATOMIC_ACQUIRE);
  if (state != K3_PMP_MB_DONE && state != K3_PMP_MB_FAILED) return -1;
  __atomic_store_n(&m->state, K3_PMP_MB_IDLE, __ATOMIC_RELEASE);
  return 0;
}
#endif
