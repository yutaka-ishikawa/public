#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sched.h>
#include "host/Enclave.hpp"
#include "edge/edge_call.h"
#include "shared/keystone_user.h"
#include "shared/sm_err.h"
#include "protocol.h"
#include "kernel/abi.h"

static uintptr_t expected_size;
static uintptr_t expected_addr;
static uintptr_t observed_addr;
static unsigned int calls;
static bool failed;
static bool disabled;
static cpu_set_t allowed_cpus;
static unsigned int interruptions;

static void observe_stop(Keystone::Error reason)
{
    if (reason == Keystone::Error::EnclaveInterrupted) {
	interruptions++;
    }
}

static bool select_cpu(unsigned int index)
{
    cpu_set_t selected;
    unsigned int count;
    int cpu;

    count = CPU_COUNT(&allowed_cpus);
    if (!count) {
	return false;
    }
    index %= count;
    for (cpu = 0; cpu < CPU_SETSIZE; cpu++) {
	if (CPU_ISSET(cpu, &allowed_cpus)) {
	    if (!index) {
		CPU_ZERO(&selected);
		CPU_SET(cpu, &selected);
		return !sched_setaffinity(0, sizeof selected, &selected);
	    }
	    index--;
	}
    }
    return false;
}

static void dispatch(void *buffer)
{
    struct edge_call *call;
    struct multimem_result result;
    uintptr_t pointer;
    uintptr_t reply;
    size_t size;
    uintptr_t expected;
    unsigned int i;

    call = static_cast<struct edge_call *>(buffer);
    call->return_data.call_status = CALL_STATUS_BAD_OFFSET;
    if (call->call_id != 1 || edge_call_args_ptr(call, &pointer, &size) ||
	size != sizeof result) {
	failed = true;
	return;
    }
    memcpy(&result, reinterpret_cast<void *>(pointer), sizeof result);
    expected = disabled
		   ? SBI_ERR_SM_NOT_IMPLEMENTED
		   : (expected_size ? 0 : SBI_ERR_SM_ENCLAVE_NOT_ACCESSIBLE);
    if (result.size_status != expected || result.addr_status != expected ||
	result.size != expected_size || result.addr != expected_addr ||
	(expected_size && (!result.addr || (result.addr & 4095))) ||
	(observed_addr && result.addr != observed_addr)) {
	failed = true;
    }
    for (i = 0; i < MULTIMEM_PROBES; i++) {
	expected = i < 2 ? SBI_ERR_SM_NOT_IMPLEMENTED
			 : (i == 7 || expected_size
				? SBI_ERR_SM_ENCLAVE_ILLEGAL_ARGUMENT
				: SBI_ERR_SM_ENCLAVE_NOT_ACCESSIBLE);
	if (disabled) {
	    expected = i == 7 ? SBI_ERR_SM_ENCLAVE_ILLEGAL_ARGUMENT
			      : SBI_ERR_SM_NOT_IMPLEMENTED;
	}
	if (result.status[i] != expected) {
	    fprintf(stderr, "FAIL SBI 4000 probe=%u got=%lu expected=%lu\n", i,
		    result.status[i], expected);
	    failed = true;
	}
    }
    observed_addr = result.addr;
    reply = failed ? 1 : 0;
    pointer = edge_call_data_ptr();
    if (edge_call_check_ptr_valid(pointer, sizeof reply)) {
	failed = true;
	return;
    }
    memcpy(reinterpret_cast<void *>(pointer), &reply, sizeof reply);
    if (edge_call_setup_ret(call, reinterpret_cast<void *>(pointer),
			    sizeof reply)) {
	failed = true;
	return;
    }
    call->return_data.call_status = CALL_STATUS_OK;
    calls++;
    if (!select_cpu(calls)) {
	failed = true;
    }
}

static uintptr_t backing_bytes(void)
{
    FILE *file;
    unsigned long value;

    file = fopen("/sys/module/keystone_driver/parameters/backing_bytes", "r");
    if (!file) {
	return UINTPTR_MAX;
    }
    value = UINTPTR_MAX;
    if (fscanf(file, "%lu", &value) != 1) {
	value = UINTPTR_MAX;
    }
    fclose(file);
    return value;
}

