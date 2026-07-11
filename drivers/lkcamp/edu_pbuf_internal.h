/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _DRIVERS_LKCAMP_EDU_PBUF_INTERNAL_H
#define _DRIVERS_LKCAMP_EDU_PBUF_INTERNAL_H

#include <linux/fs.h>
#include <linux/mutex.h>
#include <linux/sysfs.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <uapi/linux/edu_pbuf.h>

#define EDU_PBUF_NAME	"edu_pbuf"

extern unsigned int edu_pbuf_capacity;
extern struct mutex edu_pbuf_lock;
extern wait_queue_head_t edu_pbuf_wait;
extern char *edu_pbuf_data;
extern size_t edu_pbuf_len;
extern size_t edu_pbuf_limit;
extern unsigned int edu_pbuf_flags;
extern struct edu_pbuf_stats edu_pbuf_stats;

extern const struct file_operations edu_pbuf_fops;
extern const struct attribute_group *edu_pbuf_groups[];

void edu_pbuf_clear_locked(void);

#endif /* _DRIVERS_LKCAMP_EDU_PBUF_INTERNAL_H */
