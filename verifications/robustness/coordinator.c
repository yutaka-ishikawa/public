#define _GNU_SOURCE
#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include "common/client.h"

#define MAX_WORKERS 16

struct observation {
    uint64_t begin;
    uint64_t end;
    uint64_t callbacks;
    uint64_t interrupts;
    unsigned int completed;
    int failed;
};

static int hex_file(const char *path, unsigned char *bytes, size_t count)
{
    FILE *stream;
    size_t i;
    unsigned int value;
    int rc;

    stream = fopen(path, "r");
    if (!stream) {
	perror(path);
	return -1;
    }
    rc = 0;
    for (i = 0; i < count; i++) {
	if (fscanf(stream, "%2x", &value) != 1) {
	    rc = -1;
	    break;
	}
	bytes[i] = value;
    }
    fclose(stream);
    return rc;
}

static void worker(unsigned int id, int gate, int notification,
		   struct observation *observations,
		   const struct configuration *config, int cpu)
{
    struct client client;
    cpu_set_t affinity;
    unsigned int cycle;
    char byte;

    CPU_ZERO(&affinity);
    CPU_SET(cpu, &affinity);
    if (sched_setaffinity(0, sizeof affinity, &affinity)) {
	perror("worker affinity");
	_exit(2);
    }
    byte = 'R';
    if (write(notification, &byte, 1) != 1 || read(gate, &byte, 1) != 1) {
	_exit(2);
    }
    observations[id].begin = monotonic_ns();
    for (cycle = 0; cycle < config->cycles; cycle++) {
	if (client_open(&client, (id + cycle) & 1)) {
	    observations[id].failed = 1;
	    break;
	}
	if (client_run(&client, id, cycle + 100, config)) {
	    observations[id].failed = 1;
	}
	observations[id].callbacks += client.callbacks;
	observations[id].interrupts += client.interrupts;
	if (client_destroy(&client)) {
	    observations[id].failed = 1;
	}
	client_close(&client);
	observations[id].completed++;
	if (observations[id].failed) {
	    break;
	}
    }
    observations[id].end = monotonic_ns();
    _exit(observations[id].failed ? 1 : 0);
}

