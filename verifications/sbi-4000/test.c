#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include "shared/sm_err.h"
#include "kernel/abi.h"

int main(void)
{
    struct multimem_kernel_result result;
    long expected;
    unsigned int i;
    int fd;
    int disabled;

    disabled = getenv("MULTIMEM_DISABLED") != NULL;
    fd = open("/dev/keystone_multimem_verify", O_RDWR);
    if (fd < 0 || ioctl(fd, MULTIMEM_KERNEL_TEST, &result)) {
	perror("FID 4000/2006 kernel probe");
	return 1;
    }
    close(fd);
    for (i = 0; i < MULTIMEM_KERNEL_CASES; i++) {
	expected =
	    i == 0 ? SBI_ERR_SM_ENCLAVE_SBI_PROHIBITED
		   : ((i == 7 || i == 8) ? SBI_ERR_SM_ENCLAVE_REGION_OVERLAPS
					 : SBI_ERR_SM_ENCLAVE_ILLEGAL_ARGUMENT);
	if (disabled && i >= 3 && i <= 8) {
	    expected = SBI_ERR_SM_NOT_IMPLEMENTED;
	}
	if (result.error[i] != expected) {
	    fprintf(stderr, "FAIL SBI %u case=%u got=%lld expected=%ld\n",
		    i ? 2006 : 4000, i, (long long)result.error[i], expected);
	    return 1;
	}
	printf("PASS GUEST SBI %u: invalid request case=%u status=%ld\n",
	       i ? 2006 : 4000, i, expected);
    }
    return 0;
}
