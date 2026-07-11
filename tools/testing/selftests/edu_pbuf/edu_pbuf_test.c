// SPDX-License-Identifier: GPL-2.0-only
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <linux/edu_pbuf.h>

static void die(const char *msg)
{
	perror(msg);
	exit(EXIT_FAILURE);
}

static void print_info(int fd, const char *label)
{
	struct edu_pbuf_info info;

	if (ioctl(fd, EDU_PBUF_IOC_GET_INFO, &info) < 0)
		die("ioctl EDU_PBUF_IOC_GET_INFO");

	printf("%s: capacity=%u limit=%u length=%u flags=%u\n",
	       label, info.capacity, info.limit, info.length, info.flags);
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

int main(int argc, char **argv)
{
	const char *path = argc > 1 ? argv[1] : "/dev/edu_pbuf";
	const char *msg = argc > 2 ? argv[2] :
		"teste do edu_pbuf via user space\n";
	__u32 new_limit = 32;
	int fd;

	fd = open(path, O_RDWR);
	if (fd < 0)
		die("open");

	print_info(fd, "initial");
	write_all(fd, msg);
	print_info(fd, "after write");
	close(fd);

	fd = open(path, O_RDWR);
	if (fd < 0)
		die("reopen");

	read_once(fd);

	if (ioctl(fd, EDU_PBUF_IOC_SET_LIMIT, &new_limit) < 0)
		die("ioctl EDU_PBUF_IOC_SET_LIMIT");
	print_info(fd, "after set limit");

	if (ioctl(fd, EDU_PBUF_IOC_CLEAR) < 0)
		die("ioctl EDU_PBUF_IOC_CLEAR");
	print_info(fd, "after clear");

	close(fd);
	return EXIT_SUCCESS;
}
