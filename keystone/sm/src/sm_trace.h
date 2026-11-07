/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef KEYSTONE_SM_TRACE_H
#define KEYSTONE_SM_TRACE_H
/* Opt-in progress markers only: no key material, memory contents or policy changes. */
#ifdef KEYSTONE_SM_TRACE
#include <sbi/riscv_asm.h>
#include <sbi/sbi_console.h>
#define SM_TRACE_STAGE(stage) \
  sbi_printf("[SM-TRACE] hart=%lu %s\n", (unsigned long)csr_read(CSR_MHARTID), stage)
#define SM_TRACE_RESULT(stage, result) \
  sbi_printf("[SM-TRACE] hart=%lu %s rc=%ld\n", \
             (unsigned long)csr_read(CSR_MHARTID), stage, (long)(result))
#define SM_TRACE_VALUES(stage, a, b) \
  sbi_printf("[SM-TRACE] hart=%lu %s a=%lu b=0x%lx\n", \
             (unsigned long)csr_read(CSR_MHARTID), stage, \
             (unsigned long)(a), (unsigned long)(b))
#else
#define SM_TRACE_STAGE(stage) do { } while (0)
#define SM_TRACE_RESULT(stage, result) do { } while (0)
#define SM_TRACE_VALUES(stage, a, b) do { } while (0)
#endif
#endif
