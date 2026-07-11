/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_LINUX_EDU_PBUF_H
#define _UAPI_LINUX_EDU_PBUF_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define EDU_PBUF_DEFAULT_CAPACITY	4096U
#define EDU_PBUF_MAX_CAPACITY		65536U

/*
 * Local educational ioctl magic. For an upstream ABI, reserve a block in
 * Documentation/userspace-api/ioctl/ioctl-number.rst.
 */
#define EDU_PBUF_IOC_MAGIC		0xE7

/* Current state of the educational alert queue. */
struct edu_pbuf_info {
	__u32 capacity;
	__u32 limit;
	__u32 length;
	__u32 flags;
};

/* Append alerts, consume them after read, or wait until a new alert arrives. */
#define EDU_PBUF_F_APPEND		(1U << 0)
#define EDU_PBUF_F_CLEAR_ON_READ	(1U << 1)
#define EDU_PBUF_F_BLOCKING_READ	(1U << 2)
#define EDU_PBUF_F_MASK		(EDU_PBUF_F_APPEND | \
					 EDU_PBUF_F_CLEAR_ON_READ | \
					 EDU_PBUF_F_BLOCKING_READ)

/* Usage counters exposed to user space for the demo monitor. */
struct edu_pbuf_stats {
	__u64 opens;
	__u64 closes;
	__u64 read_calls;
	__u64 write_calls;
	__u64 bytes_read;
	__u64 bytes_written;
	__u64 ioctl_calls;
	__u64 waits;
	__u32 clears;
	__u32 failed_writes;
	__u32 active_handles;
	__u32 reserved;
};

#define EDU_PBUF_IOC_GET_INFO	_IOR(EDU_PBUF_IOC_MAGIC, 0x01, \
				     struct edu_pbuf_info)
#define EDU_PBUF_IOC_CLEAR	_IO(EDU_PBUF_IOC_MAGIC, 0x02)
#define EDU_PBUF_IOC_SET_LIMIT	_IOW(EDU_PBUF_IOC_MAGIC, 0x03, __u32)
#define EDU_PBUF_IOC_GET_STATS	_IOR(EDU_PBUF_IOC_MAGIC, 0x04, \
				     struct edu_pbuf_stats)
#define EDU_PBUF_IOC_SET_FLAGS	_IOW(EDU_PBUF_IOC_MAGIC, 0x05, __u32)

#endif /* _UAPI_LINUX_EDU_PBUF_H */
