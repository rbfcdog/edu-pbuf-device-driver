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
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/uaccess.h>
#include <linux/wait.h>
#include <uapi/linux/edu_pbuf.h>

#define EDU_PBUF_NAME	"edu_pbuf"

static unsigned int capacity = EDU_PBUF_DEFAULT_CAPACITY;
module_param(capacity, uint, 0444);
MODULE_PARM_DESC(capacity, "Allocated pseudo-buffer capacity in bytes");

static dev_t edu_pbuf_devt;
static struct cdev edu_pbuf_cdev;
static struct class *edu_pbuf_class;
static struct device *edu_pbuf_device;

/* Serializes buffer content, metadata, flags and statistics. */
static DEFINE_MUTEX(edu_pbuf_lock);
static wait_queue_head_t edu_pbuf_wait;
static char *edu_pbuf_data;
static size_t edu_pbuf_len;
static size_t edu_pbuf_limit;
static unsigned int edu_pbuf_flags;
static struct edu_pbuf_stats edu_pbuf_stats;

static bool edu_pbuf_read_ready(void)
{
	return READ_ONCE(edu_pbuf_len) ||
	       !(READ_ONCE(edu_pbuf_flags) & EDU_PBUF_F_BLOCKING_READ);
}

static void edu_pbuf_clear_locked(void)
{
	memset(edu_pbuf_data, 0, capacity);
	edu_pbuf_len = 0;
}

static int edu_pbuf_open(struct inode *inode, struct file *file)
{
	mutex_lock(&edu_pbuf_lock);
	edu_pbuf_stats.opens++;
	edu_pbuf_stats.active_handles++;
	mutex_unlock(&edu_pbuf_lock);

	return 0;
}

static int edu_pbuf_release(struct inode *inode, struct file *file)
{
	mutex_lock(&edu_pbuf_lock);
	edu_pbuf_stats.closes++;
	if (edu_pbuf_stats.active_handles)
		edu_pbuf_stats.active_handles--;
	mutex_unlock(&edu_pbuf_lock);

	return 0;
}

static ssize_t edu_pbuf_read(struct file *file, char __user *buf,
			     size_t count, loff_t *ppos)
{
	size_t available;
	size_t pos;
	size_t to_copy;
	unsigned int flags;
	ssize_t ret = 0;
	bool wake_poll = false;

	if (*ppos < 0)
		return -EINVAL;

	if (mutex_lock_interruptible(&edu_pbuf_lock))
		return -ERESTARTSYS;

	edu_pbuf_stats.read_calls++;

	for (;;) {
		flags = edu_pbuf_flags;
		if (edu_pbuf_len || !(flags & EDU_PBUF_F_BLOCKING_READ))
			break;

		if (file->f_flags & O_NONBLOCK) {
			ret = -EAGAIN;
			goto out_unlock;
		}

		/* Wait queues teach blocking I/O without busy-waiting. */
		edu_pbuf_stats.waits++;
		mutex_unlock(&edu_pbuf_lock);
		ret = wait_event_interruptible(edu_pbuf_wait,
					       edu_pbuf_read_ready());
		if (ret)
			return ret;

		if (mutex_lock_interruptible(&edu_pbuf_lock))
			return -ERESTARTSYS;
	}

	pos = (size_t)*ppos;
	if (pos >= edu_pbuf_len)
		goto out_unlock;

	available = edu_pbuf_len - pos;
	to_copy = min(count, available);

	/* User pointers must go through uaccess helpers. */
	if (copy_to_user(buf, edu_pbuf_data + pos, to_copy)) {
		ret = -EFAULT;
		goto out_unlock;
	}

	*ppos += to_copy;
	edu_pbuf_stats.bytes_read += to_copy;
	ret = to_copy;

	if ((flags & EDU_PBUF_F_CLEAR_ON_READ) && *ppos >= edu_pbuf_len) {
		edu_pbuf_clear_locked();
		edu_pbuf_stats.clears++;
		*ppos = 0;
		wake_poll = true;
	}

out_unlock:
	mutex_unlock(&edu_pbuf_lock);
	if (wake_poll)
		wake_up_interruptible(&edu_pbuf_wait);
	return ret;
}

