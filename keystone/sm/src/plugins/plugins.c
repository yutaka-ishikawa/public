#include "plugins.h"
#include "cpu.h"

#ifdef PLUGIN_ENABLE_MULTIMEM
#include "multimem.h"
#endif

uintptr_t call_plugin(enclave_id id, uintptr_t plugin_id, uintptr_t call_id,
		      uintptr_t arg0, uintptr_t arg1)
{
    if (!cpu_is_enclave_context()) {
	return SBI_ERR_SM_ENCLAVE_SBI_PROHIBITED;
    }
    if (arg1) {
	return SBI_ERR_SM_ENCLAVE_ILLEGAL_ARGUMENT;
    }
    switch (plugin_id) {
#ifdef PLUGIN_ENABLE_MULTIMEM
    case PLUGIN_ID_MULTIMEM:
	return do_sbi_multimem(id, call_id, arg0);
#endif
    default:
	return SBI_ERR_SM_NOT_IMPLEMENTED;
    }
}
