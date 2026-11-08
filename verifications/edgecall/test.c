/* Real SDK marshalling/dispatch and runtime OCALL, with user-copy/SBI mocks. */
#define _GNU_SOURCE
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include "edge_call.h"
#include "sm_call.h"
#ifdef NDEBUG
#error "Assertions must remain enabled"
#endif

#define SHARED_BYTES 8192
static unsigned char *shared;
static unsigned char *host_shared;
static unsigned char private_input[SHARED_BYTES + 1];
static unsigned char private_output[SHARED_BYTES + 1];
static uintptr_t shared_buffer;
static size_t shared_buffer_size = SHARED_BYTES;
static size_t expected_len;
static unsigned long host_calls, stop_calls;
static int mode, groups;
static uintptr_t stop_status;
static const uintptr_t private_tag = 0x12345678;

enum {
    NORMAL,
    BAD_RETURN_OFFSET,
    BAD_RETURN_SIZE,
    HOST_ERROR,
    SHORT_RETURN,
    LONG_RETURN
};

static void host_handler(void *buffer)
{
    struct edge_call *call;
    uintptr_t ptr;
    size_t len;
    size_t i;
    size_t result_len;

    call = buffer;
    assert(edge_call_args_ptr(call, &ptr, &len) == 0);
    assert(len == expected_len);
    assert(ptr >= (uintptr_t)host_shared &&
	   ptr + len <= (uintptr_t)host_shared + SHARED_BYTES);
    assert(ptr != (uintptr_t)private_input);
    assert(memcmp((void *)ptr, private_input, len) == 0);
    host_calls++;

    if (mode == HOST_ERROR) {
	call->return_data.call_status = CALL_STATUS_ERROR;
	return;
    }
    if (mode == BAD_RETURN_OFFSET || mode == BAD_RETURN_SIZE) {
	call->return_data.call_status = CALL_STATUS_OK;
	call->return_data.call_ret_offset =
	    mode == BAD_RETURN_OFFSET ? SIZE_MAX : sizeof(struct edge_call);
	call->return_data.call_ret_size =
	    mode == BAD_RETURN_SIZE ? SIZE_MAX : 1;
	return;
    }

    result_len = len;
    if (mode == SHORT_RETURN) {
	result_len /= 2;
    }
    if (mode == LONG_RETURN) {
	result_len += 8;
    }
    for (i = 0; i < result_len; i++) {
	((unsigned char *)ptr)[i] = private_input[i] ^ 0xa5;
    }
    assert(edge_call_setup_ret(call, (void *)ptr, result_len) == 0);
    call->return_data.call_status = CALL_STATUS_OK;
}

static void copy_from_user(void *dst, const void *src, size_t len)
{
    assert(src == private_input);
    memcpy(dst, src, len);
}

static void copy_to_user(void *dst, const void *src, size_t len)
{
    assert(dst == private_output + 1);
    assert((uintptr_t)src >= shared_buffer);
    assert((uintptr_t)src + len <= shared_buffer + SHARED_BYTES);
    memcpy(dst, src, len);
}

static uintptr_t sbi_stop_enclave(unsigned long reason)
{
    assert(reason == STOP_EDGE_CALL_HOST);
    stop_calls++;
    if (stop_status) {
	return stop_status;
    }
    edge_call_init_internals((uintptr_t)host_shared, SHARED_BYTES);
    incoming_call_dispatch(host_shared);
    edge_call_init_internals(shared_buffer, SHARED_BYTES);
    return 0;
}

#include "ocall.inc"

static void reset_case(void)
{
    size_t i;

    memset(shared, 0, SHARED_BYTES);
    memset(private_output, 0xcc, sizeof(private_output));
    for (i = 0; i < sizeof(private_input); i++) {
	private_input[i] = (unsigned char)(i * 17);
    }
    expected_len = 0;
    host_calls = 0;
    stop_calls = 0;
    stop_status = 0;
    mode = NORMAL;
    edge_call_init_internals(shared_buffer, SHARED_BYTES);
    assert(register_call(1, host_handler) == 0);
}

static void passed(const char *name)
{
    groups++;
    printf("PASS EDGE %02d %s\n", groups, name);
}