static ssize_t edu_pbuf_write(struct file *file, const char __user *buf,
			      size_t count, loff_t *ppos)
{
	size_t offset = 0;
	ssize_t ret;
	bool append;

	if (mutex_lock_interruptible(&edu_pbuf_lock))
		return -ERESTARTSYS;

	edu_pbuf_stats.write_calls++;

	if (count > edu_pbuf_limit) {
		edu_pbuf_stats.failed_writes++;
		ret = -EMSGSIZE;
		goto out_unlock;
	}

	append = edu_pbuf_flags & EDU_PBUF_F_APPEND;
	if (append) {
		if (count > edu_pbuf_limit - edu_pbuf_len) {
			edu_pbuf_stats.failed_writes++;
			ret = -ENOSPC;
			goto out_unlock;
		}
		offset = edu_pbuf_len;
	} else {
		edu_pbuf_clear_locked();
	}

	/* User pointers must go through uaccess helpers. */
	if (copy_from_user(edu_pbuf_data + offset, buf, count)) {
		ret = -EFAULT;
		goto out_unlock;
	}

	if (append)
		edu_pbuf_len += count;
	else
		edu_pbuf_len = count;

	edu_pbuf_stats.bytes_written += count;
	*ppos = 0;
	ret = count;

out_unlock:
	mutex_unlock(&edu_pbuf_lock);
	if (ret > 0)
		wake_up_interruptible(&edu_pbuf_wait);
	return ret;
}

static long edu_pbuf_ioctl(struct file *file, unsigned int cmd,
			   unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	struct edu_pbuf_info info;
	struct edu_pbuf_stats stats;
	__u32 new_limit;
	__u32 new_flags;

	mutex_lock(&edu_pbuf_lock);
	edu_pbuf_stats.ioctl_calls++;
	mutex_unlock(&edu_pbuf_lock);

	/* Keep command numbers and payloads in sync with the UAPI header. */
	switch (cmd) {
	case EDU_PBUF_IOC_GET_INFO:
		memset(&info, 0, sizeof(info));

		mutex_lock(&edu_pbuf_lock);
		info.capacity = capacity;
		info.limit = edu_pbuf_limit;
		info.length = edu_pbuf_len;
		info.flags = edu_pbuf_flags;
		mutex_unlock(&edu_pbuf_lock);

		if (copy_to_user(argp, &info, sizeof(info)))
			return -EFAULT;

		return 0;

	case EDU_PBUF_IOC_CLEAR:
		mutex_lock(&edu_pbuf_lock);
		edu_pbuf_clear_locked();
		edu_pbuf_stats.clears++;
		mutex_unlock(&edu_pbuf_lock);
		wake_up_interruptible(&edu_pbuf_wait);
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
		wake_up_interruptible(&edu_pbuf_wait);
		return 0;

	case EDU_PBUF_IOC_GET_STATS:
		mutex_lock(&edu_pbuf_lock);
		stats = edu_pbuf_stats;
		mutex_unlock(&edu_pbuf_lock);

		if (copy_to_user(argp, &stats, sizeof(stats)))
			return -EFAULT;

		return 0;

	case EDU_PBUF_IOC_SET_FLAGS:
		if (copy_from_user(&new_flags, argp, sizeof(new_flags)))
			return -EFAULT;

		if (new_flags & ~EDU_PBUF_F_MASK)
			return -EINVAL;

		mutex_lock(&edu_pbuf_lock);
		WRITE_ONCE(edu_pbuf_flags, new_flags);
		mutex_unlock(&edu_pbuf_lock);
		wake_up_interruptible(&edu_pbuf_wait);
		return 0;

	default:
		return -ENOTTY;
	}
}

static __poll_t edu_pbuf_poll(struct file *file, poll_table *wait)
{
	__poll_t mask = 0;

	poll_wait(file, &edu_pbuf_wait, wait);

	mutex_lock(&edu_pbuf_lock);
	if (edu_pbuf_len)
		mask |= EPOLLIN | EPOLLRDNORM;
	if (edu_pbuf_limit &&
	    (!(edu_pbuf_flags & EDU_PBUF_F_APPEND) ||
	     edu_pbuf_len < edu_pbuf_limit))
		mask |= EPOLLOUT | EPOLLWRNORM;
	mutex_unlock(&edu_pbuf_lock);

	return mask;
}

static ssize_t limit_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	ssize_t ret;

	mutex_lock(&edu_pbuf_lock);
	ret = sysfs_emit(buf, "%zu\n", edu_pbuf_limit);
	mutex_unlock(&edu_pbuf_lock);

	return ret;
}

static ssize_t limit_store(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count)
{
	unsigned int new_limit;

	if (kstrtouint(buf, 0, &new_limit))
		return -EINVAL;

	if (!new_limit || new_limit > capacity)
		return -EINVAL;

	mutex_lock(&edu_pbuf_lock);
	edu_pbuf_limit = new_limit;
	if (edu_pbuf_len > edu_pbuf_limit)
		edu_pbuf_len = edu_pbuf_limit;
	mutex_unlock(&edu_pbuf_lock);
	wake_up_interruptible(&edu_pbuf_wait);

	return count;
}
static DEVICE_ATTR_RW(limit);

