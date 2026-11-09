#ifndef ROBUST_CLIENT_H
#define ROBUST_CLIENT_H
#include <stdint.h>
#include <stddef.h>
#include "shared/keystone_user.h"
#include "protocol.h"
#define ROBUST_UTM (256 * 1024)

struct client {
    int fd;
    int live;
    struct keystone_ioctl_create_enclave create;
    void *utm;
    unsigned char measurement[64];
    uintptr_t backing_address;
    uintptr_t backing_size;
    unsigned long callbacks;
    unsigned long interrupts;
};

struct configuration {
    unsigned int workers;
    unsigned int cycles;
    unsigned int timeout;
    long rng_expected;
    int crypto_enabled;
    unsigned char device_key[32];
    unsigned char sm_hash[64];
};

int client_open(struct client *client, int backing);
int client_step(struct client *client, int resume,
		struct keystone_ioctl_run_enclave *result);
int client_destroy(struct client *client);
void client_close(struct client *client);
int client_run(struct client *client, unsigned int worker,
	       unsigned int generation, const struct configuration *config);
int check_report(const struct keystone_report *report,
		 const unsigned char *nonce, const unsigned char *measurement,
		 const struct configuration *config);
int lifecycle_cases(const struct configuration *config);
int lifetime_cases(const struct configuration *config);
int fault_cases(int crypto);
int quarantine_case(unsigned int stage);
int raw_cases(const struct configuration *config);
uint64_t monotonic_ns(void);
long backing_bytes(void);
int open_fd_count(void);
#endif
