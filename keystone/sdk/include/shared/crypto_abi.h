/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef KEYSTONE_CRYPTO_ABI_H
#define KEYSTONE_CRYPTO_ABI_H
#include <stdint.h>
#include <stddef.h>
#define KEYSTONE_ATTEST_DATA_MAX 1024
#define KEYSTONE_KEY_IDENTIFIER_MAX 4096
#define KEYSTONE_REPORT_BYTES 1352
#define KEYSTONE_SEAL_BYTES 192

struct keystone_enclave_report {
    uint8_t hash[64];
    uint64_t data_len;
    uint8_t data[KEYSTONE_ATTEST_DATA_MAX];
    uint8_t signature[64];
};

struct keystone_sm_report {
    uint8_t hash[64];
    uint8_t public_key[32];
    uint8_t signature[64];
};

struct keystone_report {
    struct keystone_enclave_report enclave;
    struct keystone_sm_report sm;
    uint8_t dev_public_key[32];
};
#ifdef __cplusplus
static_assert(sizeof(struct keystone_report) == KEYSTONE_REPORT_BYTES,
	      "report ABI");
static_assert(offsetof(struct keystone_report, sm) == 1160, "report offset");
#else
_Static_assert(sizeof(struct keystone_report) == KEYSTONE_REPORT_BYTES,
	       "report ABI");
_Static_assert(offsetof(struct keystone_report, sm) == 1160, "report offset");
#endif
#endif
