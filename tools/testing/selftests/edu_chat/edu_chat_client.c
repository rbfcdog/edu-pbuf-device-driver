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

static void drain_existing_messages(int fd)
{
	char buf[4096];

	for (;;) {
		ssize_t n = read(fd, buf, sizeof(buf));

		if (n > 0)
			continue;
		if (!n)
			return;
		if (errno == EAGAIN)
			return;
		if (errno == EINTR)
			continue;
		die("initial read");
	}
}

static void erase_echoed_input_line(void)
{
	if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO))
		return;

	printf("\033[1A\033[2K");
	fflush(stdout);
}

int main(int argc, char **argv)
{
	const char *path = argc > 1 ? argv[1] : "/dev/edu_chat";
	const char *nick  = argc > 2 ? argv[2] : "anon";
	__u32 flags = EDU_CHAT_F_APPEND;
	char buf[4096];
	char line[4096];
	struct pollfd fds[2];
	ssize_t n;
	int fd;

	fd = open(path, O_RDWR | O_NONBLOCK);
	if (fd < 0)
		die("open /dev/edu_chat");

	if (ioctl(fd, EDU_CHAT_IOC_SET_FLAGS, &flags) < 0)
		die("ioctl SET_FLAGS append");

	drain_existing_messages(fd);

	printf("=== chat: %s entrou (/dev/edu_chat) ===\n"
	       "digite uma mensagem e aperte enter\n"
	       "Ctrl+C para sair\n\n", nick);

	fds[0].fd     = STDIN_FILENO;
	fds[0].events = POLLIN;
	fds[1].fd     = fd;
	fds[1].events = POLLIN;

	for (;;) {
		int ret = poll(fds, 2, -1);

		if (ret < 0) {
			if (errno == EINTR)
				continue;
			die("poll");
		}

		if (fds[0].revents & POLLIN) {
			if (!fgets(line, sizeof(line), stdin))
				break;

			erase_echoed_input_line();

			n = snprintf(buf, sizeof(buf), "%s: %s", nick, line);
			if (n > 0 && write(fd, buf, n) < 0)
				die("write");
		}

		if (fds[1].revents & POLLIN) {
			n = read(fd, buf, sizeof(buf) - 1);
			if (n < 0) {
				if (errno == EAGAIN)
					continue;
				die("read");
			}
			if (n == 0)
				continue;
			buf[n] = '\0';
			printf("%s", buf);
			fflush(stdout);
		}
	}

	close(fd);
	return 0;
}
