#include "../common/harness.c"

static void verify_host_unchanged(unsigned long id)
{
    int i;

    assert(enclaves[id].state == FRESH && enclaves[id].n_thread == 0);
    assert(!enclave_context && vector_calls == 0 && csrs[satp] == 0x9876 &&
	   csrs[mideleg] == 0xffff);
    assert(!enclaves[id].threads[0].prev_mepc &&
	   os_permissions == PMP_ALL_PERM);
    for (i = 0; i < ENCLAVE_REGIONS_MAX; i++) {
	if (enclaves[id].regions[i].type != REGION_INVALID) {
	    assert(local_permissions[enclaves[id].regions[i].pmp_rid] ==
		   PMP_NO_PERM);
	}
    }
}

int main(void)
{
    struct sbi_trap_regs r;
    unsigned long id;
    int fail;

    reset_fixture();
    id = create_fixture();
    csrs[satp] = 0x9876;
    csrs[mideleg] = 0xffff;
    csrs[sstatus] = 0x111;
    r = invoke(2003, id);
    assert(r.a0 == 0 && r.a1 == (uintptr_t)epm && r.a2 == sizeof epm);
    assert(r.a3 == args.runtime_paddr && r.a4 == args.user_paddr &&
	   r.a5 == args.free_paddr);
    assert(r.a6 == (uintptr_t)utm && r.a7 == sizeof utm &&
	   r.mepc == (uintptr_t)epm);
    assert(r.mstatus == (1UL << MSTATUS_MPP_SHIFT));
    assert(csrs[satp] == 0 && csrs[mideleg] == 0);
    assert(enclaves[id].state == RUNNING && enclaves[id].n_thread == 1);
    assert(enclaves[id].threads[0].prev_mepc == 0x8000 &&
	   enclaves[id].threads[0].prev_state.ra == 0xabc);
    assert(enclaves[id].threads[0].prev_csrs.satp == 0x9876);
    assert(enclave_context && context_eid == (int)id && vector_calls == 1 &&
	   local_pmp_calls == 2);

    CASE("RUN success preserves entry ABI and saves host context after PMP "
	 "preparation");

    enclave_context = 0;
    r = invoke(2003, id);
    normal_return(r, SBI_ERR_SM_ENCLAVE_NOT_FRESH);
    assert(enclaves[id].n_thread == 1);

    CASE("second RUN rejected without thread increment");

    reset_fixture();
    id = create_fixture();
    enclaves[id].state = STOPPED;
    r = invoke(2003, id);
    normal_return(r, SBI_ERR_SM_ENCLAVE_NOT_FRESH);

    CASE("STOPPED requires RESUME 2005, not RUN");

    reset_fixture();
    r = invoke(2003, 0);
    normal_return(r, SBI_ERR_SM_ENCLAVE_NOT_FRESH);
    r = invoke(2003, ENCL_MAX);
    normal_return(r, SBI_ERR_SM_ENCLAVE_INVALID_ID);
    r = invoke(2003, UINT_MAX);
    normal_return(r, SBI_ERR_SM_ENCLAVE_INVALID_ID);

    CASE("invalid/unallocated/out-of-range IDs");

    reset_fixture();
    id = create_fixture();
    enclave_context = 1;
    r = invoke(2003, id);
    normal_return(r, SBI_ERR_SM_ENCLAVE_SBI_PROHIBITED);
    assert(enclaves[id].state == FRESH);

    CASE("enclave context cannot invoke host RUN");

    reset_fixture();
    id = create_fixture();
    ready = 0;
    r = invoke(2003, id);
    normal_return(r, SBI_ERR_SM_NOT_IMPLEMENTED);
    assert(enclaves[id].state == FRESH);

    CASE("unready adapter rejected before state mutation");

    for (fail = 1; fail <= 2; fail++) {
	reset_fixture();
	id = create_fixture();
	local_pmp_fail = fail;
	csrs[satp] = 0x9876;
	csrs[mideleg] = 0xffff;
	r = invoke(2003, id);
	normal_return(r, SBI_ERR_SM_ENCLAVE_PMP_FAILURE);
	verify_host_unchanged(id);
	assert(r.ra == 0xabc && r.sp == 0x123400 && r.mstatus == 0x1880);
	r = invoke(2003, id);
	assert(!r.a0 && enclaves[id].state == RUNNING && enclave_context);
    }

    CASE("EPM/UTM failure restores host/PMP/state and permits a successful "
	 "RUN retry");

    reset_fixture();
    id = create_fixture();
    r = invoke(2003, (1UL << 32) | id);
    normal_return(r, SBI_ERR_SM_ENCLAVE_INVALID_ID);
    assert(enclaves[id].state == FRESH && vector_calls == 0);

    CASE("64-bit EID cannot alias a valid enclave");

    reset_fixture();
    id = create_fixture();
    osm_fail_call = 1;
    csrs[satp] = 0x9876;
    csrs[mideleg] = 0xffff;
    r = invoke(2003, id);
    normal_return(r, SBI_ERR_SM_ENCLAVE_PMP_FAILURE);
    verify_host_unchanged(id);

    CASE("OS permission setup failure is reported with successful host "
	 "rollback");

    reset_fixture();
    id = create_fixture();
    local_pmp_fail = 2;
    rollback_fail = 1;
    hang_expected = 1;
    if (!setjmp(hang_target)) {
	(void)invoke(2003, id);
	assert(!"must not return when rollback fails");
    }
    assert(!enclave_context && vector_calls == 0);

    CASE("failed permission revocation halts before any S-mode return");

    reset_fixture();
    id = create_fixture();
    local_pmp_fail = 1;
    osm_fail_call = 2;
    hang_expected = 1;
    if (!setjmp(hang_target)) {
	(void)invoke(2003, id);
	assert(!"must not return when OS restore fails");
    }
    assert(!enclave_context && vector_calls == 0);

    CASE("failed OS permission restore halts before any S-mode return");

    reset_fixture();
    id = create_fixture();
    enclaves[id].state = STOPPED;
    local_pmp_fail = 1;
    r = invoke(2005, id);
    normal_return(r, SBI_ERR_SM_ENCLAVE_PMP_FAILURE);
    assert(enclaves[id].state == STOPPED && enclaves[id].n_thread == 0 &&
	   !enclave_context);

    CASE("shared RESUME switch failure restores original STOPPED state");

    reset_fixture();
    r = invoke(2005, 1UL << 32);
    normal_return(r, SBI_ERR_SM_ENCLAVE_INVALID_ID);

    CASE("shared RESUME helper also checks full-width EID");

    reset_fixture();
    id = create_fixture();
    r = invoke(2003, id);
    assert(!r.a0);
    r = invoke(SBI_SM_STOP_ENCLAVE, STOP_TIMER_INTERRUPT);
    assert(r.a0 == SBI_ERR_SM_ENCLAVE_INTERRUPTED && r.mepc == 0x8004 &&
	   r.a1 == 0xdead);
    assert(enclaves[id].state == STOPPED && !enclave_context);
    r = invoke(2005, id);
    assert(!r.a0 && enclave_context && enclaves[id].state == RUNNING);
    r = invoke(SBI_SM_EXIT_ENCLAVE, 17);
    assert(r.a0 == 0 && r.a1 == 17 && r.mepc == 0x8004);
    assert(enclaves[id].state == STOPPED && !enclave_context &&
	   host_vector_calls == 2);

    CASE("actual RUN -> timer STOP -> RESUME -> EXIT preserves state and "
	 "return ABI");

    reset_fixture();
    id = create_fixture();
    r = invoke(2003, id);
    rollback_fail = 1;
    hang_expected = 1;
    if (!setjmp(hang_target)) {
	(void)invoke(SBI_SM_EXIT_ENCLAVE, 0);
	assert(!"failed host revocation must halt");
    }
    assert(enclave_context && host_vector_calls == 0);

    CASE("shared host return halts when enclave permission revocation "
	 "fails");

    reset_fixture();
    id = create_fixture();
    r = invoke(2003, id);
    osm_fail_call = osm_calls + 1;
    hang_expected = 1;
    if (!setjmp(hang_target)) {
	(void)invoke(SBI_SM_EXIT_ENCLAVE, 0);
	assert(!"failed host OS mapping must halt");
    }
    assert(enclave_context && host_vector_calls == 0);

    CASE("shared host return halts when OS mapping restore fails");

    printf("SUMMARY SBI 2003: %d groups completed, %d known findings "
	   "reproduced (hardware mocked)\n",
	   cases, findings);
    return 0;
}
