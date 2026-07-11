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

struct edu_pbuf_info {
	__u32 capacity;
	__u32 limit;
	__u32 length;
	__u32 flags;
};

#define EDU_PBUF_IOC_GET_INFO	_IOR(EDU_PBUF_IOC_MAGIC, 0x01, \
				     struct edu_pbuf_info)
#define EDU_PBUF_IOC_CLEAR	_IO(EDU_PBUF_IOC_MAGIC, 0x02)
#define EDU_PBUF_IOC_SET_LIMIT	_IOW(EDU_PBUF_IOC_MAGIC, 0x03, __u32)

#endif /* _UAPI_LINUX_EDU_PBUF_H */
