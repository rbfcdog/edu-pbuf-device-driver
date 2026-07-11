/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _DRIVERS_LKCAMP_EDU_CHAT_INTERNAL_H
#define _DRIVERS_LKCAMP_EDU_CHAT_INTERNAL_H

#include <linux/fs.h>
#include <linux/mutex.h>
#include <linux/sysfs.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <uapi/linux/edu_chat.h>

#define EDU_CHAT_NAME	"edu_chat"

extern unsigned int edu_chat_capacity;
extern struct mutex edu_chat_lock;
extern wait_queue_head_t edu_chat_wait;
extern char *edu_chat_data;
extern size_t edu_chat_len;
extern size_t edu_chat_limit;
extern unsigned int edu_chat_flags;
extern struct edu_chat_stats edu_chat_stats;

extern const struct file_operations edu_chat_fops;
extern const struct attribute_group *edu_chat_groups[];

void edu_chat_clear_locked(void);

#endif /* _DRIVERS_LKCAMP_EDU_CHAT_INTERNAL_H */
