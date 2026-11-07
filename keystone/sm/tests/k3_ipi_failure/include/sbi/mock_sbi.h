/* Host-only stubs for compilation of the actual K3 PMP IPI adapter. */
#ifndef K3_IPI_TEST_MOCK_SBI_H
#define K3_IPI_TEST_MOCK_SBI_H
#include <stddef.h>
#include <stdint.h>
typedef uint32_t u32;
typedef unsigned long ulong;
struct sbi_scratch { unsigned int hartindex; };
struct sbi_domain { int dummy; };
struct sbi_hartmask { ulong bits; };
struct sbi_ipi_event_ops {
  const char *name;
  int (*update)(struct sbi_scratch *, struct sbi_scratch *, u32, void *);
  void (*sync)(struct sbi_scratch *);
  void (*process)(struct sbi_scratch *);
};
#define SBI_IPI_UPDATE_SUCCESS 0
#define SBI_EALREADY (-10)
#define SBI_ENOMEM (-11)
#define SBI_ENOTSUPP (-12)
#define SBI_EINVALID_STATE (-13)
#define SBI_EINVAL (-14)
#define SBI_EFAIL (-15)
#define SBI_HSM_STATE_STARTED 2
int sbi_ipi_event_create(const struct sbi_ipi_event_ops *);
void sbi_ipi_event_destroy(u32);
int sbi_ipi_send_many(ulong, ulong, u32, void *);
unsigned long sbi_scratch_alloc_offset(unsigned long);
void sbi_scratch_free_offset(unsigned long);
void *sbi_scratch_offset_ptr(struct sbi_scratch *, unsigned long);
unsigned int sbi_scratch_last_hartindex(void);
struct sbi_scratch *sbi_hartindex_to_scratch(unsigned int);
u32 sbi_hartindex_to_hartid(unsigned int);
int sbi_hsm_hart_interruptible_mask(struct sbi_domain *, struct sbi_hartmask *);
int __sbi_hsm_hart_get_state(unsigned int);
struct sbi_domain *sbi_domain_thishart_ptr(void);
int sbi_hartmask_test_hartindex(unsigned int, const struct sbi_hartmask *);
void __attribute__((noreturn)) sbi_hart_hang(void);
#endif
