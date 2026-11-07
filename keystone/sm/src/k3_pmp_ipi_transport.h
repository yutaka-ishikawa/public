/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef K3_PMP_IPI_TRANSPORT_H
#define K3_PMP_IPI_TRANSPORT_H
#include <sbi/sbi_types.h>
#include <stdint.h>
#include "k3_pmp_mailbox.h"
struct k3_pmp_ipi_request {
  uint64_t generation;
  int region;
  int operation;
  uint8_t permissions;
};
int k3_pmp_ipi_transport_register(void);
int k3_pmp_ipi_transport_is_ready(void);
void k3_pmp_ipi_transport_unregister(void);
int k3_pmp_ipi_transport_bind_apply(k3_pmp_apply_fn fn);
int k3_pmp_ipi_transport_send(ulong mask, ulong base,
                              const struct k3_pmp_ipi_request *request);
#endif
