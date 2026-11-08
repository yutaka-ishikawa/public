#include "../common/harness.c"

_Static_assert(SBI_SM_RESUME_ENCLAVE == 2005, "RESUME must be function 2005");

static unsigned long stopped_fixture(int interrupted, enclave_state state)
{
    unsigned long id;
    struct thread_state *thread;
    uintptr_t *registers;
    int reg;

    reset_fixture();
    id = create_fixture();
    enclaves[id].state = state;
    thread = &enclaves[id].threads[0];
    registers = (uintptr_t *)&thread->prev_state;
    for (reg = 1; reg < 32; reg++) {
	registers[reg] = 0x1000 + reg;
    }
    thread->prev_state.slot = interrupted;
    thread->prev_state.ra = 0x110;
    thread->prev_state.sp = 0x220;
    thread->prev_state.a0 = 0x330;
    thread->prev_state.a1 = 0x441;
    thread->prev_state.a2 = 0x442;
    thread->prev_state.a3 = 0x443;
    thread->prev_state.a4 = 0x444;
    thread->prev_state.a5 = 0x445;
    thread->prev_state.a6 = 0x446;
    thread->prev_state.a7 = 0x447;
    thread->prev_mepc = 0x9000 - 4;
    thread->prev_mstatus = (1UL << MSTATUS_MPP_SHIFT) | MSTATUS_SPIE;
    thread->prev_csrs.satp = 0x8000000000123400UL;
    thread->prev_csrs.stvec = 0x9900;
    thread->prev_csrs.sscratch = 0x9910;
    thread->prev_csrs.sie = 0x9920;
    thread->prev_csrs.sstatus = 0x9930;
    thread->prev_csrs.scounteren = 0x9940;
    thread->prev_csrs.sepc = 0x9950;
    thread->prev_csrs.scause = 0x9960;
    thread->prev_csrs.stval = 0x9970;
    thread->prev_csrs.sip = 0x9980;
    csrs[satp] = 0x8000000000567800UL;
    csrs[mideleg] = 0xffff;
    return id;
}

static void host_preserved(unsigned long id, enclave_state state,
			   const struct thread_state *snapshot)
{
    int i;

    assert(enclaves[id].state == state && enclaves[id].n_thread == 0);
    assert(!enclave_context && vector_calls == 0);
    assert(csrs[satp] == 0x8000000000567800UL && csrs[mideleg] == 0xffff);
    assert(!memcmp(snapshot, &enclaves[id].threads[0], sizeof(*snapshot)));
    assert(os_permissions == PMP_ALL_PERM);
    for (i = 0; i < ENCLAVE_REGIONS_MAX; i++) {
	if (enclaves[id].regions[i].type != REGION_INVALID) {
	    assert(local_permissions[enclaves[id].regions[i].pmp_rid] ==
		   PMP_NO_PERM);
	}
    }
}

