#include "sm_trace.h"
/* SPDX-License-Identifier: BSD-2-Clause */
/* K3 IPI transport. This adapter is not part of the production SM build until
 * all-hart enumeration, HSM replay, and PMP ownership have been integrated. */
#include <sbi/sbi_ipi.h>
#include <sbi/sbi_error.h>
#include <sbi/sbi_scratch.h>
#include <sbi/sbi_hsm.h>
#include <sbi/sbi_hart.h>
#include <sbi/sbi_domain.h>
#include <sbi/sbi_hartmask.h>
#include <sbi/sbi_ecall_interface.h>
#include "k3_pmp_ipi_transport.h"
#include "k3_pmp_mailbox.h"

static int k3_pmp_ipi_event = -1;
static unsigned long k3_mailbox_offset;
static k3_pmp_apply_fn k3_apply;

static int k3_pmp_ipi_event_update(struct sbi_scratch *local,
                                  struct sbi_scratch *remote,
                                  u32 remote_index, void *payload)
{
  struct k3_pmp_ipi_request *r = payload;
  struct k3_pmp_mailbox *box;
  (void)local; (void)remote_index;
  if (!r || !remote || !k3_mailbox_offset || !k3_apply) return SBI_ENOTSUPP;
  box = sbi_scratch_offset_ptr(remote, k3_mailbox_offset);
  if (k3_pmp_mailbox_submit(box, r->generation, r->region,
                            r->operation, r->permissions)) return SBI_EINVALID_STATE;
  return SBI_IPI_UPDATE_SUCCESS;
}

static void k3_pmp_ipi_event_process(struct sbi_scratch *scratch)
{
  struct k3_pmp_mailbox *box;
  if (!scratch || !k3_mailbox_offset) return;
  box = sbi_scratch_offset_ptr(scratch, k3_mailbox_offset);
  k3_pmp_mailbox_process(box, k3_apply);
  /* The PMP writer may have modified some CSR state before its readback
   * failed. Reporting FAILED is necessary for the sender, but it is not
   * sufficient: this hart must not resume its interrupted S/U context with
   * an unverified protection configuration. This is the existing IPI
   * handler's failure path, not an additional all-hart hold protocol. */
  if (__atomic_load_n(&box->state, __ATOMIC_ACQUIRE) == K3_PMP_MB_FAILED) {
    SM_TRACE_RESULT("PMP receiver failed", box->result);
    sbi_hart_hang();
  }
}

static const struct sbi_ipi_event_ops k3_pmp_ipi_ops = {
  .name = "k3_keystone_pmp",
  .update = k3_pmp_ipi_event_update,
  .sync = 0,
  .process = k3_pmp_ipi_event_process,
};

int k3_pmp_ipi_transport_register(void)
{
  int id;
  if (k3_pmp_ipi_event >= 0) return SBI_EALREADY;
  /* scratch allocation is only safe during the cold boot initialization phase. */
  k3_mailbox_offset = sbi_scratch_alloc_offset(sizeof(struct k3_pmp_mailbox));
  if (!k3_mailbox_offset) return SBI_ENOMEM;
  id = sbi_ipi_event_create(&k3_pmp_ipi_ops);
  if (id < 0) {
    sbi_scratch_free_offset(k3_mailbox_offset);
    k3_mailbox_offset = 0;
    return id;
  }
  k3_pmp_ipi_event = id;
  return 0;
}

int k3_pmp_ipi_transport_is_ready(void)
{
  return k3_pmp_ipi_event >= 0 && k3_mailbox_offset && k3_apply;
}

void k3_pmp_ipi_transport_unregister(void)
{
  if (k3_pmp_ipi_event >= 0) {
    sbi_ipi_event_destroy((u32)k3_pmp_ipi_event);
    k3_pmp_ipi_event = -1;
  }
  k3_apply = 0;
  if (k3_mailbox_offset) {
    sbi_scratch_free_offset(k3_mailbox_offset);
    k3_mailbox_offset = 0;
  }
}

