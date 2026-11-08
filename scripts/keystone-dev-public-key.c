/* SPDX-License-Identifier: BSD-2-Clause */
#include <stdio.h>
#include <string.h>
#include "ed25519/ed25519.h"

static int read_file(const char *name, unsigned char *bytes, size_t size)
{
    FILE *file;
    int ok;

    file = fopen(name, "rb");
    if (!file) {
	return 0;
    }
    ok = fread(bytes, 1, size, file) == size && fgetc(file) == EOF;
    fclose(file);
    return ok;
}

int main(int argc, char **argv)
{
    unsigned char seed[32];
    unsigned char public_key[32];
    unsigned char secret[64];
    unsigned char hash[64];
    unsigned char certificate[96];
    unsigned char message[96];
    volatile unsigned char *p;
    FILE *file;
    size_t i;

    if (argc == 5 && !strcmp(argv[1], "--verify")) {
	if (!read_file(argv[2], public_key, 32) ||
	    !read_file(argv[3], hash, 64) ||
	    !read_file(argv[4], certificate, 96)) {
	    return 2;
	}
	memcpy(message, hash, 64);
	memcpy(message + 64, certificate, 32);
	return ed25519_verify(certificate + 32, message, 96, public_key) ? 0
									 : 1;
    }
    if (argc != 2) {
	return 2;
    }
    file = fopen(argv[1], "rb");
    if (!file || fread(seed, 1, 32, file) != 32 || fgetc(file) != EOF) {
	return 1;
    }
    fclose(file);
    ed25519_create_keypair(public_key, secret, seed);
    p = secret;
    for (i = 0; i < sizeof secret; i++) {
	p[i] = 0;
    }
    p = seed;
    for (i = 0; i < sizeof seed; i++) {
	p[i] = 0;
    }
    return fwrite(public_key, 1, 32, stdout) == 32 ? 0 : 1;
}
