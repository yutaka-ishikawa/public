/* Host test boundary: production C state machines above mocked hardware. */
#ifdef NDEBUG
#error "Verification requires enabled assertions; do not use -DNDEBUG"
#endif
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <setjmp.h>
#define __riscv_xlen 64
#define KEYSTONE_K3_PMP_N_REG 16
#define TARGET_PLATFORM_HEADER "platform/spacemit_k3/platform.h"
#include "enclave.h"
#include "cpu.h"
#include "sbi/sbi_ecall_interface.h"
#include "sbi/sbi_error.h"
#include "platform/spacemit_k3/k3_enclave_create_preflight.h"
_Static_assert(sizeof(unsigned long) == 8, "Run on a 64-bit host");
#define SM_TRACE_STAGE(...) ((void)0)
#define SM_TRACE_RESULT(...) ((void)0)
#define SM_TRACE_VALUES(...) ((void)0)
#define sbi_printf(...) ((void)0)
#define sbi_memset memset
#define RISCV_PGSHIFT 12
#define ENCLAVE_EXISTS(eid) ((eid) < ENCL_MAX && enclaves[(eid)].state >= 0)

typedef int spinlock_t;
static spinlock_t encl_lock;

static void spin_lock(spinlock_t *lock)
{
    assert(*lock == 0);
    *lock = 1;
}

static void spin_unlock(spinlock_t *lock)
{
    assert(*lock == 1);
    *lock = 0;
}
struct enclave enclaves[ENCL_MAX];
static _Alignas(4096) unsigned char epm[16384], utm[4096];
static struct keystone_sbi_create_t args;
static int ready, enclave_context, context_eid, copy_fail, firmware_fail;
static int init_calls, fail_init_call, active_regions, global_calls,
    global_fail;
static int unset_calls, free_calls, platform_calls, platform_fail, hash_calls,
    hash_fail;
static int destroy_calls, vector_calls, guard_fail, local_pmp_calls,
    local_pmp_fail;
static int osm_calls, osm_fail_call, rollback_fail, failed_once, free_fail_call;
static int nested_destroy, hang_expected, host_vector_calls;
static unsigned long nested_result;
static jmp_buf hang_target;
static int fail_unset, unset_fail_call, cases, findings;
static int region_used[64];
static uintptr_t region_base[64];
static uint64_t region_size[64];
static int local_permissions[64], os_permissions;
static char operations[256];
static size_t op_count;

static void op(char c)
{
    assert(op_count + 1 < sizeof operations);
    operations[op_count++] = c;
    operations[op_count] = 0;
}

int sm_is_ready(void)
{
    return ready;
}

int cpu_is_enclave_context(void)
{
    return enclave_context;
}

int cpu_get_enclave_id(void)
{
    return context_eid;
}

void cpu_enter_enclave_context(enclave_id id)
{
    enclave_context = 1;
    context_eid = (int)id;
    op('C');
}

void cpu_exit_enclave_context(void)
{
    enclave_context = 0;
    op('c');
}

static int copy_to_sm(void *dest, uintptr_t src, size_t size)
{
    if (copy_fail || src != (uintptr_t)&args || size != sizeof args)
	return 1;
    memcpy(dest, (void *)src, size);
    return 0;
}

int k3_sm_firmware_range(uintptr_t *base, size_t *size)
{
    *base = 0x100000000UL;
    *size = 0x100000;
    return firmware_fail;
}

int pmp_region_init_atomic(uintptr_t base, uint64_t size, enum pmp_priority pri,
			   region_id *id, int overlap)
{
    int i;

    init_calls++;
    op('A');
    if (init_calls == fail_init_call)
	return -1;
    for (i = 0; i < 64; i++)
	if (!region_used[i]) {
	    region_used[i] = 1;
	    region_base[i] = base;
	    region_size[i] = size;
	    *id = i;
	    active_regions++;
	    return 0;
	}
    return -1;
}

int pmp_region_free_atomic(region_id id)
{
    assert(id >= 0 && id < 64 && region_used[id]);
    free_calls++;
    op('F');
    if (free_calls == free_fail_call)
	return -1;
    region_used[id] = 0;
    active_regions--;
    return 0;
}

