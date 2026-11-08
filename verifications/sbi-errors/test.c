#include <fcntl.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include "kernel/abi.h"
#include "sm_err.h"

int main(void)
{
    static const long expected[KEYSTONE_VERIFY_SBI_COUNT] = {
	SBI_ERR_SM_ENCLAVE_ILLEGAL_ARGUMENT, SBI_ERR_SM_ENCLAVE_INVALID_ID,
	SBI_ERR_SM_ENCLAVE_INVALID_ID, SBI_ERR_SM_NOT_IMPLEMENTED,
	SBI_ERR_SM_ENCLAVE_INVALID_ID};
    static const unsigned int fids[KEYSTONE_VERIFY_SBI_COUNT] = {
	2001, 2002, 2003, 2004, 2005};
    struct keystone_verify_sbi result;
    unsigned int i;
    int fd;
    int failed;

    fd = open("/dev/keystone_sbi_verify", O_RDWR);
    if (fd < 0) {
	perror("open real SBI verification device");
	return 1;
    }
    if (ioctl(fd, KEYSTONE_VERIFY_SBI, &result)) {
	perror("real SBI verification ioctl");
	close(fd);
	return 1;
    }
    close(fd);
    failed = 0;
    for (i = 0; i < KEYSTONE_VERIFY_SBI_COUNT; i++) {
	printf("%s GUEST SBI %u: error=%ld expected=%ld value=%ld\n",
	       result.error[i] == expected[i] ? "PASS" : "FAIL", fids[i],
	       (long)result.error[i], expected[i], (long)result.value[i]);
	if (result.error[i] != expected[i]) {
	    failed = 1;
	}
    }
    return failed;
}
