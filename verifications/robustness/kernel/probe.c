// SPDX-License-Identifier: GPL-2.0-only
#include <linux/capability.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/moduleparam.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <asm/sbi.h>
#include "sm_call.h"
#include "abi.h"

static atomic_long_t quarantine_bytes = ATOMIC_LONG_INIT(0);
static int quarantine_get(char *buffer, const struct kernel_param *parameter)
{
    return scnprintf(buffer, PAGE_SIZE, "%ld\n", atomic_long_read(&quarantine_bytes));
}
static const struct kernel_param_ops quarantine_ops = {.get = quarantine_get};
module_param_cb(quarantine_bytes, &quarantine_ops, NULL, 0444);

#define EPM_ORDER 10
#define UTM_ORDER 6

struct owned_enclave {
    struct mutex init_lock;
    unsigned long epm;
    unsigned long utm;
    unsigned long eid;
    bool initialized;
    atomic_t live;
};

static int probe_open(struct inode *inode, struct file *file)
{
    struct owned_enclave *owned;

    if (!capable(CAP_SYS_RAWIO)) {
	return -EPERM;
    }
    owned = kzalloc(sizeof *owned, GFP_KERNEL);
    if (!owned) {
	return -ENOMEM;
    }
    mutex_init(&owned->init_lock);
    file->private_data = owned;
    return 0;
}

static long initialize(struct owned_enclave *owned, unsigned long argument)
{
    struct robust_init request;
    struct keystone_sbi_create_t args = {0};
    struct sbiret ret;
    size_t padded[3];
    size_t total;
    size_t offset;
    unsigned int i;
    long rc;

    if (copy_from_user(&request, (void __user *)argument, sizeof request)) {
	return -EFAULT;
    }
    total = 0;
    for (i = 0; i < 3; i++) {
	if (!request.lengths[i] || request.lengths[i] > 512 * PAGE_SIZE) {
	    return -EINVAL;
	}
	padded[i] = PAGE_ALIGN(request.lengths[i]);
	total += padded[i];
    }
    if (total + 512 * PAGE_SIZE > PAGE_SIZE << EPM_ORDER) {
	return -EINVAL;
    }
    mutex_lock(&owned->init_lock);
    rc = -EBUSY;
    if (owned->initialized) {
	goto out;
    }
    /* Only this module's own pages are ever passed to raw CREATE. */
    owned->epm = __get_free_pages(GFP_KERNEL | __GFP_ZERO, EPM_ORDER);
    owned->utm = __get_free_pages(GFP_KERNEL | __GFP_ZERO, UTM_ORDER);
    rc = -ENOMEM;
    if (!owned->epm || !owned->utm) {
	goto free_pages;
    }
    offset = 0;
    rc = -EFAULT;
    for (i = 0; i < 3; i++) {
	if (copy_from_user((void *)(owned->epm + offset),
			   (void __user *)(unsigned long)request.pointers[i],
			   request.lengths[i])) {
	    goto free_pages;
	}
	offset += padded[i];
    }
    args.epm_region.paddr = __pa(owned->epm);
    args.epm_region.size = PAGE_SIZE << EPM_ORDER;
    args.utm_region.paddr = __pa(owned->utm);
    args.utm_region.size = PAGE_SIZE << UTM_ORDER;
    args.runtime_paddr = args.epm_region.paddr + padded[0];
    args.user_paddr = args.runtime_paddr + padded[1];
    args.free_paddr = args.user_paddr + padded[2];
    args.free_requested = 512 * PAGE_SIZE;
    ret = sbi_ecall(SBI_EXT_EXPERIMENTAL_KEYSTONE_ENCLAVE,
		    SBI_SM_CREATE_ENCLAVE, (unsigned long)&args, 0, 0, 0, 0, 0);
    if (ret.error) {
	/* A failed PMP update may be partially applied. Never recycle these
	 * diagnostic pages on an uncertain CREATE rollback. */
	atomic_long_add((PAGE_SIZE << EPM_ORDER) + (PAGE_SIZE << UTM_ORDER), &quarantine_bytes);
        pr_err("robustness: CREATE error=%ld; diagnostic pages quarantined\n",
	       ret.error);
	owned->epm = 0;
	owned->utm = 0;
	rc = -EIO;
	goto out;
    }
    owned->eid = ret.value;
    request.eid = ret.value;
    atomic_set(&owned->live, 1);
    owned->initialized = true;
    rc = copy_to_user((void __user *)argument, &request, sizeof request)
	     ? -EFAULT
	     : 0;
    goto out;
free_pages:
    if (owned->epm) {
	free_pages(owned->epm, EPM_ORDER);
	owned->epm = 0;
    }
    if (owned->utm) {
	free_pages(owned->utm, UTM_ORDER);
	owned->utm = 0;
    }
out:
    mutex_unlock(&owned->init_lock);
    return rc;
}

