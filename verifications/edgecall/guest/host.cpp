#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include "edge/edge_call.h"
#include "host/keystone.h"
#include "protocol.h"
#include "../kernel/probe_abi.h"

static Keystone::Enclave *enclave;
static unsigned long calls;
static bool failed;
static bool probe_passed;
static bool require_probe = true;

static void host_handler(void *buffer)
{
    struct edge_call *call;
    struct edge_verify_message request;
    struct edge_verify_message response;
    struct edge_verify_probe probe;
    uintptr_t ptr;
    size_t len;
    size_t i;
    size_t expected;
    int fd;

    call = static_cast<struct edge_call *>(buffer);
    call->return_data.call_status = CALL_STATUS_ERROR;
    if (edge_call_args_ptr(call, &ptr, &len) || len < EDGE_VERIFY_HEADER ||
	len > sizeof(request)) {
	failed = true;
	return;
    }
    memset(&request, 0, sizeof(request));
    memcpy(&request, reinterpret_cast<void *>(ptr), len);
    if (request.length > EDGE_VERIFY_MAX ||
	len != EDGE_VERIFY_HEADER + request.length ||
	request.sequence != calls) {
	failed = true;
	return;
    }
    for (i = 0; i < request.length; i++) {
	if (request.bytes[i] != static_cast<unsigned char>(i * 17 + calls)) {
	    failed = true;
	    return;
	}
    }
    /* The pointer is transported as data, never dereferenced by this wrapper.
     */
    if (edge_call_check_ptr_valid(request.private_va, 1) == 0) {
	failed = true;
	return;
    }
    if (require_probe && !probe_passed) {
	memset(&probe, 0, sizeof(probe));
	probe.private_pa = enclave->getMemory()->getStartAddr();
	probe.shared_pa = enclave->getMemory()->getUTMPhysAddr();
	expected = static_cast<unsigned char *>(buffer)[0];
	fd = open("/dev/keystone_edge_probe", O_RDWR);
	if (fd < 0 || ioctl(fd, EDGE_VERIFY_PROBE, &probe) < 0) {
	    perror("EPM probe unavailable or ioctl failed");
	    if (fd >= 0) {
		close(fd);
	    }
	    failed = true;
	    return;
	}
	close(fd);
	if (probe.control_result || probe.control_byte != 0x6d ||
	    probe.shared_result || probe.shared_byte != expected ||
	    probe.private_result != -EFAULT) {
	    fprintf(stderr,
		    "FAIL EPM protection: control=%d shared=%d private=%d\n",
		    probe.control_result, probe.shared_result,
		    probe.private_result);
	    failed = true;
	    return;
	}
	probe_passed = true;
	printf("PASS GUEST: kernel direct-map EPM read denied; UTM/control "
	       "readable\n");
    }
    response = request;
    for (i = 0; i < response.length; i++) {
	response.bytes[i] ^= 0xa5;
    }
    memcpy(reinterpret_cast<void *>(ptr), &response,
	   EDGE_VERIFY_HEADER + response.length);
    if (edge_call_setup_ret(call, reinterpret_cast<void *>(ptr),
			    EDGE_VERIFY_HEADER + response.length)) {
	failed = true;
	return;
    }
    calls++;
    call->return_data.call_status = CALL_STATUS_OK;
}

static void bad_return(void *buffer)
{
    struct edge_call *call;

    call = static_cast<struct edge_call *>(buffer);
    call->return_data.call_ret_offset = SIZE_MAX;
    call->return_data.call_ret_size = 1;
    call->return_data.call_status = CALL_STATUS_OK;
}

#ifndef EDGE_VERIFY_FREE_MEM_MIB
#define EDGE_VERIFY_FREE_MEM_MIB 2
#endif

static int execute_guest(char **argv)
{
    Keystone::Enclave instance;
    Keystone::Params params;
    Keystone::Error result;
    uintptr_t retval;

    params.setFreeMemSize((size_t)EDGE_VERIFY_FREE_MEM_MIB * 1024 * 1024);
    params.setUntrustedSize(256 * 1024);
    enclave = &instance;
    result = instance.init(argv[1], argv[2], argv[3], params);
    if (result != Keystone::Error::Success) {
	fprintf(stderr, "enclave init failed: %d\n", static_cast<int>(result));
	return 1;
    }
    edge_call_init_internals(
	reinterpret_cast<uintptr_t>(instance.getSharedBuffer()),
	instance.getSharedBufferSize());
    if (register_call(EDGE_VERIFY_CALL, host_handler) ||
	register_call(EDGE_VERIFY_BAD_RETURN, bad_return)) {
	return 1;
    }
    instance.registerOcallDispatch(incoming_call_dispatch);
    retval = 99;
    result = instance.run(&retval);
    if (result != Keystone::Error::Success || retval || failed || calls != 8 ||
	(require_probe && !probe_passed)) {
	fprintf(stderr, "FAIL guest: SDK=%d exit=%lu callbacks=%lu failed=%d\n",
		static_cast<int>(result), static_cast<unsigned long>(retval),
		calls, failed);
	return 1;
    }
    printf("PASS GUEST: 8 binary OCALL round trips and invalid return "
	   "rejection\n");
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 4 && argc != 5) {
	fprintf(stderr, "usage: %s EAPP RUNTIME LOADER [--data-only]\n",
		argv[0]);
	return 2;
    }
    if (argc == 5) {
	if (strcmp(argv[4], "--data-only")) {
	    return 2;
	}
	require_probe = false;
	printf("SKIP GUEST PMP isolation: explicit --data-only mode\n");
    }
    return execute_guest(argv);
}