static bool driver_errors(void)
{
    struct keystone_ioctl_create_enclave create = {};
    struct keystone_ioctl_create_enclave bad = {};
    struct keystone_ioctl_backing args = {};
    int fd;
    int other;
    bool ok;
    unsigned int i;

    fd = open("/dev/keystone_enclave", O_RDWR);
    other = open("/dev/keystone_enclave", O_RDWR);
    if (fd < 0 || other < 0) {
	return false;
    }
    create.min_pages = 4;
    ok = !ioctl(fd, KEYSTONE_IOC_CREATE_ENCLAVE, &create);
    if (ok) {
	args.version = KEYSTONE_CREATE_V2_VERSION;
	args.bytes = sizeof args;
	args.eid = create.eid;
	args.requested = 4096;
	ok = ioctl(other, KEYSTONE_IOC_BACKING_INIT, &args) == -1 &&
	     errno == EPERM;
	for (i = 0; i < 6; i++) {
	    args.version = i == 0 ? 99 : KEYSTONE_CREATE_V2_VERSION;
	    args.bytes = i == 1 ? 0 : sizeof args;
	    args.requested = i == 2 ? 0 : (i == 3 ? 4097 : 4096);
	    args.paddr = i == 4 ? 4096 : 0;
	    args.allocated = i == 5 ? 4096 : 0;
	    if (ioctl(fd, KEYSTONE_IOC_BACKING_INIT, &args) != -1 ||
		errno != EINVAL) {
		ok = false;
	    }
	}
	args.version = KEYSTONE_CREATE_V2_VERSION;
	args.bytes = sizeof args;
	args.requested = 4096;
	args.paddr = 0;
	args.allocated = 0;
	if (ioctl(fd, KEYSTONE_IOC_BACKING_INIT, &args) || !args.paddr ||
	    args.allocated < 4096) {
	    ok = false;
	}
	args.paddr = 0;
	args.allocated = 0;
	if (ioctl(fd, KEYSTONE_IOC_BACKING_INIT, &args) != -1 ||
	    errno != EBUSY) {
	    ok = false;
	}
	bad.eid = create.eid;
	/* Invalid runtime layout exercises failed CREATE_V2 then safe close. */
	if (ioctl(fd, KEYSTONE_IOC_FINALIZE_ENCLAVE, &bad) != -1) {
	    ok = false;
	}
    }
    close(other);
    close(fd);
    return ok;
}

static bool reject_registered(Keystone::Enclave &owner)
{
    struct multimem_overlap request = {};
    int fd;
    bool ok;

    fd = open("/dev/keystone_multimem_verify", O_RDWR);
    if (fd < 0) {
	return false;
    }
    request.paddr = owner.getBackingMemoryBase();
    request.size = owner.getBackingMemorySize();
    ok = !ioctl(fd, MULTIMEM_KERNEL_OVERLAP, &request) &&
	 request.error == SBI_ERR_SM_ENCLAVE_REGION_OVERLAPS;
    close(fd);
    if (ok) {
	printf("PASS GUEST SBI 4000/2006: another enclave backing registration "
	       "rejected\n");
    }
    return ok;
}

static bool run_one(Keystone::Enclave &enclave, uintptr_t size)
{
    Keystone::Error rc;
    uintptr_t result;

    expected_size = size;
    expected_addr = enclave.getBackingMemoryBase();
    if (enclave.getBackingMemorySize() != size) {
	return false;
    }
    observed_addr = 0;
    calls = 0;
    failed = false;
    interruptions = 0;
    edge_call_init_internals(
	reinterpret_cast<uintptr_t>(enclave.getSharedBuffer()),
	enclave.getSharedBufferSize());
    enclave.registerOcallDispatch(dispatch);
    enclave.registerStopDispatch(observe_stop);
    result = 99;
    rc = enclave.run(&result);
    if (rc != Keystone::Error::Success || result || failed || calls != 3 ||
	!interruptions) {
	fprintf(stderr, "FAIL SBI 4000 size=%lu SDK=%d exit=%lu calls=%u\n",
		size, static_cast<int>(rc), result, calls);
	return false;
    }
    printf("PASS GUEST SBI 4000: exact registered PA/size=%lu, "
	   "8 error probes, 3 OCALL/RESUME, timer stops=%u, CPU choices=%u\n",
	   size, interruptions, CPU_COUNT(&allowed_cpus));
    return true;
}