int pmp_set_global(region_id id, uint8_t permissions)
{
    assert(region_used[id] && permissions == PMP_NO_PERM);
    global_calls++;
    op('G');
    return global_fail;
}

int pmp_unset_global(region_id id)
{
    assert(region_used[id]);
    unset_calls++;
    op('U');
    return unset_calls == unset_fail_call ? -1 : fail_unset;
}

uintptr_t pmp_region_get_addr(region_id id)
{
    assert(region_used[id]);
    return region_base[id];
}

uint64_t pmp_region_get_size(region_id id)
{
    assert(region_used[id]);
    return region_size[id];
}

int pmp_set_keystone(region_id id, uint8_t permissions)
{
    assert(region_used[id]);
    local_pmp_calls++;
    op(permissions == PMP_ALL_PERM ? 'P' : 'p');
    if (permissions == PMP_ALL_PERM && local_pmp_calls == local_pmp_fail) {
	failed_once = 1;
	local_permissions[id] = permissions;
	return -1;
    }
    if (permissions == PMP_NO_PERM && rollback_fail)
	return -1;
    local_permissions[id] = permissions;
    return 0;
}

int osm_pmp_set(uint8_t perm)
{
    osm_calls++;
    op(perm == PMP_NO_PERM ? 'O' : 'o');
    if (osm_calls == osm_fail_call) {
	os_permissions = perm;
	return -1;
    }
    os_permissions = perm;
    return 0;
}

static int k3_keystone_pmp_assert_active(void)
{
    return guard_fail;
}

static void __attribute__((noreturn)) sbi_hart_hang(void)
{
    if (hang_expected)
	longjmp(hang_target, 1);
    abort();
}

void platform_init_enclave(struct enclave *e)
{
    (void)e;
}

unsigned long platform_create_enclave(struct enclave *e)
{
    assert(e->state == ALLOCATED);
    platform_calls++;
    op('B');
    return platform_fail;
}

void platform_destroy_enclave(struct enclave *e)
{
    assert(e->state == DESTROYING || e->state == ALLOCATED);
    destroy_calls++;
    op('D');
    if (nested_destroy) {
	nested_destroy = 0;
	nested_result = destroy_enclave(e->eid);
    }
}

unsigned long validate_and_hash_enclave(struct enclave *e)
{
    hash_calls++;
    op('H');
    memset(e->hash, 0x42, sizeof e->hash);
    return hash_fail;
}

void platform_switch_to_enclave(struct enclave *e)
{
    assert(e->state == RUNNING);
    op('T');
}

void switch_vector_enclave(void)
{
    vector_calls++;
    op('V');
}

void switch_vector_host(void)
{
    host_vector_calls++;
    op('v');
}

void platform_switch_from_enclave(struct enclave *e)
{
    op('t');
}

/* Actual thread swap helpers run against a software CSR bank. */
enum {
    sstatus,
    sie,
    stvec,
    scounteren,
    sscratch,
    sepc,
    scause,
    stval,
    sip,
    satp,
    mideleg,
    mip,
    csr_count
};

static uintptr_t csrs[csr_count];
#define csr_read(name) (csrs[name])
#define csr_write(name, value) (csrs[name] = (uintptr_t)(value))
#define csr_clear(name, value) (csrs[name] &= ~(uintptr_t)(value))
#define csr_set(name, value) (csrs[name] |= (uintptr_t)(value))

/* Non-target SBI bodies are unreachable in these tests. */
unsigned long attest_enclave(uintptr_t a, uintptr_t b, uintptr_t c,
			     enclave_id id)
{
    abort();
}

unsigned long get_sealing_key(uintptr_t a, uintptr_t b, size_t c, enclave_id id)
{
    abort();
}

static unsigned long call_plugin(int id, uintptr_t a, uintptr_t b, uintptr_t c,
				 uintptr_t d)
{
    abort();
}

struct sbi_trap_context {
    struct sbi_trap_regs regs;
};

struct sbi_ecall_return {
    unsigned long value;
    bool skip_regs_update;
};

