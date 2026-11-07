/* SPDX-License-Identifier: BSD-2-Clause */
/* K3 E2: Hart admission ledger. These are SOFTWARE states, not PMP proofs.
 * Initial STOPPED is quarantine, never an ACK. A successful local CSR
 * snapshot is diagnostic only and does not authorize an S/U-mode entry.
 * No production code may publish `verified` until the verified A1 PMP
 * transaction and CSR comparison have been connected.
 */
#ifndef __K3_E2_HART_QUARANTINE_H__
#define __K3_E2_HART_QUARANTINE_H__
#include <stdint.h>

#define K3_E2_HART_COUNT 16U
#define K3_E2_FULL_MASK UINT16_C(0xffff)
#define K3_E2_OK 0
#define K3_E2_BAD -1
#define K3_E2_DENY -2
#define K3_E2_BUSY -3

struct k3_e2_ledger {
 unsigned int lock;
 uint16_t present, stopped, pending, snapshot, verified, running;
 uint64_t epoch;
 uint64_t proof_epoch[K3_E2_HART_COUNT];
 unsigned int initialized, poisoned;
};

static inline int k3_e2_lock(struct k3_e2_ledger *s)
{
 unsigned int expected=0;
 return s && __atomic_compare_exchange_n(&s->lock,&expected,1,0,
                  __ATOMIC_ACQUIRE,__ATOMIC_RELAXED) ? 0 : K3_E2_BUSY;
}
static inline void k3_e2_unlock(struct k3_e2_ledger *s)
{ __atomic_store_n(&s->lock,0,__ATOMIC_RELEASE); }
/* Coldboot-only; under OpenSBI coldboot sequencing, before starting other
 * Harts. All 16 Hart indices MUST be present. No phantom/DTB-only ACK. */
static inline int k3_e2_cold_init(struct k3_e2_ledger *s,
                                    uint16_t actual_harts, unsigned boot_hart)
{
 unsigned i;
 if (!s || boot_hart >= K3_E2_HART_COUNT ||
     actual_harts!=K3_E2_FULL_MASK || s->initialized) return K3_E2_BAD;
 s->present=actual_harts;
 s->stopped=actual_harts & (uint16_t)~(1U<<boot_hart);
 s->pending=(uint16_t)(1U<<boot_hart);
 s->snapshot=s->verified=s->running=0;
 s->epoch=0; s->poisoned=0;
 for (i=0;i<K3_E2_HART_COUNT;i++) s->proof_epoch[i]=0;
 __atomic_store_n(&s->initialized,1,__ATOMIC_RELEASE);
 return K3_E2_OK;
}
/* Must be serialized with the HSM STOP/START transition AND the global
 * Keystone protection publisher. Caller guarantees protection INACTIVE
 * until HSM Start finishes; fail if this cannot be established. */
static inline int k3_e2_reserve_start(struct k3_e2_ledger *s,unsigned hart)
{
 uint16_t bit;
 if (!s || hart>=K3_E2_HART_COUNT) return K3_E2_BAD;
 if (k3_e2_lock(s)) return K3_E2_BUSY;
 bit=(uint16_t)(1U<<hart);
 if (!s->initialized || s->poisoned || !(s->present&bit) ||
     !(s->stopped&bit) || (s->pending&bit) || s->epoch) {
  k3_e2_unlock(s);return K3_E2_DENY;
 }
 s->stopped &= (uint16_t)~bit;
 s->pending |= bit;
 s->snapshot &= (uint16_t)~bit;
 s->verified &= (uint16_t)~bit;
 s->proof_epoch[hart]=0;
 k3_e2_unlock(s); return K3_E2_OK;
}
static inline int k3_e2_cancel_start(struct k3_e2_ledger *s,unsigned hart)
{
 uint16_t bit;
 if (!s || hart>=K3_E2_HART_COUNT) return K3_E2_BAD;
 if (k3_e2_lock(s)) return K3_E2_BUSY;
 bit=(uint16_t)(1U<<hart);
 if (!s->initialized || s->poisoned || !(s->pending&bit)) {
  k3_e2_unlock(s);return K3_E2_DENY;
 }
 s->pending &= (uint16_t)~bit;
 s->stopped |= bit;
 s->snapshot &= (uint16_t)~bit;
 s->verified &= (uint16_t)~bit;
 s->proof_epoch[hart]=0;
 k3_e2_unlock(s); return K3_E2_OK;
}
/* Snapshot is an observation, not proof of A1 first-match enforcement. */
static inline int k3_e2_note_snapshot(struct k3_e2_ledger *s,unsigned hart)
{
 uint16_t bit;
 if (!s || hart>=K3_E2_HART_COUNT) return K3_E2_BAD;
 if (k3_e2_lock(s)) return K3_E2_BUSY;
 bit=(uint16_t)(1U<<hart);
 if (!s->initialized || s->poisoned || !(s->pending&bit)) {
  k3_e2_unlock(s);return K3_E2_DENY;
 }
 s->snapshot |= bit;
 k3_e2_unlock(s);return K3_E2_OK;
}
/* No firmware caller: future verified A1 transaction must supply an epoch
 * only after shadow+deny+old-allow removal+CSR readback on this Hart. */
