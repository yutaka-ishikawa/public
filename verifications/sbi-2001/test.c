#include "../common/harness.c"

int main(void)
{
	struct sbi_trap_regs r;
	unsigned long id;
	unsigned long out;
	size_t offset;
	int i;
	int variation;
	int fail;

	reset_fixture();
	id = create_fixture();
	assert(enclaves[id].state == FRESH && enclaves[id].n_thread == 0);
	assert(active_regions == 2 && global_calls == 1 &&
	       platform_calls == 1 && hash_calls == 1);
	assert(!strcmp(operations, "AAGBH"));
	assert(enclaves[id].params.free_requested == args.free_requested);
	for (offset = 0; offset < sizeof utm; offset++)
		assert(utm[offset] == 0);
	for (offset = 0; offset < sizeof epm; offset++)
		assert(epm[offset] == 0x5a);

	CASE("CREATE success: EID, FRESH, EPM/UTM, zero UTM, measure order, "
	     "PC+4");

	reset_fixture();
	ready = 0;
	r = invoke(2001, (uintptr_t)&args);
	normal_return(r, SBI_ERR_SM_NOT_IMPLEMENTED);
	assert(init_calls == 0);

	CASE("adapter rejects unready SM");

	out = 99;
	assert(sbi_sm_create_enclave(&out, (uintptr_t)&args) ==
	       SBI_ERR_SM_ENCLAVE_NOT_INITIALIZED);
	assert(out == 99);

	CASE("helper readiness gate");

	reset_fixture();
	enclave_context = 1;
	r = invoke(2001, (uintptr_t)&args);
	normal_return(r, SBI_ERR_SM_ENCLAVE_SBI_PROHIBITED);
	assert(init_calls == 0);

	CASE("enclave cannot call host CREATE");

	reset_fixture();
	copy_fail = 1;
	r = invoke(2001, (uintptr_t)&args);
	normal_return(r, SBI_ERR_SM_ENCLAVE_REGION_OVERLAPS);
	no_allocated_eids();

	CASE("argument copy failure has no allocation");

	for (variation = 0; variation < 8; variation++) {
		reset_fixture();
		switch (variation) {
		case 0:
			args.epm_region.size = 0;
			break;
		case 1:
			args.epm_region.paddr++;
			break;
		case 2:
			args.utm_region.paddr = args.epm_region.paddr;
			break;
		case 3:
			args.epm_region.paddr = UINTPTR_MAX - 4095;
			args.epm_region.size = 8192;
			break;
		case 4:
			args.user_paddr = args.runtime_paddr - 1;
			break;
		case 5:
			args.free_paddr =
				args.epm_region.paddr + sizeof epm + 1;
			break;
		case 6:
			args.epm_region.paddr = 0x100000000UL;
			args.runtime_paddr = args.epm_region.paddr;
			args.user_paddr = args.runtime_paddr;
			args.free_paddr = args.runtime_paddr;
			break;
		case 7:
			firmware_fail = 1;
			break;
		}
		r = invoke(2001, (uintptr_t)&args);
		normal_return(r, SBI_ERR_SM_ENCLAVE_ILLEGAL_ARGUMENT);
		assert(init_calls == 0);
		no_allocated_eids();
	}

	CASE("8 invalid layouts/firmware range cases rejected before EID/PMP "
	     "changes");

	reset_fixture();
	args.free_paddr = (uintptr_t)epm + sizeof epm;
	create_fixture();

	CASE("free_paddr exactly EPM end accepted");

	reset_fixture();
	for (i = 0; i < ENCL_MAX; i++)
		enclaves[i].state = FRESH;
	r = invoke(2001, (uintptr_t)&args);
	normal_return(r, SBI_ERR_SM_ENCLAVE_NO_FREE_RESOURCE);
	assert(init_calls == 0);

	CASE("EID exhaustion");

	for (fail = 1; fail <= 2; fail++) {
		reset_fixture();
		fail_init_call = fail;
		r = invoke(2001, (uintptr_t)&args);
		normal_return(r, SBI_ERR_SM_ENCLAVE_PMP_FAILURE);
		assert(active_regions == 0 && free_calls == fail - 1);
		no_allocated_eids();
	}

	CASE("EPM and UTM allocation failures roll back resources");

	reset_fixture();
	global_fail = -1;
	r = invoke(2001, (uintptr_t)&args);
	normal_return(r, SBI_ERR_SM_ENCLAVE_PMP_FAILURE);
	assert(active_regions == 0 && platform_calls == 0 && hash_calls == 0);
	no_allocated_eids();

	CASE("global PMP failure prevents platform/hash and frees resources");

	reset_fixture();
	platform_fail = SBI_ERR_SM_ENCLAVE_UNKNOWN_ERROR;
	r = invoke(2001, (uintptr_t)&args);
	normal_return(r, SBI_ERR_SM_ENCLAVE_UNKNOWN_ERROR);
	assert(active_regions == 0 && unset_calls == 1 && hash_calls == 0);
	no_allocated_eids();

	CASE("platform failure unsets PMP and releases resources");

	reset_fixture();
	hash_fail = SBI_ERR_SM_ENCLAVE_ILLEGAL_PTE;
	r = invoke(2001, (uintptr_t)&args);
	normal_return(r, SBI_ERR_SM_ENCLAVE_ILLEGAL_PTE);
	assert(active_regions == 0 && unset_calls == 1 && destroy_calls == 1);
	no_allocated_eids();

	CASE("hash failure destroys platform state and rolls back");

	printf("SUMMARY SBI 2001: %d groups completed, %d known findings "
	       "reproduced (hardware mocked)\n",
	       cases, findings);
	return 0;
}
