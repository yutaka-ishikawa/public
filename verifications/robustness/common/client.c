#define _GNU_SOURCE
#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "edge/edge_call.h"
#include "common/sha3.h"
#include "shared/sm_err.h"
#include "client.h"

uint64_t monotonic_ns(void)
{
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now)) {
	abort();
    }
    return (uint64_t)now.tv_sec * 1000000000 + now.tv_nsec;
}

long backing_bytes(void)
{
    FILE *stream;
    long value;

    stream = fopen("/sys/module/keystone_driver/parameters/backing_bytes", "r");
    if (!stream) {
	return -1;
    }
    value = -1;
    if (fscanf(stream, "%ld", &value) != 1) {
	value = -1;
    }
    fclose(stream);
    return value;
}

int open_fd_count(void)
{
    DIR *directory;
    struct dirent *entry;
    int count;

    directory = opendir("/proc/self/fd");
    if (!directory) {
	return -1;
    }
    count = 0;
    while ((entry = readdir(directory))) {
	if (strcmp(entry->d_name, ".") && strcmp(entry->d_name, "..")) {
	    count++;
	}
    }
    closedir(directory);
    return count;
}

/* Same raw loader/runtime/eapp layout as the SDK; real Eyrie parses ELF. */
int client_open(struct client *client, int backing)
{
    static const char *paths[] = {"build/loader.bin", "build/eyrie-rt",
				  "build/eapp"};
    struct stat statbuf;
    struct keystone_ioctl_backing store = {0};
    void *files[3] = {MAP_FAILED, MAP_FAILED, MAP_FAILED};
    size_t lengths[3] = {0};
    uint64_t padded[3];
    sha3_ctx_t hash;
    size_t offset;
    size_t page;
    size_t i;
    int fd;
    int rc;

    memset(client, 0, sizeof *client);
    client->fd = -1;
    client->utm = MAP_FAILED;
    rc = -1;
    for (i = 0; i < 3; i++) {
	fd = open(paths[i], O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
	    goto out;
	}
	if (fstat(fd, &statbuf) || statbuf.st_size <= 0 ||
	    statbuf.st_size > 16 * 1024 * 1024) {
	    close(fd);
	    goto out;
	}
	lengths[i] = statbuf.st_size;
	padded[i] = (lengths[i] + 4095) & ~4095UL;
	files[i] = mmap(NULL, lengths[i], PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	if (files[i] == MAP_FAILED) {
	    goto out;
	}
    }
    client->fd = open("/dev/keystone_enclave", O_RDWR | O_CLOEXEC);
    if (client->fd < 0) {
	goto out;
    }
    client->create.min_pages =
	512 + 16 + (padded[0] + padded[1] + padded[2]) / 4096;
    if (ioctl(client->fd, KEYSTONE_IOC_CREATE_ENCLAVE, &client->create)) {
	goto out;
    }
    client->live = 1;
    client->create.utm_size = ROBUST_UTM;
    if (ioctl(client->fd, KEYSTONE_IOC_UTM_INIT, &client->create)) {
	goto out;
    }
    sha3_init(&hash, 64);
    sha3_update(&hash, padded, sizeof padded);
    offset = 0;
    for (i = 0; i < 3; i++) {
	for (page = 0; page < padded[i]; page += 4096) {
	    void *mapping;
	    size_t bytes;

	    mapping = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED,
			   client->fd, offset + page);
	    if (mapping == MAP_FAILED) {
		goto out;
	    }
	    bytes = lengths[i] - page;
	    if (bytes > 4096) {
		bytes = 4096;
	    }
	    memset(mapping, 0, 4096);
	    memcpy(mapping, (char *)files[i] + page, bytes);
	    sha3_update(&hash, mapping, 4096);
	    munmap(mapping, 4096);
	}
	offset += padded[i];
    }
    sha3_final(client->measurement, &hash);
    client->create.runtime_paddr = client->create.epm_paddr + padded[0];
    client->create.user_paddr = client->create.runtime_paddr + padded[1];
    client->create.free_paddr = client->create.user_paddr + padded[2];
    client->create.free_requested = 2 * 1024 * 1024;
    if (backing) {
	store.version = KEYSTONE_CREATE_V2_VERSION;
	store.bytes = sizeof store;
	store.eid = client->create.eid;
	store.requested = 2 * 1024 * 1024;
	if (ioctl(client->fd, KEYSTONE_IOC_BACKING_INIT, &store)) {
	    goto out;
	}
    }
    client->backing_address = store.paddr;
    client->backing_size = store.allocated;
    if (ioctl(client->fd, KEYSTONE_IOC_FINALIZE_ENCLAVE, &client->create)) {
	goto out;
    }
    client->utm = mmap(NULL, ROBUST_UTM, PROT_READ | PROT_WRITE, MAP_SHARED,
		       client->fd, 0);
    if (client->utm == MAP_FAILED) {
	goto out;
    }
    rc = 0;
out:
    for (i = 0; i < 3; i++) {
	if (files[i] != MAP_FAILED) {
	    munmap(files[i], lengths[i]);
	}
    }
    if (rc) {
	perror("C client CREATE/load/FINALIZE");
	client_close(client);
    }
    return rc;
}

