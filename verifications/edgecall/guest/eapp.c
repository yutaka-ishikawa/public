#include "app/eapp_utils.h"
#include "app/syscall.h"
#include "protocol.h"

static struct edge_verify_message request;
static struct edge_verify_message response;

int EAPP_ENTRY main(void)
{
    static const size_t lengths[] = {0, 1, 7, 8, 31, 255, 256, 4096};
    size_t i;
    size_t j;
    size_t length;
    int result;

    for (i = 0; i < sizeof(lengths) / sizeof(lengths[0]); i++) {
	length = lengths[i];
	request.sequence = i;
	request.private_va = (uintptr_t)&request;
	request.length = length;
	for (j = 0; j < length; j++) {
	    request.bytes[j] = (unsigned char)(j * 17 + i);
	}
	for (j = 0; j < sizeof(response); j++) {
	    ((unsigned char *)&response)[j] = 0xcc;
	}
	result = ocall(EDGE_VERIFY_CALL, &request, EDGE_VERIFY_HEADER + length,
		       &response, sizeof(response));
	if (result || response.sequence != i || response.length != length) {
	    EAPP_RETURN(1);
	}
	for (j = 0; j < length; j++) {
	    if (request.bytes[j] != (unsigned char)(j * 17 + i) ||
		response.bytes[j] != (unsigned char)(request.bytes[j] ^ 0xa5)) {
		EAPP_RETURN(2);
	    }
	}
	if (length < sizeof(response.bytes) && response.bytes[length] != 0xcc) {
	    EAPP_RETURN(3);
	}
    }
    result = ocall(EDGE_VERIFY_BAD_RETURN, &request, EDGE_VERIFY_HEADER,
		   &response, sizeof(response));
    if (result == 0) {
	EAPP_RETURN(4);
    }
    EAPP_RETURN(0);
}
