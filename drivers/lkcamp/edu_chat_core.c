// SPDX-License-Identifier: GPL-2.0-only
/*
 * edu_chat - educational local chat pseudo character driver.
 *
 * Core module lifecycle and character-device registration.
 */

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "edu_chat_internal.h"

unsigned int edu_chat_capacity = EDU_CHAT_DEFAULT_CAPACITY;
module_param_named(capacity, edu_chat_capacity, uint, 0444);
MODULE_PARM_DESC(capacity, "Allocated chat buffer capacity in bytes");

static dev_t edu_chat_devt;
static struct cdev edu_chat_cdev;
static struct class *edu_chat_class;
static struct device *edu_chat_device;

/* Serializes chat messages, metadata, flags and statistics. */
DEFINE_MUTEX(edu_chat_lock);
wait_queue_head_t edu_chat_wait;
char *edu_chat_data;
size_t edu_chat_len;
size_t edu_chat_limit;
unsigned int edu_chat_flags;
struct edu_chat_stats edu_chat_stats;

void edu_chat_clear_locked(void)
{
	memset(edu_chat_data, 0, edu_chat_capacity);
	edu_chat_len = 0;
}

static char *edu_chat_devnode(const struct device *dev, umode_t *mode)
{
	if (mode)
		*mode = 0666;
	return NULL;
}

static int __init edu_chat_init(void)
{
	int ret;

	if (!edu_chat_capacity || edu_chat_capacity > EDU_CHAT_MAX_CAPACITY)
		return -EINVAL;

	edu_chat_data = kzalloc(edu_chat_capacity, GFP_KERNEL);
	if (!edu_chat_data)
		return -ENOMEM;

	edu_chat_limit = edu_chat_capacity;
	init_waitqueue_head(&edu_chat_wait);

	ret = alloc_chrdev_region(&edu_chat_devt, 0, 1, EDU_CHAT_NAME);
	if (ret)
		goto err_free_buffer;

	cdev_init(&edu_chat_cdev, &edu_chat_fops);
	ret = cdev_add(&edu_chat_cdev, edu_chat_devt, 1);
	if (ret)
		goto err_unregister_chrdev;

	edu_chat_class = class_create(EDU_CHAT_NAME);
	if (IS_ERR(edu_chat_class)) {
		ret = PTR_ERR(edu_chat_class);
		goto err_del_cdev;
	}
	edu_chat_class->devnode = edu_chat_devnode;

	/* Create /dev/edu_chat automatically for the chat room demo. */
	edu_chat_device = device_create(edu_chat_class, NULL, edu_chat_devt,
					NULL, EDU_CHAT_NAME);
	if (IS_ERR(edu_chat_device)) {
		ret = PTR_ERR(edu_chat_device);
		goto err_destroy_class;
	}

	ret = sysfs_create_groups(&edu_chat_device->kobj, edu_chat_groups);
	if (ret)
		goto err_destroy_device;

	pr_info("edu_chat: registered /dev/%s major=%u minor=%u capacity=%u\n",
		EDU_CHAT_NAME, MAJOR(edu_chat_devt), MINOR(edu_chat_devt),
		edu_chat_capacity);
	return 0;

err_destroy_device:
	device_destroy(edu_chat_class, edu_chat_devt);
err_destroy_class:
	class_destroy(edu_chat_class);
err_del_cdev:
	cdev_del(&edu_chat_cdev);
err_unregister_chrdev:
	unregister_chrdev_region(edu_chat_devt, 1);
err_free_buffer:
	kfree(edu_chat_data);
	edu_chat_data = NULL;
	return ret;
}

static void __exit edu_chat_exit(void)
{
	sysfs_remove_groups(&edu_chat_device->kobj, edu_chat_groups);
	device_destroy(edu_chat_class, edu_chat_devt);
	class_destroy(edu_chat_class);
	cdev_del(&edu_chat_cdev);
	unregister_chrdev_region(edu_chat_devt, 1);
	kfree(edu_chat_data);
	pr_info("edu_chat: unregistered\n");
}

module_init(edu_chat_init);
module_exit(edu_chat_exit);

MODULE_AUTHOR("Rodrigo");
MODULE_DESCRIPTION("Educational local chat room character driver");
MODULE_LICENSE("GPL");
