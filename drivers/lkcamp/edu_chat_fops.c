// SPDX-License-Identifier: GPL-2.0-only
/*
 * edu_chat file operations: open, read, write, ioctl and poll.
 */

#include <linux/compat.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/poll.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/wait.h>

#include "edu_chat_internal.h"

static bool edu_chat_read_ready(void)
{
	return READ_ONCE(edu_chat_len) ||
	       !(READ_ONCE(edu_chat_flags) & EDU_CHAT_F_BLOCKING_READ);
}

static int edu_chat_open(struct inode *inode, struct file *file)
{
	mutex_lock(&edu_chat_lock);
	edu_chat_stats.opens++;
	edu_chat_stats.active_handles++;
	mutex_unlock(&edu_chat_lock);

	return 0;
}

static int edu_chat_release(struct inode *inode, struct file *file)
{
	mutex_lock(&edu_chat_lock);
	edu_chat_stats.closes++;
	if (edu_chat_stats.active_handles)
		edu_chat_stats.active_handles--;
	mutex_unlock(&edu_chat_lock);

	return 0;
}

static ssize_t edu_chat_read(struct file *file, char __user *buf,
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

	if (mutex_lock_interruptible(&edu_chat_lock))
		return -ERESTARTSYS;

	edu_chat_stats.read_calls++;

	for (;;) {
		flags = edu_chat_flags;
		if (edu_chat_len || !(flags & EDU_CHAT_F_BLOCKING_READ))
			break;

		if (file->f_flags & O_NONBLOCK) {
			ret = -EAGAIN;
			goto out_unlock;
		}

		/* Wait queues teach blocking I/O without busy-waiting. */
		edu_chat_stats.waits++;
		mutex_unlock(&edu_chat_lock);
		ret = wait_event_interruptible(edu_chat_wait,
					       edu_chat_read_ready());
		if (ret)
			return ret;

		if (mutex_lock_interruptible(&edu_chat_lock))
			return -ERESTARTSYS;
	}

	pos = (size_t)*ppos;
	if (pos >= edu_chat_len)
		goto out_unlock;

	available = edu_chat_len - pos;
	to_copy = min(count, available);

	/* User pointers must go through uaccess helpers. */
	if (copy_to_user(buf, edu_chat_data + pos, to_copy)) {
		ret = -EFAULT;
		goto out_unlock;
	}

	*ppos += to_copy;
	edu_chat_stats.bytes_read += to_copy;
	ret = to_copy;

	if ((flags & EDU_CHAT_F_CLEAR_ON_READ) && *ppos >= edu_chat_len) {
		edu_chat_clear_locked();
		edu_chat_stats.clears++;
		*ppos = 0;
		wake_poll = true;
	}

out_unlock:
	mutex_unlock(&edu_chat_lock);
	if (wake_poll)
		wake_up_interruptible(&edu_chat_wait);
	return ret;
}

static ssize_t edu_chat_write(struct file *file, const char __user *buf,
			      size_t count, loff_t *ppos)
{
	size_t offset = 0;
	ssize_t ret;
	bool append;

	if (mutex_lock_interruptible(&edu_chat_lock))
		return -ERESTARTSYS;

	edu_chat_stats.write_calls++;

	if (count > edu_chat_limit) {
		edu_chat_stats.failed_writes++;
		ret = -EMSGSIZE;
		goto out_unlock;
	}

	append = edu_chat_flags & EDU_CHAT_F_APPEND;
	if (append) {
		if (count > edu_chat_limit - edu_chat_len) {
			edu_chat_stats.failed_writes++;
			ret = -ENOSPC;
			goto out_unlock;
		}
		offset = edu_chat_len;
	} else {
		edu_chat_clear_locked();
	}

	/* User pointers must go through uaccess helpers. */
	if (copy_from_user(edu_chat_data + offset, buf, count)) {
		ret = -EFAULT;
		goto out_unlock;
	}

	if (append)
		edu_chat_len += count;
	else
		edu_chat_len = count;

	edu_chat_stats.bytes_written += count;
	*ppos = 0;
	ret = count;

out_unlock:
	mutex_unlock(&edu_chat_lock);
	if (ret > 0)
		wake_up_interruptible(&edu_chat_wait);
	return ret;
}