int main(int argc, char **argv)
{
    Keystone::Params params;
    uintptr_t first;
    uintptr_t baseline;
    pid_t child;
    int status;
    unsigned int cycle;

    if (argc != 4 && (argc != 5 || strcmp(argv[4], "--disabled"))) {
	return 2;
    }
    if (sched_getaffinity(0, sizeof allowed_cpus, &allowed_cpus)) {
	return 1;
    }
    if (argc == 5) {
	Keystone::Enclave enclave;

	disabled = true;
	params.setFreeMemSize(2 * 1024 * 1024);
	params.setUntrustedSize(256 * 1024);
	if (enclave.init(argv[1], argv[2], argv[3], params) !=
		Keystone::Error::Success ||
	    !run_one(enclave, 0) ||
	    enclave.destroy() != Keystone::Error::Success) {
	    return 1;
	}
	printf("PASS GUEST SBI 4000: disabled plugin rejected, legacy CREATE "
	       "works\n");
	return 0;
    }
    baseline = backing_bytes();
    if (baseline == UINTPTR_MAX || !driver_errors() ||
	backing_bytes() != baseline) {
	fprintf(stderr, "FAIL SBI 4000 driver rejection/rollback\n");
	return 1;
    }
    printf("PASS GUEST SBI 4000/2006: driver ABI/ownership and failed CREATE "
	   "cleanup\n");
    params.setFreeMemSize(2 * 1024 * 1024);
    params.setUntrustedSize(256 * 1024);
    for (cycle = 0; cycle < 3; cycle++) {
	Keystone::Enclave a;
	Keystone::Enclave b;

	params.setBackingMemSize(4 * 1024 * 1024);
	if (a.init(argv[1], argv[2], argv[3], params) !=
		Keystone::Error::Success ||
	    b.init(argv[1], argv[2], argv[3], params) !=
		Keystone::Error::Success ||
	    !reject_registered(a) || !run_one(a, 4 * 1024 * 1024)) {
	    return 1;
	}
	first = observed_addr;
	if (!run_one(b, 4 * 1024 * 1024) || first == observed_addr ||
	    a.destroy() != Keystone::Error::Success ||
	    b.destroy() != Keystone::Error::Success ||
	    backing_bytes() != baseline) {
	    return 1;
	}
	printf("PASS GUEST SBI 4000/2002: two owners, OCALL/RESUME, "
	       "destroy/reuse cycle=%u\n",
	       cycle);
    }
    {
	Keystone::Enclave enclave;
	const uintptr_t large = 256 * 1024 * 1024 + 4096;

	params.setBackingMemSize(large);
	if (enclave.init(argv[1], argv[2], argv[3], params) !=
		Keystone::Error::Success ||
	    !run_one(enclave, large) ||
	    enclave.destroy() != Keystone::Error::Success ||
	    backing_bytes() != baseline) {
	    return 1;
	}
	printf("PASS GUEST SBI 4000: 256 MiB + 4 KiB CMA/TOR backing, "
	       "timer/migration, reclaimed\n");
    }
    {
	Keystone::Enclave enclave;

	params.setBackingMemSize(0);
	if (enclave.init(argv[1], argv[2], argv[3], params) !=
		Keystone::Error::Success ||
	    !run_one(enclave, 0) ||
	    enclave.destroy() != Keystone::Error::Success) {
	    return 1;
	}
    }
    child = fork();
    if (child < 0) {
	return 1;
    }
    if (!child) {
	Keystone::Enclave enclave;

	params.setBackingMemSize(4 * 1024 * 1024);
	if (enclave.init(argv[1], argv[2], argv[3], params) !=
	    Keystone::Error::Success) {
	    _exit(1);
	}
	_exit(0); /* No SDK destructor: kernel release owns cleanup. */
    }
    if (waitpid(child, &status, 0) != child || !WIFEXITED(status) ||
	WEXITSTATUS(status) || backing_bytes() != baseline) {
	return 1;
    }
    if (sched_setaffinity(0, sizeof allowed_cpus, &allowed_cpus)) {
	return 1;
    }
    printf("PASS GUEST SBI 4000: registered/unregistered query, invalid "
	   "pointers, process-exit cleanup\n");
    return 0;
}
