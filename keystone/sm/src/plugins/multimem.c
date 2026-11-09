#include "plugins/multimem.h"
#include "sm.h"

uintptr_t do_sbi_multimem(enclave_id eid, uintptr_t call_id, uintptr_t arg0)
{
    return multimem_query(eid, call_id, arg0);
}