int client_step(struct client *client, int resume,
		struct keystone_ioctl_run_enclave *result)
{
    memset(result, 0, sizeof *result);
    result->eid = client->create.eid;
    return ioctl(client->fd,
		 resume ? KEYSTONE_IOC_RESUME_ENCLAVE
			: KEYSTONE_IOC_RUN_ENCLAVE,
		 result);
}

int client_destroy(struct client *client)
{
    int rc;

    rc = ioctl(client->fd, KEYSTONE_IOC_DESTROY_ENCLAVE, &client->create);
    if (!rc) {
	client->live = 0;
    }
    return rc;
}

void client_close(struct client *client)
{
    if (client->utm != MAP_FAILED) {
	munmap(client->utm, ROBUST_UTM);
	client->utm = MAP_FAILED;
    }
    if (client->fd >= 0) {
	if (client->live && client_destroy(client)) {
	    perror("cleanup DESTROY (driver close will retry/quarantine)");
	}
	close(client->fd);
	client->fd = -1;
    }
}

int client_run(struct client *client, unsigned int worker,
	       unsigned int generation, const struct configuration *config)
{
    struct keystone_ioctl_run_enclave result;
    struct edge_call *call;
    struct robust_message message;
    uintptr_t pointer;
    size_t size;
    size_t j;
    int resume;
    int rc;
    uint64_t deadline;

    edge_call_init_internals((uintptr_t)client->utm, ROBUST_UTM);
    deadline = monotonic_ns() + (uint64_t)config->timeout * 1000000000;
    resume = 0;
    for (;;) {
	if (monotonic_ns() > deadline) {
	    fprintf(stderr, "FAIL deadline worker=%u generation=%u\n", worker,
		    generation);
	    return -1;
	}
	if (client_step(client, resume, &result)) {
	    perror("RUN/RESUME");
	    return -1;
	}
	resume = 1;
	if (result.error == SBI_ERR_SM_ENCLAVE_INTERRUPTED) {
	    client->interrupts++;
	    continue;
	}
	if (!result.error) {
	    return result.value || client->callbacks != ROBUST_ROUNDS + 2 ? -1
									  : 0;
	}
	if (result.error != SBI_ERR_SM_ENCLAVE_EDGE_CALL_HOST) {
	    fprintf(stderr, "FAIL SBI 2003/2005 error=%lu value=%lu\n",
		    (unsigned long)result.error, (unsigned long)result.value);
	    return -1;
	}
	call = client->utm;
	if (call->call_id == ROBUST_BAD_RETURN &&
	    client->callbacks == ROBUST_ROUNDS + 1) {
	    call->return_data.call_status = CALL_STATUS_OK;
	    call->return_data.call_ret_offset = SIZE_MAX - 1;
	    call->return_data.call_ret_size = 4;
	    client->callbacks++;
	    continue;
	}
	if (call->call_id != ROBUST_CALL ||
	    edge_call_args_ptr(call, &pointer, &size) ||
	    size != sizeof message) {
	    return -1;
	}
	memcpy(&message, (void *)pointer, sizeof message);
	if (!client->callbacks) {
	    if (message.sequence != UINT64_MAX) {
		return -1;
	    }
	    message.worker = worker;
	    message.generation = generation;
	} else {
	    if (message.worker != worker || message.generation != generation ||
		message.sequence != client->callbacks - 1 ||
		message.rng_status != config->rng_expected ||
		message.attest_status !=
		    (config->crypto_enabled ? 0 : 100100) ||
		message.seal_status != (config->crypto_enabled ? 0 : 100100)) {
		fprintf(stderr,
			"FAIL service binding/status worker=%u sequence=%lu "
			"rng=%ld attest=%ld seal=%ld\n",
			worker, (unsigned long)message.sequence,
			(long)message.rng_status, (long)message.attest_status,
			(long)message.seal_status);
		return -1;
	    }
	    if (config->crypto_enabled &&
		check_report(&message.report, message.bytes,
			     client->measurement, config)) {
		return -1;
	    }
	    if (message.multimem.size_status !=
		    (client->backing_size ? 0 : 100007) ||
		message.multimem.addr_status !=
		    (client->backing_size ? 0 : 100007) ||
		message.multimem.size != client->backing_size ||
		message.multimem.addr != client->backing_address) {
		fprintf(stderr, "FAIL SBI 4000 backing owner/value\n");
		return -1;
	    }
	    for (j = 0; j < MULTIMEM_PROBES; j++) {
		uintptr_t expected;

		expected =
		    j < 2 ? 100100
			  : (j == 7 || client->backing_size ? 100008 : 100007);
		if (message.multimem.status[j] != expected) {
		    return -1;
		}
	    }
	    for (j = 0; j < sizeof message.bytes; j++) {
		unsigned char expected;

		expected = robust_byte(worker, generation, message.sequence, j);
		if (message.bytes[j] != expected) {
		    return -1;
		}
		message.bytes[j] ^= 0x5a;
	    }
	}
	/* Hold the enclave STOPPED while other workers make progress. */
	usleep(1000);
	pointer = edge_call_data_ptr();
	memcpy((void *)pointer, &message, sizeof message);
	rc = edge_call_setup_ret(call, (void *)pointer, sizeof message);
	call->return_data.call_status =
	    rc ? CALL_STATUS_BAD_OFFSET : CALL_STATUS_OK;
	if (rc) {
	    return -1;
	}
	client->callbacks++;
    }
}
