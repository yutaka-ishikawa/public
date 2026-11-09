#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <time.h>
#include "host/Enclave.hpp"
#include "edge/edge_call.h"
#include "shared/sm_err.h"
#include "protocol.h"

static unsigned int calls;
static bool failed;
static bool timer_interval;
static unsigned int interrupted;
static unsigned int timer_interrupted;
static unsigned int edge_stops;

static void observe_stop(Keystone::Error reason)
{
    if (reason == Keystone::Error::EnclaveInterrupted) {
	interrupted++;
	if (timer_interval) {
	    timer_interrupted++;
	}
    } else if (reason == Keystone::Error::EdgeCallHost) {
	edge_stops++;
    } else {
	failed = true;
    }
}

static void dispatch(void *buffer)
{
    struct edge_call *call;
    struct stop_verify_message message;
    uintptr_t pointer;
    size_t size;
    uint64_t reply;
    static const uint64_t invalid[] = {2, 3, UINT64_MAX};

    call = static_cast<struct edge_call *>(buffer);
    call->return_data.call_status = CALL_STATUS_BAD_OFFSET;
    if (call->call_id != STOP_VERIFY_CALL ||
	edge_call_args_ptr(call, &pointer, &size) || size != sizeof message) {
	failed = true;
	return;
    }
    memcpy(&message, reinterpret_cast<void *>(pointer), sizeof message);
    if (message.sequence != calls + 1 || calls >= STOP_VERIFY_STAGES ||
	(calls < 3 && (message.request != invalid[calls] ||
		       message.status !=
			   SBI_ERR_SM_ENCLAVE_ILLEGAL_ARGUMENT)) ||
	(calls >= 3 && (message.request != 0 || message.status != 0)) ||
	((calls == 7 || calls == 9) && timer_interrupted < 3)) {
	failed = true;
	return;
    }
    reply = STOP_VERIFY_REPLY ^ message.sequence;
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
    if (calls < 3) {
	printf("PASS GUEST SBI 3004: "
	       "invalid request=%llu rejected in enclave\n",
	       static_cast<unsigned long long>(message.request));
    } else if (calls < 6) {
	printf("PASS GUEST SBI 3004/2005: explicit timer-yield/resume %u\n",
	       calls - 2);
    } else if (calls == 6 || calls == 8) {
	timer_interrupted = 0;
	timer_interval = true;
    } else if (calls == 7 || calls == 9) {
	timer_interval = false;
	printf("PASS GUEST SBI 3004/2005: SM preemption interval, "
	       "guest-timer=%s interruptions/resumes=%u\n",
	       calls == 7 ? "armed" : "cancelled", timer_interrupted);
    }
    calls++;
}

static bool host_timer_wakeup(void)
{
    struct timespec before;
    struct timespec after;
    struct timespec delay = {0, 20000000};
    long long elapsed;

    if (clock_gettime(CLOCK_MONOTONIC, &before)) {
	return false;
    }
    while (nanosleep(&delay, &delay)) {
	if (errno != EINTR) {
	    return false;
	}
    }
    if (clock_gettime(CLOCK_MONOTONIC, &after)) {
	return false;
    }
    elapsed = (after.tv_sec - before.tv_sec) * 1000000000LL +
	      after.tv_nsec - before.tv_nsec;
    return elapsed >= 20000000;
}

int main(int argc, char **argv)
{
    Keystone::Enclave enclave;
    Keystone::Params params;
    Keystone::Error result;
    uintptr_t retval;

    if (argc != 4) {
	fprintf(stderr, "Usage: runner EAPP RUNTIME LOADER\n");
	return 2;
    }
    if (!host_timer_wakeup()) {
	fprintf(stderr, "FAIL HOST TIMER: sleep/wakeup before RUN\n");
	return 1;
    }
    params.setFreeMemSize(2 * 1024 * 1024);
    params.setUntrustedSize(256 * 1024);
    result = enclave.init(argv[1], argv[2], argv[3], params);
    if (result != Keystone::Error::Success) {
	fprintf(stderr, "FAIL SBI 3004 prerequisite CREATE: SDK=%d\n",
		static_cast<int>(result));
	return 1;
    }
    edge_call_init_internals(
	reinterpret_cast<uintptr_t>(enclave.getSharedBuffer()),
	enclave.getSharedBufferSize());
    enclave.registerOcallDispatch(dispatch);
    enclave.registerStopDispatch(observe_stop);
    retval = 99;
    result = enclave.run(&retval);
    if (result != Keystone::Error::Success || retval || failed ||
	calls != STOP_VERIFY_STAGES || edge_stops != STOP_VERIFY_STAGES ||
	interrupted < 3 || timer_interrupted < 3) {
	fprintf(stderr, "FAIL GUEST SBI 3004: SDK=%d exit=%lu calls=%u "
		"edge-stops=%u interruptions=%u timed=%u\n",
		static_cast<int>(result), static_cast<unsigned long>(retval),
		calls,
		edge_stops, interrupted, timer_interrupted);
	return 1;
    }
    if (enclave.destroy() != Keystone::Error::Success) {
	fprintf(stderr, "FAIL SBI 2002: STOP test cleanup\n");
	return 1;
    }
    if (!host_timer_wakeup()) {
	fprintf(stderr, "FAIL HOST TIMER: sleep/wakeup after EXIT\n");
	return 1;
    }
    printf("PASS GUEST HOST TIMER: sleep/wakeup before RUN and after EXIT\n");
    printf("PASS GUEST SBI 3004: STOP/RESUME, 11 OCALL round trips, "
	   "stack/global preservation, explicit DESTROY\n");
    return 0;
}
