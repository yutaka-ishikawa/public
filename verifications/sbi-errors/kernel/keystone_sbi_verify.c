// SPDX-License-Identifier: GPL-2.0-only
/* Fixed invalid requests, forwarded to the running OpenSBI/SM via real ecalls.
 */
#include <linux/capability.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <asm/sbi.h>
#include "abi.h"
#include "sm_call.h"

static long verify_ioctl(struct file *file, unsigned int command,
			 unsigned long argument)
{
    static const unsigned long fids[KEYSTONE_VERIFY_SBI_COUNT] = {
	SBI_SM_CREATE_ENCLAVE, SBI_SM_DESTROY_ENCLAVE, SBI_SM_RUN_ENCLAVE, 2004,
	SBI_SM_RESUME_ENCLAVE};
    struct keystone_sbi_create_t invalid_layout = {0};
    struct keystone_verify_sbi result = {0};
    struct sbiret ret;
    unsigned long arg0;
    unsigned int i;

    if (command != KEYSTONE_VERIFY_SBI) {
	return -ENOTTY;
    }
    if (!capable(CAP_SYS_RAWIO)) {
	return -EPERM;
    }
    for (i = 0; i < KEYSTONE_VERIFY_SBI_COUNT; i++) {
	arg0 = i == 0 ? (unsigned long)&invalid_layout : ~0UL;
	ret = sbi_ecall(SBI_EXT_EXPERIMENTAL_KEYSTONE_ENCLAVE, fids[i], arg0, 0,
			0, 0, 0, 0);
	result.error[i] = ret.error;
	result.value[i] = ret.value;
    }
    if (copy_to_user((void __user *)argument, &result, sizeof result)) {
	return -EFAULT;
    }
    return 0;
}

static const struct file_operations verify_operations = {
    .owner = THIS_MODULE,
    .unlocked_ioctl = verify_ioctl,
};
static struct miscdevice verify_device = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = "keystone_sbi_verify",
    .fops = &verify_operations,
    .mode = 0600,
};

static int __init verify_init(void)
{
    return misc_register(&verify_device);
}

static void __exit verify_exit(void)
{
    misc_deregister(&verify_device);
}

module_init(verify_init);
module_exit(verify_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Fixed Keystone SBI integration checks; no emulation");