static long edu_chat_ioctl(struct file *file, unsigned int cmd,
			   unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	struct edu_chat_info info;
	struct edu_chat_stats stats;
	__u32 new_limit;
	__u32 new_flags;

	mutex_lock(&edu_chat_lock);
	edu_chat_stats.ioctl_calls++;
	mutex_unlock(&edu_chat_lock);

	/* Keep command numbers and payloads in sync with the UAPI header. */
	switch (cmd) {
	case EDU_CHAT_IOC_GET_INFO:
		memset(&info, 0, sizeof(info));

		mutex_lock(&edu_chat_lock);
		info.capacity = edu_chat_capacity;
		info.limit = edu_chat_limit;
		info.length = edu_chat_len;
		info.flags = edu_chat_flags;
		mutex_unlock(&edu_chat_lock);

		if (copy_to_user(argp, &info, sizeof(info)))
			return -EFAULT;

		return 0;

	case EDU_CHAT_IOC_CLEAR:
		mutex_lock(&edu_chat_lock);
		edu_chat_clear_locked();
		edu_chat_stats.clears++;
		mutex_unlock(&edu_chat_lock);
		wake_up_interruptible(&edu_chat_wait);
		return 0;

	case EDU_CHAT_IOC_SET_LIMIT:
		if (copy_from_user(&new_limit, argp, sizeof(new_limit)))
			return -EFAULT;

		if (!new_limit || new_limit > edu_chat_capacity)
			return -EINVAL;

		mutex_lock(&edu_chat_lock);
		edu_chat_limit = new_limit;
		if (edu_chat_len > edu_chat_limit)
			edu_chat_len = edu_chat_limit;
		mutex_unlock(&edu_chat_lock);
		wake_up_interruptible(&edu_chat_wait);
		return 0;

	case EDU_CHAT_IOC_GET_STATS:
		mutex_lock(&edu_chat_lock);
		stats = edu_chat_stats;
		mutex_unlock(&edu_chat_lock);

		if (copy_to_user(argp, &stats, sizeof(stats)))
			return -EFAULT;

		return 0;

	case EDU_CHAT_IOC_SET_FLAGS:
		if (copy_from_user(&new_flags, argp, sizeof(new_flags)))
			return -EFAULT;

		if (new_flags & ~EDU_CHAT_F_MASK)
			return -EINVAL;

		mutex_lock(&edu_chat_lock);
		WRITE_ONCE(edu_chat_flags, new_flags);
		mutex_unlock(&edu_chat_lock);
		wake_up_interruptible(&edu_chat_wait);
		return 0;

	default:
		return -ENOTTY;
	}
}

static __poll_t edu_chat_poll(struct file *file, poll_table *wait)
{
	__poll_t mask = 0;

	poll_wait(file, &edu_chat_wait, wait);

	mutex_lock(&edu_chat_lock);
	if (edu_chat_len)
		mask |= EPOLLIN | EPOLLRDNORM;
	if (edu_chat_limit &&
	    (!(edu_chat_flags & EDU_CHAT_F_APPEND) ||
	     edu_chat_len < edu_chat_limit))
		mask |= EPOLLOUT | EPOLLWRNORM;
	mutex_unlock(&edu_chat_lock);

	return mask;
}

const struct file_operations edu_chat_fops = {
	.owner		= THIS_MODULE,
	.open		= edu_chat_open,
	.release	= edu_chat_release,
	.read		= edu_chat_read,
	.write		= edu_chat_write,
	.poll		= edu_chat_poll,
	.unlocked_ioctl	= edu_chat_ioctl,
	.compat_ioctl	= compat_ptr_ioctl,
	.llseek		= noop_llseek,
};