static int concurrent(const struct configuration *config)
{
    struct observation *observations;
    struct pollfd ready;
    cpu_set_t allowed;
    pid_t children[MAX_WORKERS] = {0};
    int cpus[MAX_WORKERS];
    int gate[2];
    int notification[2];
    unsigned int i;
    unsigned int count;
    unsigned int launched;
    unsigned int reaped;
    int status;
    int failed;
    pid_t pid;
    uint64_t deadline;
    uint64_t first_end;
    uint64_t last_begin;
    char byte;

    if (sched_getaffinity(0, sizeof allowed, &allowed)) {
	return 1;
    }
    count = 0;
    for (i = 0; i < CPU_SETSIZE && count < MAX_WORKERS; i++) {
	if (CPU_ISSET(i, &allowed)) {
	    cpus[count++] = i;
	}
    }
    if (count < config->workers) {
	fprintf(stderr, "SETUP_ERROR need %u distinct CPUs, have %u\n",
		config->workers, count);
	return 2;
    }
    observations =
	mmap(NULL, sizeof *observations * MAX_WORKERS, PROT_READ | PROT_WRITE,
	     MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (observations == MAP_FAILED || pipe(gate) || pipe(notification)) {
	return 2;
    }
    failed = 0;
    launched = 0;
    for (i = 0; i < config->workers; i++) {
	pid = fork();
	if (pid < 0) {
	    failed = 1;
	    break;
	}
	if (!pid) {
	    close(gate[1]);
	    close(notification[0]);
	    worker(i, gate[0], notification[1], observations, config, cpus[i]);
	}
	children[launched++] = pid;
    }
    close(gate[0]);
    close(notification[1]);
    ready.fd = notification[0];
    ready.events = POLLIN;
    for (i = 0; i < launched && !failed; i++) {
	if (poll(&ready, 1, 10000) != 1 || read(ready.fd, &byte, 1) != 1 ||
	    byte != 'R') {
	    failed = 1;
	}
    }
    close(notification[0]);
    for (i = 0; i < launched && !failed; i++) {
	byte = 'G';
	if (write(gate[1], &byte, 1) != 1) {
	    failed = 1;
	}
    }
    close(gate[1]);
    deadline = monotonic_ns() + (uint64_t)config->timeout * 1000000000;
    reaped = 0;
    while (reaped < launched) {
	pid = waitpid(-1, &status, WNOHANG);
	if (pid > 0) {
	    reaped++;
	    if (!WIFEXITED(status) || WEXITSTATUS(status)) {
		failed = 1;
	    }
	    for (i = 0; i < launched; i++) {
		if (children[i] == pid) {
		    children[i] = 0;
		}
	    }
	} else if (pid < 0 || monotonic_ns() > deadline) {
	    failed = 1;
	    break;
	} else {
	    usleep(10000);
	}
    }
    for (i = 0; i < launched; i++) {
	if (children[i]) {
	    kill(children[i], SIGKILL);
	    waitpid(children[i], &status, 0);
	}
    }
    first_end = UINT64_MAX;
    last_begin = 0;
    for (i = 0; i < launched; i++) {
	printf("OBS worker=%u cpu=%d begin_ns=%llu end_ns=%llu completed=%u "
	       "callbacks=%llu interrupts=%llu failed=%d\n",
	       i, cpus[i], (unsigned long long)observations[i].begin,
	       (unsigned long long)observations[i].end,
	       observations[i].completed,
	       (unsigned long long)observations[i].callbacks,
	       (unsigned long long)observations[i].interrupts,
	       observations[i].failed);
	if (observations[i].end < first_end) {
	    first_end = observations[i].end;
	}
	if (observations[i].begin > last_begin) {
	    last_begin = observations[i].begin;
	}
	if (observations[i].failed ||
	    observations[i].completed != config->cycles ||
	    observations[i].callbacks != (ROBUST_ROUNDS + 2) * config->cycles) {
	    failed = 1;
	}
    }
    if (first_end <= last_begin) {
	fprintf(stderr, "INCONCLUSIVE no overlapping worker lifetime\n");
	failed = 1;
    }
    printf("%s S-01/S-02 SBI "
	   "2001/2002/2003/2005/2006/3001/3002/3003/3004/3006/4000: "
	   "different-enclave concurrent requests, overlap_ns=%llu\n",
	   failed ? "FAIL" : "PASS",
	   (unsigned long long)(first_end > last_begin ? first_end - last_begin
						       : 0));
    munmap(observations, sizeof *observations * MAX_WORKERS);
    return failed;
}

int main(int argc, char **argv)
{
    struct configuration config = {2, 4, 120, 32, 0, {0}, {0}};
    int i;
    int failed;
    int raw_only = 0;
    int faults = 0;
    unsigned int quarantine_stage = 0;
    int group = 0;
    char *end;
    unsigned long value;
    long baseline_backing;
    long after_backing;
    int baseline_fds;
    int after_fds;

    setvbuf(stdout, NULL, _IOLBF, 0);
    if (sizeof(uintptr_t) != 8 || geteuid()) {
	fprintf(stderr, "SETUP_ERROR run as root in RV64 Linux guest\n");
	return 2;
    }
    for (i = 1; i < argc; i++) {
	if (!strcmp(argv[i], "--lifecycle-only")) {
	    group = 1;
	} else if (!strcmp(argv[i], "--lifetime-only")) {
	    group = 2;
	} else if (!strcmp(argv[i], "--stress-only")) {
	    group = 3;
	} else if (!strcmp(argv[i], "--faults")) {
	    faults = 1;
	} else if (!strcmp(argv[i], "--raw-only")) {
	    raw_only = 1;
	} else if (!strcmp(argv[i], "--crypto") && i + 2 < argc) {
	    config.crypto_enabled = 1;
	    if (hex_file(argv[++i], config.device_key, 32) ||
		hex_file(argv[++i], config.sm_hash, 64)) {
		return 2;
	    }
	} else if (!strcmp(argv[i], "--rng") && i + 1 < argc) {
	    config.rng_expected = strtol(argv[++i], &end, 10);
	    if (*end ||
		(config.rng_expected != 32 && config.rng_expected != -38 &&
		 config.rng_expected != -11 && config.rng_expected != -5)) {
		return 2;
	    }
	} else if ((!strcmp(argv[i], "--workers") ||
		    !strcmp(argv[i], "--cycles") ||
		    !strcmp(argv[i], "--timeout")) &&
		   i + 1 < argc) {
	    const char *option;

	    option = argv[i];
	    value = strtoul(argv[++i], &end, 10);
	    if (*end || !value || value > 3600) {
		return 2;
	    }
	    if (!strcmp(option, "--workers")) {
		if (value < 2 || value > MAX_WORKERS) {
		    return 2;
		}
		config.workers = value;
	    } else if (!strcmp(option, "--cycles")) {
		config.cycles = value;
	    } else {
		config.timeout = value;
	    }
	} else {
	    fprintf(stderr, "Usage: coordinator [--workers 2..16] [--cycles N] "
			    "[--timeout seconds] [--rng 32|-38|-11|-5] "
			    "[--crypto device-public.hex sm-hash.hex]\n");
	    return 2;
	}
    }
    signal(SIGPIPE, SIG_IGN);
    alarm(config.timeout + 30);
    if (quarantine_stage) {
        failed = quarantine_case(quarantine_stage);
        printf("%s ROBUSTNESS quarantine C integration\n", failed ? "FAIL" : "PASS");
        return failed ? 1 : 0;
    }
    if (faults) {
	failed = fault_cases(config.crypto_enabled);
	printf("%s ROBUSTNESS fault C integration\n", failed ? "FAIL" : "PASS");
	return failed ? 1 : 0;
    }
    if (raw_only) {
	failed = raw_cases(&config);
	printf("%s ROBUSTNESS raw C integration\n", failed ? "FAIL" : "PASS");
	return failed ? 1 : 0;
    }
    baseline_backing = backing_bytes();
    baseline_fds = open_fd_count();
    if (baseline_backing < 0 || baseline_fds < 0) {
	fprintf(stderr,
		"SETUP_ERROR requires backing_bytes and mounted proc/sys\n");
	return 2;
    }
    failed = 0;
    if (!group || group == 1) {
	failed += lifecycle_cases(&config);
    }
    if (!group || group == 2) {
	failed += lifetime_cases(&config);
    }
    if (!group || group == 3) {
	failed += concurrent(&config);
    }
    after_backing = backing_bytes();
    after_fds = open_fd_count();
    printf("%s S-04 C-client-fds before=%d after=%d driver-backing before=%ld "
	   "after=%ld\n",
	   baseline_fds == after_fds && baseline_backing == after_backing
	       ? "PASS"
	       : "FAIL",
	   baseline_fds, after_fds, baseline_backing, after_backing);
    if (baseline_fds != after_fds || baseline_backing != after_backing) {
	failed++;
    }
    printf("%s ROBUSTNESS finite C integration\n", failed ? "FAIL" : "PASS");
    return failed ? 1 : 0;
}