int main(void)
{
    static const size_t lengths[] = {0,	 1,   7,   8,	31,  63,
				     64, 255, 256, 512, 4096};
    struct edge_call *call;
    uintptr_t ptr;
    edge_data_offset offset;
    size_t len;
    size_t i;
    size_t j;
    int variant;
    int fd;

    fd = memfd_create("edgecall-shared", 0);
    assert(fd >= 0 && ftruncate(fd, SHARED_BYTES) == 0);
    shared =
	mmap(NULL, SHARED_BYTES, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    host_shared =
	mmap(NULL, SHARED_BYTES, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    assert(shared != MAP_FAILED && host_shared != MAP_FAILED &&
	   shared != host_shared);
    shared_buffer = (uintptr_t)shared;

    for (i = 0; i < sizeof(lengths) / sizeof(lengths[0]); i++) {
	reset_case();
	expected_len = lengths[i];
	assert(dispatch_edgecall_ocall(1, private_input, expected_len,
				       private_output + 1, expected_len) == 0);
	for (j = 0; j < expected_len; j++) {
	    assert(private_output[j + 1] ==
		   (unsigned char)(private_input[j] ^ 0xa5));
	    assert(private_input[j] == (unsigned char)(j * 17));
	}
	assert(private_output[0] == 0xcc &&
	       private_output[expected_len + 1] == 0xcc);
	assert(stop_calls == 1 && host_calls == 1);
    }
    passed("11 sizes: binary data with NUL bytes, copied arguments, "
	   "transformed return, canaries");

    reset_case();
    expected_len = SHARED_BYTES - sizeof(struct edge_call);
    assert(dispatch_edgecall_ocall(1, private_input, expected_len,
				   private_output + 1, expected_len) == 0);
    passed("maximum shared payload fits exactly");

    reset_case();
    expected_len = SHARED_BYTES - sizeof(struct edge_call) + 1;
    assert(dispatch_edgecall_ocall(1, private_input, expected_len,
				   private_output + 1, expected_len) != 0);
    assert(!stop_calls && !host_calls);
    passed("one byte too many rejected before STOP/host dispatch");

    for (variant = BAD_RETURN_OFFSET; variant <= HOST_ERROR; variant++) {
	reset_case();
	expected_len = 32;
	mode = variant;
	assert(dispatch_edgecall_ocall(1, private_input, expected_len,
				       private_output + 1, 32) != 0);
	for (i = 0; i < sizeof(private_output); i++) {
	    assert(private_output[i] == 0xcc);
	}
    }
    passed("forged return offset/length and host error do not copy to private "
	   "output");

    for (variant = SHORT_RETURN; variant <= LONG_RETURN; variant++) {
	reset_case();
	expected_len = 32;
	mode = variant;
	assert(dispatch_edgecall_ocall(1, private_input, 32, private_output + 1,
				       32) == 0);
	len = variant == SHORT_RETURN ? 16 : 32;
	for (i = 0; i < len; i++) {
	    assert(private_output[i + 1] ==
		   (unsigned char)(private_input[i] ^ 0xa5));
	}
	assert(private_output[len + 1] == 0xcc);
    }
    passed("short return copies only available bytes; long return is capped at "
	   "caller length");

    reset_case();
    expected_len = 16;
    assert(dispatch_edgecall_ocall(1, private_input, 16, NULL, 0) == 0);
    passed("OCALL without a return buffer");

    reset_case();
    stop_status = 1;
    assert(dispatch_edgecall_ocall(1, private_input, 0, private_output + 1,
				   0) != 0);
    assert(!host_calls);
    passed("failed STOP does not dispatch or report success");

    reset_case();
    call = (struct edge_call *)shared;
    assert(edge_call_setup_call(call, private_input, 8) != 0);
    assert(edge_call_setup_ret(call, private_output, 8) != 0);
    passed("SDK refuses private pointers as shared argument/return ranges");

    for (i = 0; i < 4; i++) {
	reset_case();
	call = (struct edge_call *)shared;
	call->call_arg_offset = i == 0	 ? SHARED_BYTES
				: i == 1 ? SIZE_MAX
				: i == 2
				    ? SHARED_BYTES - 1
				    : (uintptr_t)private_input - shared_buffer;
	call->call_arg_size = i == 2 ? 2 : 8;
	ptr = private_tag;
	assert(edge_call_args_ptr(call, &ptr, &len) != 0);
	assert(ptr == private_tag);
    }
    passed("host argument decoder rejects "
	   "end/overflow/cross-end/private-address offsets");

    reset_case();
    assert(edge_call_get_offset_from_ptr(shared_buffer + 128, 32, &offset) ==
	   0);
    assert(offset == 128);
    ptr = 0;
    assert(edge_call_get_ptr_from_offset(offset, 32, &ptr) == 0);
    assert(ptr == shared_buffer + 128);
    passed("shared pointer/offset round trip");

    printf("SUMMARY EDGE DATA: %d groups passed (SBI/user-copy mocked; not a "
	   "PMP access test)\n",
	   groups);
    assert(munmap(shared, SHARED_BYTES) == 0);
    assert(munmap(host_shared, SHARED_BYTES) == 0);
    close(fd);
    return 0;
}
