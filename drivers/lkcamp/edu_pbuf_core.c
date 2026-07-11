// SPDX-License-Identifier: GPL-2.0-only
/*
 * edu_pbuf - educational pseudo character driver.
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

#include "edu_pbuf_internal.h"

unsigned int edu_pbuf_capacity = EDU_PBUF_DEFAULT_CAPACITY;
module_param_named(capacity, edu_pbuf_capacity, uint, 0444);
MODULE_PARM_DESC(capacity, "Allocated pseudo-buffer capacity in bytes");

static dev_t edu_pbuf_devt;
static struct cdev edu_pbuf_cdev;
static struct class *edu_pbuf_class;
static struct device *edu_pbuf_device;

/* Serializes buffer content, metadata, flags and statistics. */
DEFINE_MUTEX(edu_pbuf_lock);
wait_queue_head_t edu_pbuf_wait;
char *edu_pbuf_data;
size_t edu_pbuf_len;
size_t edu_pbuf_limit;
unsigned int edu_pbuf_flags;
struct edu_pbuf_stats edu_pbuf_stats;

void edu_pbuf_clear_locked(void)
{
	memset(edu_pbuf_data, 0, edu_pbuf_capacity);
	edu_pbuf_len = 0;
}

static int __init edu_pbuf_init(void)
{
	int ret;

	if (!edu_pbuf_capacity || edu_pbuf_capacity > EDU_PBUF_MAX_CAPACITY)
		return -EINVAL;

	edu_pbuf_data = kzalloc(edu_pbuf_capacity, GFP_KERNEL);
	if (!edu_pbuf_data)
		return -ENOMEM;

	edu_pbuf_limit = edu_pbuf_capacity;
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
		edu_pbuf_capacity);
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
