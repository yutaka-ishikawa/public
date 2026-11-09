// Derived from keystone/examples/tests/untrusted; see ../LICENSE.
#include "app/eapp_utils.h"
#include "app/string.h"
#include "app/syscall.h"
#include "app/malloc.h"
#include "edge_wrapper.h"

int EAPP_ENTRY main(void)
{
    char *msg = "hello world!\n";
    char *msg2 = "2nd hello world!\n";
    struct edge_data package;
    unsigned long ret;
    char *host_string;
    size_t i;
    unsigned long count;

    edge_init();
    ret = ocall_print_buffer(msg, 13);
    ocall_print_buffer(msg2, 17);
    ocall_print_value(ret);
    ocall_get_string(&package);
    if (!package.size || package.size > 4096) {
	EAPP_RETURN(-1);
    }
    host_string = malloc(package.size);
    if (!host_string ||
	copy_from_shared(host_string, package.offset, package.size)) {
	EAPP_RETURN(-1);
    }
    count = 0;
    for (i = 0; i < package.size; i++) {
	if (host_string[i] == 'l') {
	    count++;
	}
    }
    ocall_print_value(count);
    free(host_string);
    EAPP_RETURN(ret);
}
