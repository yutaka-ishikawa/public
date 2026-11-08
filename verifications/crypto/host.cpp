#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/random.h>
#include "host/keystone.h"
#include "edge/edge_call.h"
#include "verifier/Report.hpp"
#include "protocol.h"

static unsigned char device_pin[32];
static unsigned char sm_pin[64];
static unsigned char enclave_pin[64];
static struct crypto_challenge challenge;
static unsigned int reports;
static struct crypto_message last_report;
static bool done;
static bool failed;
static bool issued;
static bool unavailable;

static bool read_exact(const char *name, void *bytes, size_t length)
{
    FILE *file;
    bool ok;

    file = fopen(name, "rb");
    if (!file) {
	perror(name);
	return false;
    }
    ok = fread(bytes, 1, length, file) == length && fgetc(file) == EOF;
    fclose(file);
    return ok;
}

static void dispatch(void *buffer)
{
    struct edge_call *call;
    struct crypto_message message;
    struct crypto_message changed;
    Report verifier;
    uintptr_t ptr;
    size_t length;
    size_t i;
    size_t expected;
    unsigned long response;
    unsigned char wrong_pin[32];
    unsigned char wrong_hash[64];
    static const size_t lengths[] = {0, 1, 7, 8, 32, 1023, 1024};

    call = static_cast<struct edge_call *>(buffer);
    call->return_data.call_status = CALL_STATUS_ERROR;
    if (edge_call_args_ptr(call, &ptr, &length)) {
	failed = true;
	return;
    }
    if (!length) {
	ptr = edge_call_data_ptr();
    }
    if (edge_call_check_ptr_valid(ptr, length ? length : sizeof challenge)) {
	failed = true;
	return;
    }
    if (call->call_id == CRYPTO_CHALLENGE_CALL && !issued && !length) {
	if (getrandom(challenge.nonce, 32, GRND_NONBLOCK) != 32) {
	    perror("verifier challenge getrandom");
	    failed = true;
	    return;
	}
	memcpy(reinterpret_cast<void *>(ptr), &challenge, sizeof challenge);
	if (edge_call_setup_ret(call, reinterpret_cast<void *>(ptr),
				sizeof challenge)) {
	    failed = true;
	    return;
	}
	issued = true;
    } else if (call->call_id == CRYPTO_REPORT_CALL && issued && !unavailable &&
	       !done && reports < 7 && length == sizeof message) {
	memcpy(&message, reinterpret_cast<void *>(ptr), sizeof message);
	expected = lengths[reports];
	if (message.sequence != reports ||
	    message.report.enclave.data_len != expected ||
	    !verifier.fromBytesChecked(
		reinterpret_cast<byte *>(&message.report),
		sizeof message.report) ||
	    !verifier.verify(enclave_pin, sm_pin, device_pin)) {
	    failed = true;
	    return;
	}
	for (i = 0; i < 1024; i++) {
	    unsigned char value;

	    value = i < expected
			? (i < 32 ? challenge.nonce[i]
				  : static_cast<unsigned char>(i ^ reports))
			: 0;
	    if (message.report.enclave.data[i] != value) {
		failed = true;
		return;
	    }
	}
	changed = message;
	changed.report.enclave.hash[0] ^= 1;
	verifier.fromBytesChecked(reinterpret_cast<byte *>(&changed.report),
				  sizeof changed.report);
	if (verifier.verify(enclave_pin, sm_pin, device_pin)) {
	    failed = true;
	    return;
	}
	changed = message;
	changed.report.enclave.data_len = 1025;
	if (verifier.fromBytesChecked(reinterpret_cast<byte *>(&changed.report),
				      sizeof changed.report) ||
	    verifier.checkSignaturesOnly(device_pin) ||
	    verifier.fromBytesChecked(reinterpret_cast<byte *>(&message.report),
				      1351)) {
	    failed = true;
	    return;
	}
	memcpy(wrong_pin, device_pin, 32);
	wrong_pin[0] ^= 1;
	verifier.fromBytesChecked(reinterpret_cast<byte *>(&message.report),
				  sizeof message.report);
	if (verifier.checkSignaturesOnly(wrong_pin) ||
	    verifier.fromJsonChecked("{\"enclave\":{\"datalen\":-1}}")) {
	    failed = true;
	    return;
	}
	memcpy(wrong_hash, sm_pin, 64);
	wrong_hash[0] ^= 1;
	verifier.fromBytesChecked(reinterpret_cast<byte *>(&message.report),
				  sizeof message.report);
	if (verifier.verify(enclave_pin, wrong_hash, device_pin)) {
	    failed = true;
	    return;
	}
	changed = message;
	changed.report.sm.signature[0] ^= 1;
	verifier.fromBytesChecked(reinterpret_cast<byte *>(&changed.report),
				  sizeof changed.report);
	if (verifier.checkSignaturesOnly(device_pin)) {
	    failed = true;
	    return;
	}
	changed = message;
	changed.report.enclave.signature[0] ^= 1;
	verifier.fromBytesChecked(reinterpret_cast<byte *>(&changed.report),
				  sizeof changed.report);
	if (verifier.checkSignaturesOnly(device_pin)) {
	    failed = true;
	    return;
	}
	last_report = message;
	reports++;
	response = 0;
	memcpy(reinterpret_cast<void *>(ptr), &response, sizeof response);
	if (edge_call_setup_ret(call, reinterpret_cast<void *>(ptr),
				sizeof response)) {
	    failed = true;
	    return;
	}
    } else if (call->call_id == CRYPTO_REPLAY_CALL && reports == 7 && !done &&
	       length == sizeof message) {
	memcpy(&message, reinterpret_cast<void *>(ptr), sizeof message);
	if (memcmp(&message, &last_report, sizeof message) ||
	    !verifier.fromBytesChecked(
		reinterpret_cast<byte *>(&message.report),
		sizeof message.report) ||
	    !verifier.verify(enclave_pin, sm_pin, device_pin)) {
	    failed = true;
	    return;
	}
	/* Signature-valid but already accepted nonce/length challenge. */
	response = 1;
	memcpy(reinterpret_cast<void *>(ptr), &response, sizeof response);
	if (edge_call_setup_ret(call, reinterpret_cast<void *>(ptr),
				sizeof response)) {
	    failed = true;
	    return;
	}
    } else if (call->call_id == CRYPTO_DONE_CALL && issued && !done &&
	       !length && reports == (unavailable ? 0U : 7U)) {
	done = true;
	response = 0;
	memcpy(reinterpret_cast<void *>(ptr), &response, sizeof response);
	if (edge_call_setup_ret(call, reinterpret_cast<void *>(ptr),
				sizeof response)) {
	    failed = true;
	    return;
	}
    } else {
	failed = true;
	return;
    }
    call->return_data.call_status = CALL_STATUS_OK;
}

