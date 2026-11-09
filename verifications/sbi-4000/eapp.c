#include "app/eapp_utils.h"
#include "app/syscall.h"
#include "protocol.h"

static const uint64_t readonly_word __attribute__((aligned(4096))) =
    UINT64_C(0x9a25bc78d136ef40);

int EAPP_ENTRY main(void)
{
    struct multimem_result result;
    uintptr_t reply;
    unsigned int i;
    uint64_t start;
    uint64_t now;

    for (i = 0; i < 3; i++) {
	__asm__ volatile("rdtime %0" : "=r"(start));
	do {
	    __asm__ volatile("rdtime %0" : "=r"(now));
	} while (now - start < 400000);
	if (SYSCALL_2(1098, &result, &readonly_word)) {
	    EAPP_RETURN(1);
	}
	reply = 99;
	if (ocall(1, &result, sizeof result, &reply, sizeof reply) || reply) {
	    EAPP_RETURN(2);
	}
    }
    if (*(volatile const uint64_t *)&readonly_word !=
	UINT64_C(0x9a25bc78d136ef40)) {
	EAPP_RETURN(3);
    }
    EAPP_RETURN(0);
}
