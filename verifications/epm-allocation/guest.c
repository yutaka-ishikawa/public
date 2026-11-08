#define _FILE_OFFSET_BITS 64
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include "keystone_user.h"

/* Allocates driver EPM only; does not finalize an enclave or test SM/PMP. */
static int check_size(unsigned long mib)
{
    struct keystone_ioctl_create_enclave args;
    unsigned char *ptr;
    size_t offsets[3];
    size_t i;
    int fd;
    int result;

    memset(&args, 0, sizeof args);
    args.min_pages = mib * 256;
    fd = open("/dev/keystone_enclave", O_RDWR);
    if (fd < 0) {
	perror("open keystone");
	return 1;
    }
    if (ioctl(fd, KEYSTONE_IOC_CREATE_ENCLAVE, &args) < 0) {
	fprintf(stderr, "FAIL EPM %lu MiB: %s\n", mib, strerror(errno));
	close(fd);
	return 1;
    }
    result = 0;
    if (args.epm_size < (mib << 20) || args.epm_paddr < 0x100000000UL) {
	fprintf(stderr, "FAIL size or K3 physical address\n");
	result = 1;
    }
    offsets[0] = 0;
    offsets[1] = (args.epm_size / 2) & ~(size_t)4095;
    offsets[2] = args.epm_size - 4096;
    for (i = 0; i < 3; i++) {
	ptr = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
		   (off_t)offsets[i]);
	if (ptr == MAP_FAILED) {
	    perror("mmap EPM page");
	    result = 1;
	    break;
	}
	if (ptr[0] || ptr[4095]) {
	    fprintf(stderr, "FAIL EPM not zeroed\n");
	    result = 1;
	}
	ptr[0] = 0x5a;
	ptr[4095] = 0xa5;
	if (ptr[0] != 0x5a || ptr[4095] != 0xa5) {
	    result = 1;
	}
	if (munmap(ptr, 4096)) {
	    perror("munmap");
	    result = 1;
	}
    }
    if (ioctl(fd, KEYSTONE_IOC_DESTROY_ENCLAVE, &args) < 0) {
	perror("destroy EPM");
	result = 1;
    }
    close(fd);
    if (!result) {
	printf("PASS GUEST EPM: requested=%lu MiB allocated=%lu bytes "
	       "CPU PA=0x%lx; sampled R/W and released\n",
	       mib, (unsigned long)args.epm_size,
	       (unsigned long)args.epm_paddr);
    }
    return result;
}

int main(void)
{
    unsigned int repeat;

    for (repeat = 0; repeat < 3; repeat++) {
	if (check_size(4) || check_size(256) || check_size(257)) {
	    return 1;
	}
    }
    puts("PASS GUEST EPM: 3 allocation/release cycles for 4/256/257 MiB");
    return 0;
}