static inline int k3_e2_verified_local(struct k3_e2_ledger *s,unsigned hart,
                                       uint64_t epoch, int csr_exact_match)
{
 uint16_t bit;
 if (!s || hart>=K3_E2_HART_COUNT) return K3_E2_BAD;
 if (k3_e2_lock(s)) return K3_E2_BUSY;
 bit=(uint16_t)(1U<<hart);
 if (!s->initialized || s->poisoned || !epoch ||
     epoch!=s->epoch || !csr_exact_match ||
     !(s->pending&bit) || !(s->snapshot&bit)) {
  k3_e2_unlock(s);return K3_E2_DENY;
 }
 s->verified |= bit;
 s->proof_epoch[hart]=epoch;
 k3_e2_unlock(s);return K3_E2_OK;
}
/* Initial HSM Start is NOT a dynamic PMP update: each Hart must first
 * successfully configure and read back its local OpenSBI PMP layout, pass
 * the A1-E local preflight, and record a snapshot. No cross-Hart IPI or
 * dynamic epoch is required at boot. Once a dynamic epoch exists, an
 * unproved Start remains denied; snapshot is NOT an epoch ACK.
 */
static inline int k3_e2_finish_start(struct k3_e2_ledger *s,unsigned hart)
{
 uint16_t bit;
 if (!s || hart>=K3_E2_HART_COUNT) return K3_E2_BAD;
 if (k3_e2_lock(s)) return K3_E2_BUSY;
 bit=(uint16_t)(1U<<hart);
 if (!s->initialized || s->poisoned || !(s->present&bit) ||
     !(s->pending&bit) || !(s->snapshot&bit) || (s->running&bit)) {
  k3_e2_unlock(s);return K3_E2_DENY;
 }
 if (!s->epoch) {
  /* First boot only: proof_epoch=0 and verified=0 are intentional. */
  if ((s->verified&bit) || s->proof_epoch[hart]) {
   k3_e2_unlock(s);return K3_E2_DENY;
  }
 } else if (!(s->verified&bit) ||
            s->proof_epoch[hart]!=s->epoch) {
  /* Never treat a boot snapshot as proof of a dynamic PMP epoch. */
  k3_e2_unlock(s);return K3_E2_DENY;
 }
 s->pending &= (uint16_t)~bit;
 s->running |= bit;
 k3_e2_unlock(s);return K3_E2_OK;
}
/* Only safe after the real STOPPED transition; future starts must replay. */
static inline int k3_e2_mark_stopped(struct k3_e2_ledger *s,unsigned hart)
{
 uint16_t bit;
 if (!s || hart>=K3_E2_HART_COUNT) return K3_E2_BAD;
 if (k3_e2_lock(s)) return K3_E2_BUSY;
 bit=(uint16_t)(1U<<hart);
 if (!s->initialized || s->poisoned || !(s->running&bit)) {
  k3_e2_unlock(s);return K3_E2_DENY;
 }
 s->running &= (uint16_t)~bit;
 s->stopped |= bit;
 s->verified &= (uint16_t)~bit;
 s->snapshot &= (uint16_t)~bit;
 s->proof_epoch[hart]=0;
 k3_e2_unlock(s);return K3_E2_OK;
}
/* Classify a Hart at dynamic PMP update target capture. Initial STARTED
 * Harts have local boot PMP evidence (snapshot), not a dynamic-epoch ACK.
 * All STARTED Harts remain targets for the first real IPI transaction;
 * no synthetic ACK is created. Unexpected/pending states fail closed.
 * Return 0=STOPPED quarantine, 1=STARTED target, negative=reject. */
static inline int k3_e2_classify_target(struct k3_e2_ledger *s,
                                       unsigned hart, int hsm_state)
{
 uint16_t bit;
 int rc=K3_E2_DENY;
 if (!s || hart>=K3_E2_HART_COUNT) return K3_E2_BAD;
 if (k3_e2_lock(s)) return K3_E2_BUSY;
 bit=(uint16_t)(1U<<hart);
 if (!s->initialized || s->poisoned || !(s->present&bit) ||
     (s->pending&bit)) goto done;
 if (hsm_state==0 && (s->stopped&bit) && !(s->running&bit))
  rc=0;
 else if (hsm_state==1 && (s->running&bit) &&
          (s->snapshot&bit) &&
          ((!s->epoch && !(s->verified&bit) && !s->proof_epoch[hart]) ||
           (s->epoch && (s->verified&bit) &&
            s->proof_epoch[hart]==s->epoch)))
  rc=1;
done:
 k3_e2_unlock(s);
 return rc;
}
/* Future global coordinator ONLY. No production publisher in this stage.
 * Fail closed if any start is pending, local PMP is unknown, or running
 * Harts' readback doesn't match. STOPPED Harts are quarantined, not ACKed;
 * the HSM protection gate MUST reject starts while enclaves exist. */
static inline int k3_e2_can_activate(const struct k3_e2_ledger *s)
{
 unsigned i;
 uint16_t running;
 if (!s || !s->initialized || s->poisoned || !s->epoch ||
     s->pending || !s->running ||
     (uint16_t)(s->running|s->stopped)!=s->present ||
     s->verified!=s->running) return K3_E2_DENY;
 running=s->running;
 for (i=0;i<K3_E2_HART_COUNT;i++)
  if ((running&(1U<<i)) && s->proof_epoch[i]!=s->epoch)
   return K3_E2_DENY;
 return K3_E2_OK;
}
#endif
