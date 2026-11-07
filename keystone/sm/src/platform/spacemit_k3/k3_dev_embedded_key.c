/* SPDX-License-Identifier: BSD-2-Clause */
#include "k3_dev_embedded_key.h"
#if defined(K3_KEY_PROVIDER_HW_ROT_A) || defined(K3_KEY_PROVIDER_TEST_ONLY)
#error "K3 embedded development key must not impersonate a production provider"
#endif

#ifdef KEYSTONE_K3_DEV_EMBED_KEY
/* Mandatory build-time generated C source, deliberately excluded from git/ZIP. */
#include "k3_dev_key_generated.inc"
#ifndef K3_DEV_KEY_MATERIAL_PRESENT
#error "Missing generated development key material"
#endif
#endif

int k3_dev_embedded_key_copy(unsigned char secret[64], unsigned char public_key[32])
{
#ifdef KEYSTONE_K3_DEV_EMBED_KEY
    unsigned int i;
    if (!secret || !public_key)
        return -1;
    for (i = 0; i < 64; ++i)
        secret[i] = k3_dev_privkey[i];
    for (i = 0; i < 32; ++i)
        public_key[i] = k3_dev_pubkey[i];
    return 0;
#else
    (void)secret;
    (void)public_key;
    return -1;
#endif
}
