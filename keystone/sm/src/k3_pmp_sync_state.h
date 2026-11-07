/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef K3_PMP_SYNC_STATE_H
#define K3_PMP_SYNC_STATE_H
#include <stdint.h>
/* State transitions are local to each mailbox, issued by caller with synchronization. */
struct k3_pmp_sync_state {
  uint64_t requested_generation;
  uint64_t completed_generation;
  int last_error;
};
/* A new request cannot replace an unacknowledged request. */
static inline int k3_pmp_sync_request(struct k3_pmp_sync_state *s, uint64_t generation)
{
  if (!s || !generation || s->requested_generation != s->completed_generation ||
      generation <= s->requested_generation) return -1;
  s->requested_generation = generation;
  s->last_error = 0;
  return 0;
}
/* The receiver must call this ONLY after its local PMP write and readback succeed. */
static inline int k3_pmp_sync_complete(struct k3_pmp_sync_state *s, uint64_t generation, int error)
{
  if (!s || !generation || generation != s->requested_generation ||
      s->requested_generation == s->completed_generation) return -1;
  if (error) { s->last_error = error; return error; }
  s->completed_generation = generation;
  s->last_error = 0;
  return 0;
}
static inline int k3_pmp_sync_is_complete(const struct k3_pmp_sync_state *s, uint64_t generation)
{
  return s && generation && s->requested_generation == generation &&
         s->completed_generation == generation && !s->last_error;
}
#endif
