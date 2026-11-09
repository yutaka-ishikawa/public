#define _GNU_SOURCE
#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include "common/client.h"
#include "shared/sm_err.h"

static int expectation(const char *id, const char *fids, int condition)
{
    printf("%s %s SBI %s\n", condition ? "PASS" : "FAIL", id, fids);
    return condition ? 0 : 1;
}

int lifecycle_cases(const struct configuration *config)
{
    struct client client;
    struct keystone_ioctl_run_enclave result;
    struct keystone_ioctl_backing backing = {0};
    void *mapping;
    int other;
    int failed;
    int rc;

    failed = 0;
    if (client_open(&client, 0)) {
	return 1;
    }
    rc = client_step(&client, 1, &result);
    failed +=
	expectation("A-2005-01/fresh", "2005",
		    !rc && result.error == SBI_ERR_SM_ENCLAVE_NOT_RESUMABLE);
    other = open("/dev/keystone_enclave", O_RDWR | O_CLOEXEC);
    if (other < 0) {
	client_close(&client);
	return 1;
    }
    errno = 0;
    rc = ioctl(other, KEYSTONE_IOC_RUN_ENCLAVE, &client.create);
    failed += expectation("A-DRV-01/foreign-file", "2003",
			  rc == -1 && errno == EPERM);
    close(other);
    rc = ioctl(client.fd, KEYSTONE_IOC_UTM_INIT, &client.create);
    failed += expectation("X-17/utm-after-finalize", "2001",
			  rc == -1 && errno == EBUSY);
    backing.version = KEYSTONE_CREATE_V2_VERSION;
    backing.bytes = sizeof backing;
    backing.eid = client.create.eid;
    backing.requested = 4096;
    rc = ioctl(client.fd, KEYSTONE_IOC_BACKING_INIT, &backing);
    failed += expectation("X-17/backing-after-finalize", "2006",
			  rc == -1 && errno == EBUSY);
    errno = 0;
    mapping = mmap(NULL, ROBUST_UTM + 4096, PROT_READ | PROT_WRITE, MAP_SHARED,
		   client.fd, 0);
    failed +=
	expectation("A-DRV-02/oversized-mmap", "2001", mapping == MAP_FAILED);
    if (mapping != MAP_FAILED) {
	munmap(mapping, ROBUST_UTM + 4096);
    }
    errno = 0;
    rc = ioctl(client.fd, KEYSTONE_IOC_RUN_ENCLAVE, (void *)1);
    failed += expectation("A-DRV-03/copy-from-user", "2003",
			  rc == -1 && errno == EFAULT);
    rc = client_run(&client, 0, 0, config);
    failed += expectation("A-3006-01/exit", "2003/2005/3004/3006", !rc);
    rc = client_step(&client, 0, &result);
    failed += expectation("A-2003-01/after-exit", "2003",
			  !rc && result.error == SBI_ERR_SM_ENCLAVE_NOT_FRESH);
    rc = client_step(&client, 1, &result);
    failed +=
	expectation("A-2005-02/after-exit", "2005",
		    !rc && result.error == SBI_ERR_SM_ENCLAVE_NOT_RESUMABLE);
    failed +=
	expectation("A-2002-01/destroy", "2002", !client_destroy(&client));
    errno = 0;
    rc = client_destroy(&client);
    failed += expectation("A-2002-02/double-destroy-driver", "2002", rc == -1);
    client_close(&client);
    return failed;
}

/* Verify last-reference cleanup, not the C++ SDK's independent lifetime bug. */
int lifetime_cases(const struct configuration *config)
{
    struct client client;
    struct keystone_ioctl_run_enclave result;
    int ready[2];
    int rc;
    int status;
    int duplicate;
    pid_t child;
    char byte;

    if (client_open(&client, 0)) {
	return 1;
    }
    duplicate = dup(client.fd);
    if (duplicate < 0) {
	client_close(&client);
	return 1;
    }
    close(client.fd);
    client.fd = duplicate;
    rc = client_run(&client, 1, 1, config);
    client_close(&client);
    if (expectation("X-16/dup-close-original", "2001/2002/2003/2005", !rc)) {
	return 1;
    }
    if (pipe(ready)) {
	return 1;
    }
    child = fork();
    if (child < 0) {
	close(ready[0]);
	close(ready[1]);
	return 1;
    }
    if (!child) {
	close(ready[0]);
	if (client_open(&client, 1)) {
	    _exit(2);
	}
	rc = client_step(&client, 0, &result);
	while (!rc && result.error == SBI_ERR_SM_ENCLAVE_INTERRUPTED) {
	    rc = client_step(&client, 1, &result);
	}
	if (rc || result.error != SBI_ERR_SM_ENCLAVE_EDGE_CALL_HOST) {
	    _exit(3);
	}
	byte = 'S';
	if (write(ready[1], &byte, 1) != 1) {
	    _exit(4);
	}
	for (;;) {
	    pause();
	}
    }
    close(ready[1]);
    rc = read(ready[0], &byte, 1);
    close(ready[0]);
    if (rc != 1 || byte != 'S') {
	kill(child, SIGKILL);
	waitpid(child, &status, 0);
	return 1;
    }
    kill(child, SIGKILL);
    waitpid(child, &status, 0);
    rc = WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL;
    if (expectation("X-16/sigkill-stopped", "2002/2004/2006", rc)) {
	return 1;
    }
    if (client_open(&client, 1)) {
	return 1;
    }
    rc = client_run(&client, 2, 2, config);
    client_close(&client);
    return expectation("X-16/recreate-after-sigkill", "2001/2002/2006", !rc);
}