static ssize_t flags_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	ssize_t ret;

	mutex_lock(&edu_pbuf_lock);
	ret = sysfs_emit(buf, "0x%x\n", edu_pbuf_flags);
	mutex_unlock(&edu_pbuf_lock);

	return ret;
}

static ssize_t flags_store(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count)
{
	unsigned int new_flags;

	if (kstrtouint(buf, 0, &new_flags))
		return -EINVAL;

	if (new_flags & ~EDU_PBUF_F_MASK)
		return -EINVAL;

	mutex_lock(&edu_pbuf_lock);
	WRITE_ONCE(edu_pbuf_flags, new_flags);
	mutex_unlock(&edu_pbuf_lock);
	wake_up_interruptible(&edu_pbuf_wait);

	return count;
}
static DEVICE_ATTR_RW(flags);

static ssize_t length_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	ssize_t ret;

	mutex_lock(&edu_pbuf_lock);
	ret = sysfs_emit(buf, "%zu\n", edu_pbuf_len);
	mutex_unlock(&edu_pbuf_lock);

	return ret;
}
static DEVICE_ATTR_RO(length);

static ssize_t stats_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct edu_pbuf_stats stats;

	mutex_lock(&edu_pbuf_lock);
	stats = edu_pbuf_stats;
	mutex_unlock(&edu_pbuf_lock);

	return sysfs_emit(buf,
			  "opens=%llu\n"
			  "closes=%llu\n"
			  "active_handles=%u\n"
			  "read_calls=%llu\n"
			  "write_calls=%llu\n"
			  "bytes_read=%llu\n"
			  "bytes_written=%llu\n"
			  "ioctl_calls=%llu\n"
			  "waits=%llu\n"
			  "clears=%u\n"
			  "failed_writes=%u\n",
			  stats.opens, stats.closes, stats.active_handles,
			  stats.read_calls, stats.write_calls, stats.bytes_read,
			  stats.bytes_written, stats.ioctl_calls, stats.waits,
			  stats.clears, stats.failed_writes);
}
static DEVICE_ATTR_RO(stats);

static ssize_t clear_store(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count)
{
	if (!sysfs_streq(buf, "1") && !sysfs_streq(buf, "clear"))
		return -EINVAL;

	mutex_lock(&edu_pbuf_lock);
	edu_pbuf_clear_locked();
	edu_pbuf_stats.clears++;
	mutex_unlock(&edu_pbuf_lock);
	wake_up_interruptible(&edu_pbuf_wait);

	return count;
}
static DEVICE_ATTR_WO(clear);

static struct attribute *edu_pbuf_attrs[] = {
	&dev_attr_limit.attr,
	&dev_attr_flags.attr,
	&dev_attr_length.attr,
	&dev_attr_stats.attr,
	&dev_attr_clear.attr,
	NULL,
};
ATTRIBUTE_GROUPS(edu_pbuf);

static const struct file_operations edu_pbuf_fops = {
	.owner		= THIS_MODULE,
	.open		= edu_pbuf_open,
	.release	= edu_pbuf_release,
	.read		= edu_pbuf_read,
	.write		= edu_pbuf_write,
	.poll		= edu_pbuf_poll,
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
	init_waitqueue_head(&edu_pbuf_wait);

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

	/* Create /dev/edu_pbuf automatically for the classroom demo. */
	edu_pbuf_device = device_create(edu_pbuf_class, NULL, edu_pbuf_devt,
					NULL, EDU_PBUF_NAME);
	if (IS_ERR(edu_pbuf_device)) {
		ret = PTR_ERR(edu_pbuf_device);
		goto err_destroy_class;
	}

	ret = sysfs_create_groups(&edu_pbuf_device->kobj, edu_pbuf_groups);
	if (ret)
		goto err_destroy_device;

	pr_info("edu_pbuf: registered /dev/%s major=%u minor=%u capacity=%u\n",
		EDU_PBUF_NAME, MAJOR(edu_pbuf_devt), MINOR(edu_pbuf_devt),
		capacity);
	return 0;

err_destroy_device:
	device_destroy(edu_pbuf_class, edu_pbuf_devt);
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
	sysfs_remove_groups(&edu_pbuf_device->kobj, edu_pbuf_groups);
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
