/* SPDX-License-Identifier: BSD-2-Clause
 * Directly compile the production K3 PMP IPI transport into a host harness.
 * The fake hart exits with code 77 if production code calls sbi_hart_hang().
 */
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include "../../src/k3_pmp_ipi_transport.c"

static struct sbi_scratch hart;
static struct sbi_domain domain;
static struct k3_pmp_mailbox mailbox;
static int apply_returns_error;
static int calls;
static int send_calls;

int sbi_ipi_event_create(const struct sbi_ipi_event_ops *p) { return p ? 1 : -1; }
void sbi_ipi_event_destroy(u32 id) { (void)id; }
int sbi_ipi_send_many(ulong a, ulong b, u32 c, void *d)
{ (void)a; (void)b; (void)c; (void)d; send_calls++; return 0; }
unsigned long sbi_scratch_alloc_offset(unsigned long size) { return size ? 1 : 0; }
void sbi_scratch_free_offset(unsigned long offset) { (void)offset; }
void *sbi_scratch_offset_ptr(struct sbi_scratch *s, unsigned long offset)
{ assert(s == &hart && offset != 0); return &mailbox; }
unsigned int sbi_scratch_last_hartindex(void) { return 0; }
struct sbi_scratch *sbi_hartindex_to_scratch(unsigned int i) { return i == 0 ? &hart : NULL; }
u32 sbi_hartindex_to_hartid(unsigned int i) { return i; }
int sbi_hsm_hart_interruptible_mask(struct sbi_domain *d, struct sbi_hartmask *m)
{ (void)d; m->bits = 1; return 0; }
int __sbi_hsm_hart_get_state(unsigned int i) { (void)i; return SBI_HSM_STATE_STARTED; }
struct sbi_domain *sbi_domain_thishart_ptr(void) { return &domain; }
int sbi_hartmask_test_hartindex(unsigned int i, const struct sbi_hartmask *m)
{ return !!(m->bits & (1UL << i)); }
void __attribute__((noreturn)) sbi_hart_hang(void) { _exit(77); }
static int fake_apply(int region, int operation, uint8_t perms)
{ (void)region; (void)operation; (void)perms; calls++; return apply_returns_error; }

static void run_case(int failed, int expected_exit)
{
  pid_t pid = fork();
  int status;
  assert(pid >= 0);
  if (!pid) {
    mailbox.state = K3_PMP_MB_IDLE;
    mailbox.generation = 0;
    k3_mailbox_offset = 1;
    k3_apply = fake_apply;
    apply_returns_error = failed;
    assert(k3_pmp_mailbox_submit(&mailbox, 1, 2, 3, 0) == 0);
    k3_pmp_ipi_event_process(&hart);
    assert(calls == 1);
    assert(mailbox.state == (failed ? K3_PMP_MB_FAILED : K3_PMP_MB_DONE));
    _exit(0);
  }
  assert(waitpid(pid, &status, 0) == pid);
  assert(WIFEXITED(status) && WEXITSTATUS(status) == expected_exit);
}
int main(void)
{
  run_case(0, 0);     /* Normal PMP update still returns from IPI. */
  run_case(-1, 77);   /* PMP update/readback failure never returns to S-mode. */
  {
    const struct k3_pmp_ipi_request req = { .generation = 1 };
    k3_pmp_ipi_event = 1;
    k3_mailbox_offset = 1;
    k3_apply = fake_apply;
    /* Only hart 0 exists; asking for 0 and 1 must not silently deliver to 0. */
    assert(k3_pmp_ipi_transport_send(3UL, 0, &req) == SBI_ENOTSUPP);
    assert(send_calls == 0);
    /* A request for an entirely nonexistent hart must likewise be rejected. */
    assert(k3_pmp_ipi_transport_send(2UL, 0, &req) == SBI_ENOTSUPP);
    assert(send_calls == 0);
  }
  return 0;
}