static long probe_ioctl(struct file *file, unsigned int command,
			unsigned long argument)
{
    struct owned_enclave *owned;
    struct robust_call request;
    struct sbiret ret;

    if (!capable(CAP_SYS_RAWIO)) {
	return -EPERM;
    }
    owned = file->private_data;
    if (command == ROBUST_INIT) {
	return initialize(owned, argument);
    }
    if (command != ROBUST_CALL_RAW || !READ_ONCE(owned->initialized)) {
	return -EINVAL;
    }
    if (copy_from_user(&request, (void __user *)argument, sizeof request)) {
	return -EFAULT;
    }
    if (request.fid == 4900) {
	ret = sbi_ecall(SBI_EXT_EXPERIMENTAL_KEYSTONE_ENCLAVE, 4900,
			request.operation, request.stage, owned->eid,
			request.ticks, 0, 0);
    } else if (request.fid == SBI_SM_RUN_ENCLAVE ||
	       request.fid == SBI_SM_RESUME_ENCLAVE ||
	       request.fid == SBI_SM_DESTROY_ENCLAVE) {
	/* Deliberately no file mutex around the real SBI: tests must reach
	 * the SM's own exclusion, rather than the production driver mutex.
	 * The file reference retains pages until ALL ioctls have returned. */
	ret = sbi_ecall(SBI_EXT_EXPERIMENTAL_KEYSTONE_ENCLAVE, request.fid,
			owned->eid, 0, 0, 0, 0, 0);
	if (request.fid == SBI_SM_DESTROY_ENCLAVE && !ret.error) {
	    atomic_set(&owned->live, 0);
	}
    } else {
	return -EPERM;
    }
    request.error = ret.error;
    request.value = ret.value;
    return copy_to_user((void __user *)argument, &request, sizeof request)
	       ? -EFAULT
	       : 0;
}

static int probe_release(struct inode *inode, struct file *file)
{
    struct owned_enclave *owned;
    struct sbiret ret = {0};

    owned = file->private_data;
    if (atomic_read(&owned->live)) {
	ret = sbi_ecall(SBI_EXT_EXPERIMENTAL_KEYSTONE_ENCLAVE,
			SBI_SM_DESTROY_ENCLAVE, owned->eid, 0, 0, 0, 0, 0);
    }
    if (!ret.error) {
	if (owned->epm) {
	    free_pages(owned->epm, EPM_ORDER);
	}
	if (owned->utm) {
	    free_pages(owned->utm, UTM_ORDER);
	}
    } else {
	atomic_long_add((PAGE_SIZE << EPM_ORDER) + (PAGE_SIZE << UTM_ORDER), &quarantine_bytes);
        pr_err("robustness: DESTROY error=%ld; diagnostic pages quarantined\n",
	       ret.error);
    }
    kfree(owned);
    return 0;
}

static const struct file_operations operations = {
    .owner = THIS_MODULE,
    .open = probe_open,
    .unlocked_ioctl = probe_ioctl,
    .release = probe_release,
};
static struct miscdevice device = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = "keystone_robustness",
    .fops = &operations,
    .mode = 0600,
};

static int __init probe_init(void)
{
    return misc_register(&device);
}

static void __exit probe_exit(void)
{
    misc_deregister(&device);
}

module_init(probe_init);
module_exit(probe_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Owned-enclave real Keystone SM concurrency verification");
