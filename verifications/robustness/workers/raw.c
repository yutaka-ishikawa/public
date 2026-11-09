#define _GNU_SOURCE
#include <stdint.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include "common/client.h"
#include "shared/sm_err.h"
#include "kernel/abi.h"

struct attempt {
    int fd;
    int cpu;
    int rc;
    struct robust_call call;
};

static void *holder(void *argument)
{
    struct attempt *attempt;
    cpu_set_t affinity;

    attempt = argument;
    CPU_ZERO(&affinity);
    CPU_SET(attempt->cpu, &affinity);
    attempt->rc = sched_setaffinity(0, sizeof affinity, &affinity);
    if (!attempt->rc) {
	attempt->rc = ioctl(attempt->fd, ROBUST_CALL_RAW, &attempt->call);
    }
    return NULL;
}

static int owned_open(const char *eapp)
{
    static const char *names[3] = {"build/loader.bin", "build/eyrie-rt",
				   "build/eapp"};
    struct robust_init request = {0};
    const char *path;
    struct stat info;
    void *mapping[3] = {MAP_FAILED, MAP_FAILED, MAP_FAILED};
    unsigned int i;
    int input;
    int fd;
    int rc;

    fd = open("/dev/keystone_robustness", O_RDWR | O_CLOEXEC);
    if (fd < 0) {
	return -1;
    }
    rc = -1;
    for (i = 0; i < 3; i++) {
	path = i == 2 ? eapp : names[i];
	input = open(path, O_RDONLY | O_CLOEXEC);
	if (input < 0) {
	    goto out;
	}
	if (fstat(input, &info) || info.st_size <= 0) {
	    close(input);
	    goto out;
	}
	request.lengths[i] = info.st_size;
	mapping[i] = mmap(NULL, info.st_size, PROT_READ, MAP_PRIVATE, input, 0);
	close(input);
	if (mapping[i] == MAP_FAILED) {
	    goto out;
	}
	request.pointers[i] = (uintptr_t)mapping[i];
    }
    rc = ioctl(fd, ROBUST_INIT, &request);
out:
    for (i = 0; i < 3; i++) {
	if (mapping[i] != MAP_FAILED) {
	    munmap(mapping[i], request.lengths[i]);
	}
    }
    if (rc) {
	close(fd);
	return -1;
    }
    return fd;
}

static int race(unsigned long first, unsigned long second, unsigned int stage,
		unsigned long expected, int cpu)
{
    struct attempt attempt = {0};
    struct robust_call control = {4900, 0, stage, 10000000, 0, 0};
    struct robust_call query = {4900, 1, 0, 0, 0, 0};
    struct robust_call contender = {0};
    struct robust_call prepare = {2003, 0, 0, 0, 0, 0};
    pthread_t thread;
    uint64_t deadline;
    int fd;
    int failed;
    int entered;
    int completed_during_gate;

    fd = owned_open("build/eapp");
    if (fd < 0) {
	perror("SETUP_ERROR owned raw-SBI enclave");
	return 1;
    }
    if (first == 2005) {
	do {
	    if (ioctl(fd, ROBUST_CALL_RAW, &prepare)) {
		close(fd);
		return 1;
	    }
	    prepare.fid = 2005;
	} while (prepare.error == SBI_ERR_SM_ENCLAVE_INTERRUPTED);
	if (prepare.error != SBI_ERR_SM_ENCLAVE_EDGE_CALL_HOST) {
	    close(fd);
	    return 1;
	}
    }
    if (ioctl(fd, ROBUST_CALL_RAW, &control) || control.error) {
	fprintf(stderr,
		"SETUP_ERROR requires KEYSTONE_VERIFICATION=1 firmware, "
		"error=%lld\n",
		(long long)control.error);
	close(fd);
	return 1;
    }
    attempt.fd = fd;
    attempt.cpu = cpu;
    attempt.call.fid = first;
    if (pthread_create(&thread, NULL, holder, &attempt)) {
	close(fd);
	return 1;
    }
    entered = 0;
    deadline = monotonic_ns() + 5000000000ULL;
    do {
	if (ioctl(fd, ROBUST_CALL_RAW, &query) || query.error) {
	    break;
	}
	if (query.value == 2) {
	    entered = 1;
	    break;
	}
	if (query.value == 3) {
	    break;
	}
	usleep(100);
    } while (monotonic_ns() < deadline);
    contender.fid = second;
    failed = !entered;
    completed_during_gate = 0;
    if (entered) {
	if (ioctl(fd, ROBUST_CALL_RAW, &contender) ||
	    contender.error != (long long)expected) {
	    failed = 1;
	}
	if (!ioctl(fd, ROBUST_CALL_RAW, &query) && !query.error &&
	    query.value == 2) {
	    completed_during_gate = 1;
	} else {
	    failed = 1;
	}
    }
    pthread_join(thread, NULL);
    if (attempt.rc ||
	(first == 2002
	     ? attempt.call.error != 0
	     : (attempt.call.error != SBI_ERR_SM_ENCLAVE_INTERRUPTED &&
		attempt.call.error != SBI_ERR_SM_ENCLAVE_EDGE_CALL_HOST))) {
	failed = 1;
    }
    printf("%s RAW SBI %lu x %lu same-enclave stage=%u entered=%d "
	   "contender-returned-during-gate=%d holder-status=%lld "
	   "contender-status=%lld expected=%lu\n",
	   failed ? "FAIL" : "PASS", first, second, stage, entered,
	   completed_during_gate, (long long)attempt.call.error,
	   (long long)contender.error, expected);
    close(fd);
    return failed;
}

