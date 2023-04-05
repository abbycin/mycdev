// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Author: Abby Cin
 * Mail: abbytsing@gmail.com
 * Create Time: 2023-04-05 13:16:40
 */

#include <asm-generic/errno-base.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/poll.h>
#include <unistd.h>
#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <iostream>

static bool volatile g_stop;
#define BUFSZ 128U
static char buf[BUFSZ];

void on_signal(int)
{
	g_stop = true;
}

int main(int argc, char *argv[])
{
	int opt;
	struct pollfd pfd;
	const char *dev = NULL;
	int flags = O_NONBLOCK;
	int rc, tmo = -1;
	std::string line;
	ssize_t off = 0, nbytes = 0;

	while ((opt = getopt(argc, argv, "r:w:t:")) != -1) {
		switch (opt) {
		case 'r':
			pfd.events = POLLIN;
			flags |= O_RDONLY;
			dev = optarg;
			break;
		case 'w':
			pfd.events = POLLOUT;
			flags |= O_WRONLY;
			dev = optarg;
			break;
		case 't':
			tmo = atoi(optarg);
			break;
		default:
			break;
		}
	}
	if (!dev) {
		fprintf(stderr, "%s -[r|w] dev\n", argv[0]);
		return 1;
	}

	pfd.fd = open(dev, flags);
	if (pfd.fd < 0) {
		fprintf(stderr, "open %s error %d", dev, errno);
		return 1;
	}

	signal(SIGTERM, on_signal);
	signal(SIGINT, on_signal);
	
	while (!g_stop) {
		errno = 0;
		rc = poll(&pfd, 1, tmo);
		if (rc == 0) {
			printf("poll timeout\n");
			continue;
		}
		if (rc < 0) {
			fprintf(stderr, "poll error %d\n", errno);
			goto err;
		}

		if (pfd.revents & POLLIN) {
			while (true) {
				rc = read(pfd.fd, buf, sizeof(buf));
				if (rc == 0)
					goto err;
				if (rc < 0) {
					if (errno == EAGAIN)
						break;
					fprintf(stderr, "read error %d\n", errno);
					goto err;
				}
				nbytes = write(STDOUT_FILENO, buf, rc);
			}
			continue;
		}
		if (pfd.revents & POLLOUT) {
			if (off == 0) {
				line.clear();
				printf("enter text: ");
				if (!std::getline(std::cin, line))
					goto err;
				line.push_back('\n');
			}
			while (off < line.size()) {
				nbytes = line.size() - off;
				rc = write(pfd.fd, line.data() + off, nbytes);
				if (rc == 0)
					goto err;
				if (rc < 0) {
					if (errno == EAGAIN)
						break;
					fprintf(stderr, "write error %d\n", errno);
					goto err;
				}
				off += rc;
			}
			if (errno == 0)
				off = 0;
		}
	}
err:
	close(pfd.fd);
}