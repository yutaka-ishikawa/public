#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "edge/edge_call.h"
#include "host/keystone.h"
#include "protocol.h"

static long expected_error;
static bool env_test;
static unsigned int calls;
static bool failed;

static void handle_random(void *buffer)
{
    struct edge_call *call;
    struct rng_message message;
    uintptr_t ptr;
    size_t length;
    size_t i;
    static const size_t lengths[] = {0, 1, 7, 8, 9, 15, 16, 17, 32, 4096};

    call = static_cast<struct edge_call *>(buffer);
    call->return_data.call_status = CALL_STATUS_ERROR;
    if (edge_call_args_ptr(call, &ptr, &length) || length != sizeof message) {
	failed = true;
	return;
    }
    memcpy(&message, reinterpret_cast<void *>(ptr), sizeof message);
    if (calls >= 10 ||
	message.sequence != (expected_error && !env_test ? 1 : calls) ||
	message.length !=
	    (env_test ? 16 : lengths[expected_error ? 1 : calls]) ||
	message.status != (expected_error && !env_test
			       ? expected_error
			       : static_cast<long>(message.length))) {
	failed = true;
	return;
    }
    for (i = 0; i < message.length; i++) {
	message.bytes[i] ^= 0x5a;
    }
    memcpy(reinterpret_cast<void *>(ptr), &message, sizeof message);
    if (edge_call_setup_ret(call, reinterpret_cast<void *>(ptr),
			    sizeof message)) {
	failed = true;
	return;
    }
    calls++;
    call->return_data.call_status = CALL_STATUS_OK;
}

int main(int argc, char **argv)
{
    Keystone::Enclave enclave;
    Keystone::Params params;
    Keystone::Error result;
    uintptr_t retval;

    if (argc != 5 && argc != 6) {
	fprintf(stderr, "usage: %s EAPP RUNTIME LOADER EXPECTED_ERRNO [env]\n",
		argv[0]);
	return 2;
    }
    env_test = argc == 6;
    if (env_test && strcmp(argv[5], "env")) {
	return 2;
    }
    expected_error = strtol(argv[4], nullptr, 10);
    if (expected_error != 0 && expected_error != -38 && expected_error != -11 &&
	expected_error != -5) {
	return 2;
    }
    params.setFreeMemSize(2 * 1024 * 1024);
    params.setUntrustedSize(256 * 1024);
    result = enclave.init(argv[1], argv[2], argv[3], params);
    if (result != Keystone::Error::Success) {
	fprintf(stderr, "FAIL SBI 2001 RNG CREATE: %d\n", static_cast<int>(result));
	return 1;
    }
    edge_call_init_internals(
	reinterpret_cast<uintptr_t>(enclave.getSharedBuffer()),
	enclave.getSharedBufferSize());
    if (register_call(RNG_CALL, handle_random)) {
	return 1;
    }
    enclave.registerOcallDispatch(incoming_call_dispatch);
    retval = 99;
    result = enclave.run(&retval);
    if (env_test && expected_error) {
	if (result != Keystone::Error::Success || retval != UINTPTR_MAX ||
	    calls) {
	    fprintf(stderr, "FAIL SBI 3001 AT_RANDOM: expected startup rejection\n");
	    return 1;
	}
    } else if (result != Keystone::Error::Success || retval || failed ||
	       calls != (env_test || expected_error ? 1U : 10U)) {
	fprintf(stderr, "FAIL SBI 3001 RNG: SDK=%d exit=%lu calls=%u failed=%d\n",
		static_cast<int>(result), static_cast<unsigned long>(retval),
		calls, failed);
	return 1;
    }
    if (enclave.destroy() != Keystone::Error::Success) {
	return 1;
    }
    printf("PASS GUEST SBI 3001: mode=%s expected-error=%ld callbacks=%u "
	   "DESTROY\n",
	   env_test ? "env" : "native", expected_error, calls);
    if (env_test) {
	printf("PASS GUEST SBI 3001: AT_RANDOM startup/resume error=%ld\n",
	       expected_error);
    }
    return 0;
}
