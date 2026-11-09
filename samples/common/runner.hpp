/* Current K3 runner for the copied Keystone examples. */
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/random.h>
#include "host/Enclave.hpp"
#include "edge/edge_call.h"
#include "edge/edge_syscall.h"
#include "verifier/Report.hpp"

static bool failed;
static bool require_report;
static unsigned int reports;
static byte device_pin[32];
static byte sm_pin[64];
static byte enclave_pin[64];
static unsigned char challenge[65];
static size_t challenge_size;

static bool load_pin(const char *directory, const char *name, void *data,
		     size_t size)
{
    char path[4096];
    FILE *file;
    bool ok;

    if (snprintf(path, sizeof path, "%s/%s", directory, name) >=
	static_cast<int>(sizeof path)) {
	return false;
    }
    file = fopen(path, "rb");
    if (!file) {
	perror(path);
	return false;
    }
    ok = fread(data, 1, size, file) == size && fgetc(file) == EOF;
    fclose(file);
    return ok;
}

static void dispatch(void *buffer)
{
    struct edge_call *call;
    uintptr_t pointer;
    uintptr_t response;
    uintptr_t value;
    size_t size;
    Report report;

    call = static_cast<struct edge_call *>(buffer);
    if (call->call_id == EDGECALL_SYSCALL) {
	incoming_call_dispatch(buffer);
	return;
    }
    call->return_data.call_status = CALL_STATUS_BAD_OFFSET;
    if (edge_call_args_ptr(call, &pointer, &size)) {
	failed = true;
	return;
    }
    response = 0;
    switch (call->call_id) {
    case 1:
	if (!size) {
	    failed = true;
	    return;
	}
	if (fwrite(reinterpret_cast<void *>(pointer), 1,
		   size - (reinterpret_cast<unsigned char *>(
			       pointer)[size - 1] == 0),
		   stdout) == 0) {
	    failed = true;
	    return;
	}
	response =
	    size - (reinterpret_cast<unsigned char *>(pointer)[size - 1] == 0);
	break;
    case 2:
	if (size != sizeof value) {
	    failed = true;
	    return;
	}
	memcpy(&value, reinterpret_cast<void *>(pointer), sizeof value);
	printf("Enclave value: %lu\n", static_cast<unsigned long>(value));
	break;
    case 3:
	if (!require_report || reports ||
	    !report.fromBytesChecked(reinterpret_cast<byte *>(pointer), size) ||
	    !report.verify(enclave_pin, sm_pin, device_pin) ||
	    report.getDataSize() != challenge_size ||
	    memcmp(report.getDataSection(), challenge, challenge_size)) {
	    failed = true;
	    return;
	}
	reports++;
	printf(
	    "PASS SAMPLE ATTESTATION: signature, enclave/SM pins and nonce\n");
	break;
    case 4:
	if (size ||
	    edge_call_setup_wrapped_ret(call, challenge, challenge_size)) {
	    failed = true;
	    return;
	}
	call->return_data.call_status = CALL_STATUS_OK;
	return;
    default:
	failed = true;
	return;
    }
    pointer = edge_call_data_ptr();
    if (edge_call_check_ptr_valid(pointer, sizeof response)) {
	failed = true;
	return;
    }
    memcpy(reinterpret_cast<void *>(pointer), &response, sizeof response);
    if (edge_call_setup_ret(call, reinterpret_cast<void *>(pointer),
			    sizeof response)) {
	failed = true;
	return;
    }
    call->return_data.call_status = CALL_STATUS_OK;
}

int main(int argc, char **argv)
{
    Keystone::Enclave enclave;
    Keystone::Params params;
    Keystone::Error rc;
    uintptr_t result;
    uintptr_t expected;
    const char *public_dir;
    const char *name;
    const char *expected_arg;
    char *end;
    unsigned char random[32];
    size_t i;
    static const char hex[] = "0123456789abcdef";
    bool any;

    if (argc != 4 && argc != 5) {
	fprintf(stderr, "Usage: runner EAPP RUNTIME LOADER EXPECTED|any\n");
	return 2;
    }
    name = strrchr(argv[1], '/');
    name = name ? name + 1 : argv[1];
    require_report =
	!strcmp(name, "attestor") || !strcmp(name, "test-attestation");
    expected_arg = argc == 5 ? argv[4] : "0";
    any = !strcmp(expected_arg, "any");
    expected = strtoull(expected_arg, &end, 0);
    if (!any && (!*expected_arg || *end)) {
	return 2;
    }
    strcpy(reinterpret_cast<char *>(challenge),
	   "hellohellohellohellohellohellohellohellohellohello");
    challenge_size = strlen(reinterpret_cast<char *>(challenge)) + 1;
    if (require_report) {
	public_dir = getenv("KEYSTONE_PUBLIC_DIR");
	if (!public_dir ||
	    !load_pin(public_dir, "device-public-key.bin", device_pin, 32) ||
	    !load_pin(public_dir, "sm-hash.bin", sm_pin, 64) ||
	    Keystone::Enclave::measure(reinterpret_cast<char *>(enclave_pin),
				       argv[1], argv[2],
				       argv[3]) != Keystone::Error::Success) {
	    fprintf(stderr, "Provide trusted KEYSTONE_PUBLIC_DIR for the "
			    "booted crypto provider\n");
	    return 2;
	}
	if (!strcmp(name, "attestor")) {
	    if (getrandom(random, sizeof random, 0) != sizeof random) {
		return 1;
	    }
	    for (i = 0; i < sizeof random; i++) {
		challenge[i * 2] = hex[random[i] >> 4];
		challenge[i * 2 + 1] = hex[random[i] & 15];
	    }
	    challenge[64] = 0;
	    challenge_size = 65;
	} else {
	    memcpy(challenge, "nonce", 5);
	    challenge_size = 5;
	}
    }
    params.setFreeMemSize(2 * 1024 * 1024);
    params.setUntrustedSize(256 * 1024);
    rc = enclave.init(argv[1], argv[2], argv[3], params);
    if (rc != Keystone::Error::Success) {
	fprintf(stderr, "FAIL SAMPLE %s: CREATE SDK=%d\n", name,
		static_cast<int>(rc));
	return 1;
    }
    edge_call_init_internals(
	reinterpret_cast<uintptr_t>(enclave.getSharedBuffer()),
	enclave.getSharedBufferSize());
    enclave.registerOcallDispatch(dispatch);
    result = UINTPTR_MAX;
    rc = enclave.run(&result);
    if (rc != Keystone::Error::Success || failed ||
	(!any && result != expected) || (require_report && reports != 1)) {
	fprintf(stderr, "FAIL SAMPLE %s: SDK=%d exit=%lu expected=%s\n", name,
		static_cast<int>(rc), result, expected_arg);
	return 1;
    }
    if (enclave.destroy() != Keystone::Error::Success) {
	return 1;
    }
    printf("\nPASS SAMPLE %s: enclave exit=%lu DESTROY\n", name, result);
    return 0;
}