int main(void)
{
    static const enclave_state rejected[] = {INVALID, DESTROYING, ALLOCATED,
					     FRESH};
    struct sbi_trap_regs r;
    struct thread_state snapshot;
    struct sbi_trap_regs interrupted;
    unsigned long id;
    unsigned long bad;
    size_t i;
    int fail;
    int state;

    id = stopped_fixture(0, STOPPED);
    snapshot = enclaves[id].threads[0];
    r = invoke(2005, id);
    assert(r.a0 == 0 && r.a1 == 0x441 && r.a2 == 0x442 && r.a3 == 0x443);
    assert(r.a4 == 0x444 && r.a5 == 0x445 && r.a6 == 0x446 && r.a7 == 0x447);
    assert(r.ra == 0x110 && r.sp == 0x220 && r.mepc == 0x9000);
    for (i = 1; i < 32; i++) {
	if (i != 10) {
	    assert(((uintptr_t *)&r)[i] ==
		   ((uintptr_t *)&snapshot.prev_state)[i]);
	}
    }
    assert(r.mstatus ==
	   ((0x1880 & ~(MSTATUS_SIE | MSTATUS_SPIE | MSTATUS_SPP | MSTATUS_MPP |
			MSTATUS_FS | MSTATUS_SUM | MSTATUS_MXR)) |
	    (1UL << MSTATUS_MPP_SHIFT) | MSTATUS_SPIE));
    assert(csrs[satp] == 0x8000000000123400UL && csrs[stvec] == 0x9900);
    assert(csrs[sscratch] == 0x9910 && csrs[sie] == 0x9920 &&
	   csrs[mideleg] == 0);
    assert(csrs[sstatus] == 0x9930 && csrs[scounteren] == 0x9940);
    assert(csrs[sepc] == 0x9950 && csrs[scause] == 0x9960);
    assert(csrs[stval] == 0x9970 && csrs[sip] == 0x9980);
    assert(enclaves[id].state == RUNNING && enclaves[id].n_thread == 1);
    assert(enclave_context && context_eid == (int)id);
    assert(enclaves[id].threads[0].prev_mepc == 0x8000);
    assert(enclaves[id].threads[0].prev_state.ra == 0xabc);
    CASE("STOPPED resume restores saved registers/CSRs and exact PC, not RUN "
	 "boot parameters");

    id = stopped_fixture(1, STOPPED);
    r = invoke(2005, id);
    assert(r.zero == 1 && r.a0 == 0x330 && r.mepc == 0x9000);
    CASE("timer interruption marker preserves saved a0; PC bias adjusted "
	 "exactly once");

    id = stopped_fixture(0, STOPPED);
    r = invoke(2005, id);
    assert(r.zero == 0 && r.a0 == 0 && r.a1 == 0x441);
    CASE("OCALL marker returns success in a0 without overwriting other "
	 "restored registers");

    for (i = 0; i < sizeof(rejected) / sizeof(rejected[0]); i++) {
	id = stopped_fixture(0, rejected[i]);
	snapshot = enclaves[id].threads[0];
	r = invoke(2005, id);
	normal_return(r, SBI_ERR_SM_ENCLAVE_NOT_RESUMABLE);
	assert(enclaves[id].state == rejected[i] && local_pmp_calls == 0);
	assert(!memcmp(&snapshot, &enclaves[id].threads[0], sizeof(snapshot)));
    }
    CASE("INVALID/DESTROYING/ALLOCATED/FRESH states cannot resume");

    for (state = STOPPED; state <= RUNNING; state++) {
	id = stopped_fixture(0, state);
	enclaves[id].n_thread = MAX_ENCL_THREADS;
	r = invoke(2005, id);
	normal_return(r, SBI_ERR_SM_ENCLAVE_NOT_RESUMABLE);
	assert(enclaves[id].n_thread == MAX_ENCL_THREADS &&
	       local_pmp_calls == 0);
    }
    CASE("STOPPED/RUNNING with full thread count rejected without extra "
	 "reservation");

    id = stopped_fixture(0, RUNNING);
    r = invoke(2005, id);
    assert(!r.a0 && enclaves[id].state == RUNNING &&
	   enclaves[id].n_thread == 1);
    CASE("source predicate accepts synthetic RUNNING with zero active threads");

    for (i = 0; i < 4; i++) {
	id = stopped_fixture(0, STOPPED);
	bad = i == 0   ? ENCL_MAX
	      : i == 1 ? UINT_MAX
	      : i == 2 ? ULONG_MAX
		       : (1UL << 32) | id;
	r = invoke(2005, bad);
	normal_return(r, SBI_ERR_SM_ENCLAVE_INVALID_ID);
	assert(enclaves[id].state == STOPPED && local_pmp_calls == 0);
    }
    CASE("full-width EID bounds reject all aliases before 32-bit conversion");

    id = stopped_fixture(0, STOPPED);
    enclave_context = 1;
    r = invoke(2005, id);
    normal_return(r, SBI_ERR_SM_ENCLAVE_SBI_PROHIBITED);
    assert(enclaves[id].state == STOPPED && !enclaves[id].n_thread);
    CASE("enclave context cannot issue host RESUME");

    id = stopped_fixture(0, STOPPED);
    ready = 0;
    r = invoke(2005, id);
    normal_return(r, SBI_ERR_SM_NOT_IMPLEMENTED);
    assert(enclaves[id].state == STOPPED && local_pmp_calls == 0);
    CASE("unready SM rejects RESUME before state changes");

    for (fail = 1; fail <= 2; fail++) {
	id = stopped_fixture(0, STOPPED);
	snapshot = enclaves[id].threads[0];
	local_pmp_fail = fail;
	r = invoke(2005, id);
	normal_return(r, SBI_ERR_SM_ENCLAVE_PMP_FAILURE);
	host_preserved(id, STOPPED, &snapshot);
	r = invoke(2005, id);
	assert(!r.a0 && enclave_context && enclaves[id].n_thread == 1);
    }
    CASE("EPM/UTM setup failure restores STOPPED context/permissions and "
	 "allows retry");

    id = stopped_fixture(0, RUNNING);
    snapshot = enclaves[id].threads[0];
    local_pmp_fail = 1;
    r = invoke(2005, id);
    normal_return(r, SBI_ERR_SM_ENCLAVE_PMP_FAILURE);
    host_preserved(id, RUNNING, &snapshot);
    CASE("rollback restores original RUNNING state, not unconditionally "
	 "STOPPED");

    id = stopped_fixture(0, STOPPED);
    snapshot = enclaves[id].threads[0];
    osm_fail_call = 1;
    r = invoke(2005, id);
    normal_return(r, SBI_ERR_SM_ENCLAVE_PMP_FAILURE);
    host_preserved(id, STOPPED, &snapshot);
    CASE("OS PMP setup failure restores saved context and thread reservation");

    id = stopped_fixture(0, STOPPED);
    local_pmp_fail = 2;
    rollback_fail = 1;
    hang_expected = 1;
    if (!setjmp(hang_target)) {
	(void)invoke(2005, id);
	assert(!"rollback failure must not return");
    }
    assert(!enclave_context && vector_calls == 0);
    CASE("failed permission rollback halts before S-mode return");

    id = stopped_fixture(0, STOPPED);
    local_pmp_fail = 1;
    osm_fail_call = 2;
    hang_expected = 1;
    if (!setjmp(hang_target)) {
	(void)invoke(2005, id);
	assert(!"OS restore failure must not return");
    }
    assert(!enclave_context && vector_calls == 0);
    CASE("failed OS mapping restore halts before S-mode return");

    id = stopped_fixture(0, STOPPED);
    guard_fail = 1;
    hang_expected = 1;
    if (!setjmp(hang_target)) {
	(void)invoke(2005, id);
	assert(!"PMP/HSM guard failure must halt");
    }
    assert(!enclave_context && local_pmp_calls == 0 && vector_calls == 0);
    CASE("PMP/HSM guard prevents any context/permission switch");

    reset_fixture();
    id = create_fixture();
    interrupted = invoke(2003, id);
    interrupted.a0 = 0xabc123;
    interrupted.a1 = 0xabc456;
    interrupted.mepc = 0xa000 - 4;
    assert(!sbi_sm_stop_enclave(&interrupted, STOP_TIMER_INTERRUPT));
    assert(interrupted.a0 == SBI_ERR_SM_ENCLAVE_INTERRUPTED);
    r = invoke(2005, id);
    assert(r.a0 == 0xabc123 && r.a1 == 0xabc456 && r.mepc == 0xa000);
    CASE("actual timer-reason STOP helper and RESUME preserve interrupted a0 "
	 "and PC");

    reset_fixture();
    id = create_fixture();
    r = invoke(2003, id);
    assert(!r.a0);
    for (i = 0; i < 100; i++) {
	op_count = 0;
	operations[0] = 0;
	utm[128] = (unsigned char)i;
	r = invoke(SBI_SM_STOP_ENCLAVE, STOP_EDGE_CALL_HOST);
	assert(r.a0 == SBI_ERR_SM_ENCLAVE_EDGE_CALL_HOST && !enclave_context);
	utm[128] ^= 0xa5;
	r = invoke(2005, id);
	assert(!r.a0 && enclave_context && enclaves[id].n_thread == 1);
	assert(utm[128] == (unsigned char)(i ^ 0xa5));
    }
    r = invoke(SBI_SM_EXIT_ENCLAVE, 23);
    assert(!r.a0 && r.a1 == 23 && enclaves[id].state == STOPPED);
    r = invoke(2002, id);
    normal_return(r, 0);
    assert(active_regions == 0);
    CASE("100 OCALL STOP/RESUME cycles preserve shared data and release "
	 "resources after EXIT");

    printf("SUMMARY SBI 2005: %d groups completed, %d known findings "
	   "reproduced (hardware mocked)\n",
	   cases, findings);
    return 0;
}
