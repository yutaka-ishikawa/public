#include "app/eapp_utils.h"
#include "app/syscall.h"
#include "shared/sm_call.h"
#include "shared/sm_err.h"
#include "protocol.h"

static volatile uint64_t global_canary = UINT64_C(0xa34e9185f0762bcd);

static uint64_t ticks(void)
{
    uint64_t value;

    __asm__ volatile("rdtime %0" : "=r"(value));
    return value;
}

static void report(struct stop_verify_message *message)
{
    uint64_t reply;

    reply = 0;
    if (ocall(STOP_VERIFY_CALL, message, sizeof *message,
	      &reply, sizeof reply) ||
	reply != (STOP_VERIFY_REPLY ^ message->sequence)) {
	EAPP_RETURN(10 + message->sequence);
    }
}

int EAPP_ENTRY main(void)
{
    static const uint64_t invalid[] = {STOP_EXIT_ENCLAVE, 3, UINT64_MAX};
    volatile uint64_t stack_canary[32];
    struct stop_verify_message message = {0};
    uint64_t start;
    unsigned int i;

    for (i = 0; i < 32; i++) {
	stack_canary[i] = STOP_VERIFY_REPLY ^ i;
    }
    for (i = 0; i < sizeof invalid / sizeof invalid[0]; i++) {
	message.sequence++;
	message.request = invalid[i];
	message.status = SYSCALL_2(STOP_VERIFY_SYSCALL, 0, invalid[i]);
	if (message.status != SBI_ERR_SM_ENCLAVE_ILLEGAL_ARGUMENT) {
	    EAPP_RETURN(1);
	}
	report(&message);
    }
    for (i = 0; i < 3; i++) {
	message.sequence++;
	message.request = STOP_TIMER_INTERRUPT;
	message.status = SYSCALL_2(STOP_VERIFY_SYSCALL, 0,
				   STOP_TIMER_INTERRUPT);
	if (message.status != SBI_ERR_SM_ENCLAVE_SUCCESS) {
	    EAPP_RETURN(2);
	}
	report(&message);
    }
    /* Tell Linux to count asynchronous STOPs only in the next interval. */
    message.sequence++;
    report(&message);
    if (SYSCALL_2(STOP_VERIFY_SYSCALL, 1, 0)) {
	EAPP_RETURN(3);
    }
    /* No explicit STOP syscall or OCALL while waiting in U-mode. */
    start = ticks();
    while (ticks() - start < 4000000) {
	__asm__ volatile("" : : : "memory");
    }
    message.sequence++;
    report(&message);
    /* Cancelling the enclave timer must not cancel SM preemption. */
    message.sequence++;
    report(&message);
    if (SYSCALL_2(STOP_VERIFY_SYSCALL, 2, 0)) {
	EAPP_RETURN(7);
    }
    start = ticks();
    while (ticks() - start < 4000000) {
	__asm__ volatile("" : : : "memory");
    }
    message.sequence++;
    report(&message);
    for (i = 0; i < 32; i++) {
	if (stack_canary[i] != (STOP_VERIFY_REPLY ^ i)) {
	    EAPP_RETURN(5);
	}
    }
    if (global_canary != UINT64_C(0xa34e9185f0762bcd)) {
	EAPP_RETURN(6);
    }
    message.sequence++;
    report(&message);
    EAPP_RETURN(0);
}