int raw_cases(const struct configuration *config)
{
    cpu_set_t allowed;
    cpu_set_t parent;
    unsigned int i;
    int first_cpu;
    int second_cpu;
    int failed;

    (void)config;
    if (sched_getaffinity(0, sizeof allowed, &allowed)) {
	return 1;
    }
    first_cpu = -1;
    second_cpu = -1;
    for (i = 0; i < CPU_SETSIZE; i++) {
	if (CPU_ISSET(i, &allowed)) {
	    if (first_cpu < 0) {
		first_cpu = i;
	    } else {
		second_cpu = i;
		break;
	    }
	}
    }
    if (second_cpu < 0) {
	fprintf(stderr, "SETUP_ERROR raw races require two CPUs\n");
	return 2;
    }
    CPU_ZERO(&parent);
    CPU_SET(first_cpu, &parent);
    if (sched_setaffinity(0, sizeof parent, &parent)) {
	return 2;
    }
    failed = race(2003, 2003, 1, 100016, second_cpu);
    failed += race(2003, 2005, 1, 100010, second_cpu);
    failed += race(2003, 2002, 1, 100005, second_cpu);
    failed += race(2005, 2005, 2, 100010, second_cpu);
    failed += race(2005, 2003, 2, 100016, second_cpu);
    failed += race(2005, 2002, 2, 100005, second_cpu);
    failed += race(2002, 2002, 3, 100005, second_cpu);
    failed += race(2002, 2003, 3, 100016, second_cpu);
    failed += race(2002, 2005, 3, 100010, second_cpu);
    if (sched_setaffinity(0, sizeof allowed, &allowed)) {
	return 2;
    }
    return failed;
}

static int fault_case(unsigned int stage, int seal)
{
    struct robust_call control = {4900, 2, stage, 0, 0, 0};
    struct robust_call query = {4900, 1, 0, 0, 0, 0};
    struct robust_call call = {2003, 0, 0, 0, 0, 0};
    char path[64];
    int fd;
    int failed;
    uint64_t deadline;

    snprintf(path, sizeof path, "build/fault-%u%s", stage, seal ? "-seal" : "");
    fd = owned_open(path);
    if (fd < 0) {
	return 1;
    }
    if (ioctl(fd, ROBUST_CALL_RAW, &control) || control.error) {
	close(fd);
	return 1;
    }
    failed = 0;
    deadline = monotonic_ns() + 10000000000ULL;
    do {
	if (ioctl(fd, ROBUST_CALL_RAW, &call) || monotonic_ns() > deadline) {
	    failed = 1;
	    break;
	}
	call.fid = 2005;
    } while (call.error == SBI_ERR_SM_ENCLAVE_INTERRUPTED);
    if (call.error || call.value || ioctl(fd, ROBUST_CALL_RAW, &query) ||
	query.error || query.value != 5) {
	failed = 1;
    }
    printf("%s F-%s SBI %u stage=%u fired=%d first-fails-second-recovers "
	   "exit=%llu status=%lld\n",
	   failed ? "FAIL" : "PASS",
	   stage == 10	 ? "RNG"
	   : stage == 13 ? "CRYPTO"
			 : "COPY",
	   stage == 10		 ? 3001
	   : seal || stage == 13 ? 3003
				 : 3002,
	   stage, query.value == 5, (unsigned long long)call.value,
	   (long long)call.error);
    close(fd);
    return failed;
}

int fault_cases(int crypto)
{
    int failed;

    failed = fault_case(10, 0);
    if (crypto) {
	failed += fault_case(11, 0);
	failed += fault_case(12, 0);
	failed += fault_case(11, 1);
	failed += fault_case(12, 1);
	failed += fault_case(13, 0);
    } else {
	printf(
	    "SKIP F-COPY/F-CRYPTO SBI 3002/3003: requires qemu-dev provider\n");
    }
    return failed;
}

static long quarantined_bytes(void)
{
    FILE *stream;
    long bytes;

    stream = fopen("/sys/module/probe/parameters/quarantine_bytes", "r");
    if (!stream) {
        return -1;
    }
    bytes = -1;
    if (fscanf(stream, "%ld", &bytes) != 1) {
        bytes = -1;
    }
    fclose(stream);
    return bytes;
}

int quarantine_case(unsigned int stage)
{
    struct robust_call control = {4900, 2, stage, 0, 0, 0};
    struct robust_call query = {4900, 1, 0, 0, 0, 0};
    struct robust_call destroy = {2002, 0, 0, 0, 0, 0};
    long before;
    long after;
    long first;
    int fd;
    int failed;

    before = quarantined_bytes();
    fd = owned_open("build/eapp");
    if (fd < 0 || before < 0) {
        if (fd >= 0) {
            close(fd);
        }
        return 2;
    }
    failed = ioctl(fd, ROBUST_CALL_RAW, &control) || control.error;
    if (!failed) {
        failed = ioctl(fd, ROBUST_CALL_RAW, &destroy) || destroy.error != 100003;
    }
    first = destroy.error;
    if (!failed) {
        failed = ioctl(fd, ROBUST_CALL_RAW, &query) || query.error || query.value != 5;
    }
    if (!failed) {
        failed = ioctl(fd, ROBUST_CALL_RAW, &destroy) || destroy.error != 100005;
    }
    close(fd);
    after = quarantined_bytes();
    if (after - before != (4 * 1024 * 1024 + ROBUST_UTM)) {
        failed = 1;
    }
    printf("%s F-%s SBI 2002 stage=%u fired=%d first=%ld retry=%lld quarantined-before=%ld after=%ld\n",
           failed ? "FAIL" : "PASS", stage == 14 ? "PMP-pre-unset" : "IPI-post-send-observation",
           stage, query.value == 5, first, (long long)destroy.error, before, after);
    return failed;
}
