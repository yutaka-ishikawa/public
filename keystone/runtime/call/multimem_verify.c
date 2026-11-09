/* Built only in the explicit MULTIMEM_VERIFY runtime. */
#include <stdint.h>

#include "call/sbi.h"
#include "edge_call.h"
#include "mm/vm.h"
#include "multimem_protocol.h"
#include "sm_err.h"
#include "uaccess.h"

static uintptr_t probe(uintptr_t plugin, uintptr_t call, uintptr_t output,
		       uintptr_t reserved)
{
    register uintptr_t a0 __asm__("a0") = plugin;
    register uintptr_t a1 __asm__("a1") = call;
    register uintptr_t a2 __asm__("a2") = output;
    register uintptr_t a3 __asm__("a3") = reserved;
    register uintptr_t a6 __asm__("a6") = SBI_SM_CALL_PLUGIN;
    register uintptr_t a7 __asm__("a7") = SBI_EXT_EXPERIMENTAL_KEYSTONE_ENCLAVE;

    __asm__ volatile("ecall"
		     : "+r"(a0), "+r"(a1)
		     : "r"(a2), "r"(a3), "r"(a6), "r"(a7)
		     : "memory");
    return a0;
}

uintptr_t multimem_verify(uintptr_t output, uintptr_t readonly)
{
    struct multimem_result result = {0};
    uintptr_t canary;

    canary = 0x5a5a;
    result.size_status = sbi_query_multimem(&result.size);
    result.addr_status = sbi_query_multimem_addr(&result.addr);
    result.status[0] = probe(99, 1, (uintptr_t)&canary, 0);
    result.status[1] = probe(1, 99, (uintptr_t)&canary, 0);
    result.status[2] = probe(1, 1, 0, 0);
    result.status[3] = probe(1, 1, 1, 0);
    result.status[4] = probe(1, 1, UINTPTR_MAX - 3, 0);
    result.status[5] = probe(1, 1, (uintptr_t)shared_buffer, 0);
    result.status[6] = probe(1, 1, readonly, 0);
    result.status[7] = probe(1, 1, (uintptr_t)&canary, 1);
    if (canary != 0x5a5a) {
	return 1;
    }
    return copy_to_user((void *)output, &result, sizeof result);
}
