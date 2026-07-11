// SPDX-License-Identifier: GPL-2.0-only
/*
 * edu_pbuf sysfs attributes for simple configuration and diagnostics.
 */

#include <linux/device.h>
#include <linux/errno.h>
#include <linux/kstrtox.h>
#include <linux/sysfs.h>

#include "edu_pbuf_internal.h"

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

	if (!new_limit || new_limit > edu_pbuf_capacity)
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

static const struct attribute_group edu_pbuf_group = {
	.attrs = edu_pbuf_attrs,
};

const struct attribute_group *edu_pbuf_groups[] = {
	&edu_pbuf_group,
	NULL,
};