struct sbi_ecall_extension {
    int (*handle)(unsigned long, unsigned long, struct sbi_trap_regs *,
		  struct sbi_ecall_return *);
};
static struct sbi_ecall_extension *sbi_ecall_find_extension(unsigned long id);
#include "production.inc"

static struct sbi_ecall_extension *sbi_ecall_find_extension(unsigned long id)
{
    static struct sbi_ecall_extension ext = {
	.handle = sbi_ecall_keystone_enclave_handler};
    return id == SBI_EXT_EXPERIMENTAL_KEYSTONE_ENCLAVE ? &ext : NULL;
}

static void reset_fixture(void)
{
    ready = 1;
    enclave_context = 0;
    context_eid = 0;
    copy_fail = 0;
    firmware_fail = 0;
    init_calls = 0;
    fail_init_call = 0;
    active_regions = 0;
    global_calls = 0;
    global_fail = 0;
    unset_calls = 0;
    free_calls = 0;
    platform_calls = 0;
    platform_fail = 0;
    hash_calls = 0;
    hash_fail = 0;
    destroy_calls = 0;
    vector_calls = 0;
    guard_fail = 0;
    local_pmp_calls = 0;
    local_pmp_fail = 0;
    fail_unset = 0;
    unset_fail_call = 0;
    osm_calls = 0;
    osm_fail_call = 0;
    rollback_fail = 0;
    failed_once = 0;
    free_fail_call = 0;
    nested_destroy = 0;
    nested_result = 0;
    hang_expected = 0;
    host_vector_calls = 0;
    os_permissions = PMP_ALL_PERM;
    memset(local_permissions, 0, sizeof local_permissions);
    op_count = 0;
    operations[0] = 0;
    encl_lock = 0;
    memset(region_used, 0, sizeof region_used);
    memset(csrs, 0, sizeof csrs);
    memset(enclaves, 0, sizeof enclaves);
    enclave_init_metadata();
    memset(epm, 0x5a, sizeof epm);
    memset(utm, 0xa5, sizeof utm);
    args = (struct keystone_sbi_create_t){
	.epm_region = {.paddr = (uintptr_t)epm, .size = sizeof epm},
	.utm_region = {.paddr = (uintptr_t)utm, .size = sizeof utm},
	.runtime_paddr = (uintptr_t)epm + 4096,
	.user_paddr = (uintptr_t)epm + 8192,
	.free_paddr = (uintptr_t)epm + 12288,
	.free_requested = 4096};
}

static struct sbi_trap_regs invoke(unsigned long id, unsigned long a0)
{
    struct sbi_trap_context ctx = {0};

    ctx.regs.a7 = SBI_EXT_EXPERIMENTAL_KEYSTONE_ENCLAVE;
    ctx.regs.a6 = id;
    ctx.regs.a0 = a0;
    ctx.regs.a1 = 0xdead;
    ctx.regs.mepc = 0x8000;
    ctx.regs.mstatus = 0x1880;
    ctx.regs.ra = 0xabc;
    ctx.regs.sp = 0x123400;
    assert(sbi_ecall_handler(&ctx) == 0);
    assert(encl_lock == 0);
    return ctx.regs;
}

static void normal_return(struct sbi_trap_regs r, unsigned long status)
{
    assert(r.a0 == status);
    assert(r.a1 == 0);
    assert(r.mepc == 0x8004);
}

static void no_allocated_eids(void)
{
    int i;

    for (i = 0; i < ENCL_MAX; i++)
	assert(enclaves[i].state == INVALID);
}

static unsigned long create_fixture(void)
{
    struct sbi_trap_regs r = invoke(SBI_SM_CREATE_ENCLAVE, (uintptr_t)&args);

    assert(!r.a0 && r.a1 < ENCL_MAX && r.mepc == 0x8004);
    return r.a1;
}

#define CASE(name)                                                             \
    do {                                                                       \
	cases++;                                                               \
	printf("PASS %02d %s\n", cases, name);                                 \
    } while (0)

#define OBSERVED(name)                                                         \
    do {                                                                       \
	cases++;                                                               \
	findings++;                                                            \
	printf("OBSERVED %02d %s [not an acceptance PASS]\n", cases, name);    \
    } while (0)
