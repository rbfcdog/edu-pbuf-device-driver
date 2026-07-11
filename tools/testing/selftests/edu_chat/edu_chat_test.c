// SPDX-License-Identifier: GPL-2.0-only
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <linux/edu_chat.h>

static void die(const char *msg)
{
	perror(msg);
	exit(EXIT_FAILURE);
}

static void get_info(int fd, struct edu_chat_info *info)
{
	if (ioctl(fd, EDU_CHAT_IOC_GET_INFO, info) < 0)
		die("ioctl EDU_CHAT_IOC_GET_INFO");
}

static void print_info(int fd, const char *label)
{
	struct edu_chat_info info;

	get_info(fd, &info);

	printf("%s: capacity=%u limit=%u length=%u flags=0x%x\n",
	       label, info.capacity, info.limit, info.length, info.flags);
}

static void print_stats(int fd, const char *label)
{
	struct edu_chat_stats stats;

	if (ioctl(fd, EDU_CHAT_IOC_GET_STATS, &stats) < 0)
		die("ioctl EDU_CHAT_IOC_GET_STATS");

	printf("%s: opens=%llu closes=%llu active=%u reads=%llu writes=%llu ",
	       label, stats.opens, stats.closes, stats.active_handles,
	       stats.read_calls, stats.write_calls);
	printf("bytes_read=%llu bytes_written=%llu ioctls=%llu waits=%llu ",
	       stats.bytes_read, stats.bytes_written, stats.ioctl_calls,
	       stats.waits);
	printf("clears=%u failed_writes=%u\n",
	       stats.clears, stats.failed_writes);
}

static void write_all(int fd, const char *msg)
{
	size_t len = strlen(msg);
	ssize_t written;

	written = write(fd, msg, len);
	if (written < 0)
		die("write");

	if ((size_t)written != len) {
		fprintf(stderr, "short write: %zd of %zu\n", written, len);
		exit(EXIT_FAILURE);
	}
}

static void read_once(int fd)
{
	char buf[512];
	ssize_t nread;

	memset(buf, 0, sizeof(buf));

	nread = read(fd, buf, sizeof(buf) - 1);
	if (nread < 0)
		die("read");

	printf("read: %zd bytes: %s", nread, buf);
	if (nread && buf[nread - 1] != '\n')
		printf("\n");
}

static void poll_once(int fd, const char *label)
{
	struct pollfd pfd = {
		.fd = fd,
		.events = POLLIN | POLLOUT,
	};
	int ret;

	ret = poll(&pfd, 1, 0);
	if (ret < 0)
		die("poll");

	printf("%s: poll ret=%d revents=0x%x readable=%s writable=%s\n",
	       label, ret, pfd.revents,
	       pfd.revents & POLLIN ? "yes" : "no",
	       pfd.revents & POLLOUT ? "yes" : "no");
}

static void test_nonblocking_empty_read(const char *path)
{
	__u32 flags = EDU_CHAT_F_BLOCKING_READ;
	char byte;
	int fd;

	fd = open(path, O_RDWR | O_NONBLOCK);
	if (fd < 0)
		die("open nonblocking");

	if (ioctl(fd, EDU_CHAT_IOC_SET_FLAGS, &flags) < 0)
		die("ioctl EDU_CHAT_IOC_SET_FLAGS blocking");

	if (read(fd, &byte, sizeof(byte)) >= 0 || errno != EAGAIN) {
		fprintf(stderr, "expected EAGAIN from empty nonblocking read\n");
		exit(EXIT_FAILURE);
	}
	printf("nonblocking empty read: got EAGAIN as expected\n");

	flags = 0;
	if (ioctl(fd, EDU_CHAT_IOC_SET_FLAGS, &flags) < 0)
		die("ioctl EDU_CHAT_IOC_SET_FLAGS reset");

	close(fd);
}

int main(int argc, char **argv)
{
	const char *path = argc > 1 ? argv[1] : "/dev/edu_chat";
	const char *msg = argc > 2 ? argv[2] :
		"rodrigo: oi pessoal, tudo bem?\n";
	const char *suffix =
		"ana: oi rodrigo! tranquilo e voce?\n";
	struct edu_chat_info initial_info;
	__u32 restore_limit;
	__u32 new_limit = 32;
	__u32 flags = EDU_CHAT_F_APPEND | EDU_CHAT_F_CLEAR_ON_READ;
	int fd;

	fd = open(path, O_RDWR);
	if (fd < 0)
		die("open");

	get_info(fd, &initial_info);
	restore_limit = initial_info.capacity;

	if (ioctl(fd, EDU_CHAT_IOC_SET_LIMIT, &restore_limit) < 0)
		die("ioctl EDU_CHAT_IOC_SET_LIMIT initial restore");

	if (ioctl(fd, EDU_CHAT_IOC_CLEAR) < 0)
		die("ioctl EDU_CHAT_IOC_CLEAR initial");
	print_info(fd, "initial");
	print_stats(fd, "initial stats");

	if (ioctl(fd, EDU_CHAT_IOC_SET_FLAGS, &flags) < 0)
		die("ioctl EDU_CHAT_IOC_SET_FLAGS append");
	print_info(fd, "after set append+clear_on_read");

	write_all(fd, msg);
	write_all(fd, suffix);
	print_info(fd, "after two writes");
	poll_once(fd, "after write");
	close(fd);

	fd = open(path, O_RDWR);
	if (fd < 0)
		die("reopen");

	read_once(fd);
	print_info(fd, "after read with clear_on_read");

	if (ioctl(fd, EDU_CHAT_IOC_SET_LIMIT, &new_limit) < 0)
		die("ioctl EDU_CHAT_IOC_SET_LIMIT");
	print_info(fd, "after set limit");

	test_nonblocking_empty_read(path);
	print_stats(fd, "final stats");

	if (ioctl(fd, EDU_CHAT_IOC_CLEAR) < 0)
		die("ioctl EDU_CHAT_IOC_CLEAR");
	if (ioctl(fd, EDU_CHAT_IOC_SET_LIMIT, &restore_limit) < 0)
		die("ioctl EDU_CHAT_IOC_SET_LIMIT restore");
	print_info(fd, "after clear");

	close(fd);
	return EXIT_SUCCESS;
}
