// SPDX-License-Identifier: GPL-2.0-only
/*
 * edu_pbuf - educational pseudo character driver.
 *
 * This driver is intentionally small and hardware-independent. It exposes
 * /dev/edu_pbuf and stores the latest byte sequence written by user space.
 */

#include <linux/cdev.h>
#include <linux/compat.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <uapi/linux/edu_pbuf.h>

#define EDU_PBUF_NAME	"edu_pbuf"

static unsigned int capacity = EDU_PBUF_DEFAULT_CAPACITY;
module_param(capacity, uint, 0444);
MODULE_PARM_DESC(capacity, "Allocated pseudo-buffer capacity in bytes");

static dev_t edu_pbuf_devt;
static struct cdev edu_pbuf_cdev;
static struct class *edu_pbuf_class;
static struct device *edu_pbuf_device;

static DEFINE_MUTEX(edu_pbuf_lock);
static char *edu_pbuf_data;
static size_t edu_pbuf_len;
static size_t edu_pbuf_limit;

static ssize_t edu_pbuf_read(struct file *file, char __user *buf,
			     size_t count, loff_t *ppos)
{
	size_t available;
	size_t pos;
	size_t to_copy;
	ssize_t ret = 0;

	if (*ppos < 0)
		return -EINVAL;

	if (mutex_lock_interruptible(&edu_pbuf_lock))
		return -ERESTARTSYS;

	pos = (size_t)*ppos;
	if (pos >= edu_pbuf_len)
		goto out_unlock;

	available = edu_pbuf_len - pos;
	to_copy = min(count, available);

	if (copy_to_user(buf, edu_pbuf_data + pos, to_copy)) {
		ret = -EFAULT;
		goto out_unlock;
	}

	*ppos += to_copy;
	ret = to_copy;

out_unlock:
	mutex_unlock(&edu_pbuf_lock);
	return ret;
}

static ssize_t edu_pbuf_write(struct file *file, const char __user *buf,
			      size_t count, loff_t *ppos)
{
	ssize_t ret;

	if (mutex_lock_interruptible(&edu_pbuf_lock))
		return -ERESTARTSYS;

	if (count > edu_pbuf_limit) {
		ret = -EMSGSIZE;
		goto out_unlock;
	}

	memset(edu_pbuf_data, 0, capacity);

	if (copy_from_user(edu_pbuf_data, buf, count)) {
		ret = -EFAULT;
		goto out_unlock;
	}

	edu_pbuf_len = count;
	*ppos = 0;
	ret = count;

out_unlock:
	mutex_unlock(&edu_pbuf_lock);
	return ret;
}

static long edu_pbuf_ioctl(struct file *file, unsigned int cmd,
			   unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	struct edu_pbuf_info info;
	__u32 new_limit;

	switch (cmd) {
	case EDU_PBUF_IOC_GET_INFO:
		memset(&info, 0, sizeof(info));

		mutex_lock(&edu_pbuf_lock);
		info.capacity = capacity;
		info.limit = edu_pbuf_limit;
		info.length = edu_pbuf_len;
		mutex_unlock(&edu_pbuf_lock);

		if (copy_to_user(argp, &info, sizeof(info)))
			return -EFAULT;

		return 0;

	case EDU_PBUF_IOC_CLEAR:
		mutex_lock(&edu_pbuf_lock);
		memset(edu_pbuf_data, 0, capacity);
		edu_pbuf_len = 0;
		mutex_unlock(&edu_pbuf_lock);
		return 0;

	case EDU_PBUF_IOC_SET_LIMIT:
		if (copy_from_user(&new_limit, argp, sizeof(new_limit)))
			return -EFAULT;

		if (!new_limit || new_limit > capacity)
			return -EINVAL;

		mutex_lock(&edu_pbuf_lock);
		edu_pbuf_limit = new_limit;
		if (edu_pbuf_len > edu_pbuf_limit)
			edu_pbuf_len = edu_pbuf_limit;
		mutex_unlock(&edu_pbuf_lock);
		return 0;

	default:
		return -ENOTTY;
	}
}

static const struct file_operations edu_pbuf_fops = {
	.owner		= THIS_MODULE,
	.read		= edu_pbuf_read,
	.write		= edu_pbuf_write,
	.unlocked_ioctl	= edu_pbuf_ioctl,
	.compat_ioctl	= compat_ptr_ioctl,
	.llseek		= noop_llseek,
};

static int __init edu_pbuf_init(void)
{
	int ret;

	if (!capacity || capacity > EDU_PBUF_MAX_CAPACITY)
		return -EINVAL;

	edu_pbuf_data = kzalloc(capacity, GFP_KERNEL);
	if (!edu_pbuf_data)
		return -ENOMEM;

	edu_pbuf_limit = capacity;

	ret = alloc_chrdev_region(&edu_pbuf_devt, 0, 1, EDU_PBUF_NAME);
	if (ret)
		goto err_free_buffer;

	cdev_init(&edu_pbuf_cdev, &edu_pbuf_fops);
	ret = cdev_add(&edu_pbuf_cdev, edu_pbuf_devt, 1);
	if (ret)
		goto err_unregister_chrdev;

	edu_pbuf_class = class_create(EDU_PBUF_NAME);
	if (IS_ERR(edu_pbuf_class)) {
		ret = PTR_ERR(edu_pbuf_class);
		goto err_del_cdev;
	}

	edu_pbuf_device = device_create(edu_pbuf_class, NULL, edu_pbuf_devt,
					NULL, EDU_PBUF_NAME);
	if (IS_ERR(edu_pbuf_device)) {
		ret = PTR_ERR(edu_pbuf_device);
		goto err_destroy_class;
	}

	pr_info("edu_pbuf: registered /dev/%s major=%u minor=%u capacity=%u\n",
		EDU_PBUF_NAME, MAJOR(edu_pbuf_devt), MINOR(edu_pbuf_devt),
		capacity);
	return 0;

err_destroy_class:
	class_destroy(edu_pbuf_class);
err_del_cdev:
	cdev_del(&edu_pbuf_cdev);
err_unregister_chrdev:
	unregister_chrdev_region(edu_pbuf_devt, 1);
err_free_buffer:
	kfree(edu_pbuf_data);
	edu_pbuf_data = NULL;
	return ret;
}

static void __exit edu_pbuf_exit(void)
{
	device_destroy(edu_pbuf_class, edu_pbuf_devt);
	class_destroy(edu_pbuf_class);
	cdev_del(&edu_pbuf_cdev);
	unregister_chrdev_region(edu_pbuf_devt, 1);
	kfree(edu_pbuf_data);
	pr_info("edu_pbuf: unregistered\n");
}

module_init(edu_pbuf_init);
module_exit(edu_pbuf_exit);

MODULE_AUTHOR("Rodrigo");
MODULE_DESCRIPTION("Educational pseudo buffer character driver");
MODULE_LICENSE("GPL");
