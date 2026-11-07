/* SPDX-License-Identifier: BSD-2-Clause */
/* Host-only helper used by k3_generate_embedded_dev_key.py.
 * Uses the same SHA3-based Ed25519-like keypair code as the original SM;
 * standard openssl Ed25519 public keys are NOT compatible here. */
#include <stdio.h>
#include <string.h>
#include "../src/ed25519/ed25519.h"

int main(void)
{
    unsigned char seed[32], public_key[32], private_key[64];
    if (fread(seed, 1, sizeof(seed), stdin) != sizeof(seed))
        return 1;
    ed25519_create_keypair(public_key, private_key, seed);
    memset(seed, 0, sizeof(seed));
    if (fwrite(public_key, 1, sizeof(public_key), stdout) != sizeof(public_key) ||
        fwrite(private_key, 1, sizeof(private_key), stdout) != sizeof(private_key))
        return 2;
    memset(private_key, 0, sizeof(private_key));
    return 0;
}
