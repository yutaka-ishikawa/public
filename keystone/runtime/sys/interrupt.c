//******************************************************************************
// Copyright (c) 2018, The Regents of the University of California (Regents).
// All Rights Reserved. See LICENSE for license details.
//------------------------------------------------------------------------------
#include "util/regs.h"
#include "call/sbi.h"
#include "sys/timex.h"
#include "sys/interrupt.h"
#include "util/printf.h"
#include <asm/csr.h>

#define DEFAULT_CLOCK_DELAY 10000

#ifdef USE_STOP_VERIFY
/* Available only in the dedicated verification runtime (syscall 1100). */
unsigned long stop_verify_control(unsigned long operation,
				  unsigned long request)
{
    if (operation == 0) {
	csr_clear(sie, SIE_STIE | SIE_SSIE);
	sbi_set_timer(UINT64_MAX);
	return sbi_stop_enclave(request);
    }
    if (operation == 1) {
	sbi_set_timer(get_cycles64() + DEFAULT_CLOCK_DELAY);
	csr_set(sie, SIE_STIE);
	return 0;
    }
    return (unsigned long)-1;
}
#endif

void init_timer(void)
{
  sbi_set_timer(get_cycles64() + DEFAULT_CLOCK_DELAY);
  csr_set(sstatus, SR_SPIE);
  csr_set(sie, SIE_STIE | SIE_SSIE);
}

void handle_timer_interrupt(void)
{
    unsigned long next_cycle;

    sbi_stop_enclave(STOP_TIMER_INTERRUPT);
    next_cycle = get_cycles64() + DEFAULT_CLOCK_DELAY;
    sbi_set_timer(next_cycle);
    csr_set(sstatus, SR_SPIE);
}

void handle_interrupts(struct encl_ctx* regs)
{
  unsigned long cause = regs->scause;

  switch(cause) {
    case INTERRUPT_CAUSE_TIMER:
      handle_timer_interrupt();
      break;
    /* ignore other interrupts */
    case INTERRUPT_CAUSE_SOFTWARE:
    case INTERRUPT_CAUSE_EXTERNAL:
    default:
      sbi_stop_enclave(0);
      return;
  }
}
