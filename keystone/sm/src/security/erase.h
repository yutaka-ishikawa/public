/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef KEYSTONE_SECRET_ERASE_H
#define KEYSTONE_SECRET_ERASE_H
#include <sbi/sbi_types.h>

static inline void keystone_secret_erase(void *buffer, size_t size)
{
    volatile unsigned char *p;

    p = buffer;
    while (size--) {
	*p++ = 0;
    }
}
#endif
