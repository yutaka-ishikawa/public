/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef KEYSTONE_SECURITY_H
#define KEYSTONE_SECURITY_H
#include <sbi/sbi_types.h>
#define KEYSTONE_KEY_ID_MAX 4096
int keystone_security_init(void);
int keystone_security_ready(void);
unsigned long keystone_security_call(unsigned long fid, uintptr_t output,
				     uintptr_t input, size_t length);
int keystone_security_snapshot(unsigned int eid, unsigned char hash[64],
			       uintptr_t *base, uint64_t *size);
#endif
