// SPDX-License-Identifier: GPL-2.0-only
#include <linux/capability.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/uaccess.h>
#include <asm/sbi.h>
#include "sm_call.h"
#include "abi.h"

/* Caller supplies the PA of a backing region owned by an existing enclave.
 * Only a root/CAP_SYS_RAWIO test process can issue this verification command.
 */
static long registered_overlap(unsigned long argument)
{
    struct multimem_overlap request;
    struct keystone_sbi_create_v2 args = {0};
    struct sbiret ret;
    unsigned long epm;
    unsigned long utm;
    bool quarantine;

    if (copy_from_user(&request, (void __user *)argument, sizeof request)) {
	return -EFAULT;
    }
    epm = __get_free_pages(GFP_KERNEL | __GFP_ZERO, 2);
    utm = __get_free_pages(GFP_KERNEL | __GFP_ZERO, 1);
    if (!epm || !utm) {
	if (epm) {
	    free_pages(epm, 2);
	}
	if (utm) {
	    free_pages(utm, 1);
	}
	return -ENOMEM;
    }
    args.version = KEYSTONE_CREATE_V2_VERSION;
    args.bytes = sizeof args;
    args.base.epm_region.paddr = __pa(epm);
    args.base.epm_region.size = 4 * PAGE_SIZE;
    args.base.utm_region.paddr = __pa(utm);
    args.base.utm_region.size = 2 * PAGE_SIZE;
    args.base.runtime_paddr = __pa(epm);
    args.base.user_paddr = __pa(epm) + PAGE_SIZE;
    args.base.free_paddr = __pa(epm) + 2 * PAGE_SIZE;
    args.backing.paddr = request.paddr;
    args.backing.size = request.size;
    ret = sbi_ecall(SBI_EXT_EXPERIMENTAL_KEYSTONE_ENCLAVE,
		    SBI_SM_CREATE_ENCLAVE_V2, (unsigned long)&args, 0, 0, 0, 0,
		    0);
    request.error = ret.error;
    quarantine = false;
    if (!ret.error) {
	ret = sbi_ecall(SBI_EXT_EXPERIMENTAL_KEYSTONE_ENCLAVE,
			SBI_SM_DESTROY_ENCLAVE, ret.value, 0, 0, 0, 0, 0);
	quarantine = ret.error != 0;
    }
    if (!quarantine) {
	free_pages(utm, 1);
	free_pages(epm, 2);
    }
    if (copy_to_user((void __user *)argument, &request, sizeof request)) {
	return -EFAULT;
    }
    return 0;
}

static long probe_ioctl(struct file *file, unsigned int command,
			unsigned long argument)
{
    struct keystone_sbi_create_v2 args;
    struct multimem_kernel_result result = {0};
    struct sbiret ret;
    unsigned long epm;
    unsigned long utm;
    unsigned int i;
    bool quarantine = false;

    if (!capable(CAP_SYS_RAWIO)) {
	return -EPERM;
    }
    if (command == MULTIMEM_KERNEL_OVERLAP) {
	return registered_overlap(argument);
    }
    if (command != MULTIMEM_KERNEL_TEST) {
	return -EPERM;
    }
    epm = __get_free_pages(GFP_KERNEL | __GFP_ZERO, 2);
    utm = __get_free_pages(GFP_KERNEL | __GFP_ZERO, 1);
    if (!epm || !utm) {
	if (epm) {
	    free_pages(epm, 2);
	}
	if (utm) {
	    free_pages(utm, 1);
	}
	return -ENOMEM;
    }
    ret = sbi_ecall(SBI_EXT_EXPERIMENTAL_KEYSTONE_ENCLAVE, SBI_SM_CALL_PLUGIN,
		    1, 1, 0, 0, 0, 0);
    result.error[0] = ret.error;
    for (i = 1; i < MULTIMEM_KERNEL_CASES; i++) {
	memset(&args, 0, sizeof args);
	args.version = KEYSTONE_CREATE_V2_VERSION;
	args.bytes = sizeof args;
	args.base.epm_region.paddr = __pa(epm);
	args.base.epm_region.size = 4 * PAGE_SIZE;
	args.base.utm_region.paddr = __pa(utm);
	args.base.utm_region.size = 2 * PAGE_SIZE;
	args.base.runtime_paddr = __pa(epm);
	args.base.user_paddr = __pa(epm) + PAGE_SIZE;
	args.base.free_paddr = __pa(epm) + 2 * PAGE_SIZE;
	args.backing.paddr = __pa(epm);
	args.backing.size = PAGE_SIZE;
	switch (i) {
	case 1:
	    args.version++;
	    break;
	case 2:
	    args.bytes--;
	    break;
	case 3:
	    args.backing.size = 0;
	    break;
	case 4:
	    args.backing.paddr++;
	    break;
	case 5:
	    args.backing.size++;
	    break;
	case 6:
	    args.backing.paddr = ~0UL - PAGE_SIZE + 1;
	    args.backing.size = 2 * PAGE_SIZE;
	    break;
	case 7:
	    break; /* EPM overlap. */
	case 8:
	    args.backing.paddr = __pa(utm);
	    break;
	case 9:
	    break; /* Reserved argument is nonzero. */
	}
	ret = sbi_ecall(SBI_EXT_EXPERIMENTAL_KEYSTONE_ENCLAVE,
			SBI_SM_CREATE_ENCLAVE_V2, (unsigned long)&args, i == 9,
			0, 0, 0, 0);
	result.error[i] = ret.error;
	if (!ret.error) {
	    ret = sbi_ecall(SBI_EXT_EXPERIMENTAL_KEYSTONE_ENCLAVE,
			    SBI_SM_DESTROY_ENCLAVE, ret.value, 0, 0, 0, 0, 0);
	    if (ret.error) {
		quarantine = true;
	    }
	}
    }
    if (!quarantine) {
	free_pages(utm, 1);
	free_pages(epm, 2);
    } else {
	pr_err("multimem probe: unexpected CREATE/DESTROY failure; pages "
	       "retained\n");
    }
    if (copy_to_user((void __user *)argument, &result, sizeof result)) {
	return -EFAULT;
    }
    return 0;
}

static const struct file_operations probe_ops = {
    .owner = THIS_MODULE,
    .unlocked_ioctl = probe_ioctl,
};
static struct miscdevice probe_device = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = "keystone_multimem_verify",
    .fops = &probe_ops,
    .mode = 0600,
};

static int __init probe_init(void)
{
    return misc_register(&probe_device);
}

static void __exit probe_exit(void)
{
    misc_deregister(&probe_device);
}

module_init(probe_init);
module_exit(probe_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Real SM FID 4000/2006 malformed request verification");
