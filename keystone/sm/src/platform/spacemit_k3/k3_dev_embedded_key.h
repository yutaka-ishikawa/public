/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef K3_DEV_EMBEDDED_KEY_H
#define K3_DEV_EMBEDDED_KEY_H
/* Development-only key-copy backend.  Never confers hardware attestation. */
int k3_dev_embedded_key_copy(unsigned char secret[64], unsigned char public_key[32]);
#endif