/* Explicit opt-in only after the caller verifies ownership and HSM replay.
 * No production caller is connected in Stage 175. */
int k3_pmp_ipi_transport_bind_apply(k3_pmp_apply_fn fn)
{
  if (k3_pmp_ipi_event < 0 || !fn) return SBI_EINVAL;
  k3_apply = fn;
  return 0;
}

/* The caller holds the global transaction lock. This backend accepts only
 * enumerated, started harts: HSM replay is deliberately a separate stage. */
int k3_pmp_ipi_transport_send(ulong mask, ulong base,
                              const struct k3_pmp_ipi_request *request)
{
  unsigned int n = 0;
  unsigned int i;
  ulong unmatched;
  int rc;
  struct sbi_hartmask deliverable;
  struct sbi_domain *dom = sbi_domain_thishart_ptr();
  if (k3_pmp_ipi_event < 0 || !k3_apply || !request || !mask || base != 0)
    return SBI_ENOTSUPP;
  if (!dom || sbi_hsm_hart_interruptible_mask(dom, &deliverable))
    return SBI_ENOTSUPP;
  if (sbi_scratch_last_hartindex() >= sizeof(ulong) * 8)
    return SBI_ENOTSUPP;
  /* A nonexistent hartid in the request must never be silently skipped. */
  unmatched = mask;
  for (i = 0; i <= sbi_scratch_last_hartindex(); i++) {
    struct sbi_scratch *remote = sbi_hartindex_to_scratch(i);
    u32 hartid = sbi_hartindex_to_hartid(i);
    if (!remote || hartid >= sizeof(ulong) * 8) return SBI_ENOTSUPP;
    if (!(mask & (1UL << hartid))) continue;
    unmatched &= ~(1UL << hartid);
    if (!sbi_hartmask_test_hartindex(i, &deliverable))
      return SBI_ENOTSUPP; /* Domain/HSM filtering must not silently drop a target. */
    if (__sbi_hsm_hart_get_state(i) != SBI_HSM_STATE_STARTED)
      return SBI_ENOTSUPP;
    n++;
  }
  if (!n || unmatched) return SBI_ENOTSUPP;
  SM_TRACE_VALUES("PMP send", mask, request->generation);
  rc = sbi_ipi_send_many(mask, base, (u32)k3_pmp_ipi_event, (void *)request);
  SM_TRACE_RESULT("PMP send result", rc);
  if (rc) return rc;
  /* A send is never an ACK. Observe every target's exact generation. */
  for (i = 0; i <= sbi_scratch_last_hartindex(); i++) {
    struct sbi_scratch *remote = sbi_hartindex_to_scratch(i);
    u32 hartid = sbi_hartindex_to_hartid(i);
    struct k3_pmp_mailbox *mb;
    if (!(mask & (1UL << hartid))) continue;
    mb = sbi_scratch_offset_ptr(remote, k3_mailbox_offset);
    unsigned long spins = 1000000UL;
    int state;
    do {
      state = k3_pmp_mailbox_check(mb, request->generation);
      if (state < 0) {
        SM_TRACE_VALUES("PMP ACK failed", hartid, request->generation);
        return SBI_EFAIL;
      }
      if (state == 1) break;
    } while (--spins);
    if (!spins) {
      SM_TRACE_VALUES("PMP ACK timeout", hartid, request->generation);
      return SBI_EFAIL;
    }
  }
  for (i = 0; i <= sbi_scratch_last_hartindex(); i++) {
    struct sbi_scratch *remote = sbi_hartindex_to_scratch(i);
    u32 hartid = sbi_hartindex_to_hartid(i);
    struct k3_pmp_mailbox *mb;
    if (!(mask & (1UL << hartid))) continue;
    mb = sbi_scratch_offset_ptr(remote, k3_mailbox_offset);
    if (k3_pmp_mailbox_rearm(mb, request->generation)) return SBI_EFAIL;
  }
  return 0;
}