int main(int argc, char **argv)
{
    Keystone::Enclave instance;
    Keystone::Params params;
    Keystone::Error result;
    uintptr_t retval;

    if (argc != 7) {
	fprintf(stderr, "usage: runner EAPP RUNTIME LOADER DEVICE_PIN SM_HASH "
			"KEY_DIGEST (or three '-' for unavailable)\n");
	return 2;
    }
    unavailable = !strcmp(argv[4], "-");
    if (!unavailable && (!read_exact(argv[4], device_pin, 32) ||
			 !read_exact(argv[5], sm_pin, 64) ||
			 !read_exact(argv[6], challenge.key_digest, 64))) {
	return 2;
    }
    result = Keystone::Enclave::measure(reinterpret_cast<char *>(enclave_pin),
					argv[1], argv[2], argv[3]);
    if (result != Keystone::Error::Success) {
	return 1;
    }
    params.setFreeMemSize(2 * 1024 * 1024);
    params.setUntrustedSize(256 * 1024);
    if (instance.init(argv[1], argv[2], argv[3], params) !=
	Keystone::Error::Success) {
	fprintf(stderr, "FAIL CRYPTO CREATE\n");
	return 1;
    }
    edge_call_init_internals(
	reinterpret_cast<uintptr_t>(instance.getSharedBuffer()),
	instance.getSharedBufferSize());
    if (register_call(CRYPTO_CHALLENGE_CALL, dispatch) ||
	register_call(CRYPTO_REPORT_CALL, dispatch) ||
	register_call(CRYPTO_REPLAY_CALL, dispatch) ||
	register_call(CRYPTO_DONE_CALL, dispatch)) {
	return 1;
    }
    instance.registerOcallDispatch(incoming_call_dispatch);
    retval = 99;
    result = instance.run(&retval);
    if (result != Keystone::Error::Success || retval || failed || !done) {
	fprintf(stderr, "FAIL CRYPTO SDK=%d exit=%lu reports=%u failed=%d\n",
		static_cast<int>(result), static_cast<unsigned long>(retval),
		reports, failed);
	return 1;
    }
    if (instance.destroy() != Keystone::Error::Success) {
	return 1;
    }
    printf("PASS GUEST CRYPTO: provider=%s reports=%u "
	   "CREATE/RUN/OCALL/RESUME/DESTROY; no keys exported\n",
	   unavailable ? "unavailable" : "qemu-dev", reports);
    return 0;
}
