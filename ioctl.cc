// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Author: Abby Cin
 * Mail: abbytsing@gmail.com
 * Create Time: 2023-03-26 13:51:12
 */

#include <iostream>
#include <unistd.h>
#include <fcntl.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/ioctl.h>

#define MAGIC ('a' | 'b' | 'b' | 'y')
#define CMD_ADD _IOW(MAGIC, 1, int32_t *)
#define CMD_DEL _IOW(MAGIC, 2, int32_t *)

#define debug(fmt, ...)                                                        \
	fprintf(stderr, "%s:%d " fmt "\n", __func__, __LINE__, ##__VA_ARGS__)

int main(int argc, char *argv[])
{
	int opt;
	unsigned int op = -1;
	int dev = -1;
	int fd = -1;
	int rc = 0;
	const char *path = "/dev/mcdev_control";

	optind = 0;
	while ((opt = getopt(argc, argv, "p:ad:h")) != -1) {
		switch (opt) {
		case 'p':
			path = optarg;
			break;
		case 'a':
			op = CMD_ADD;
			break;
		case 'd':
			op = CMD_DEL;
			dev = atoi(optarg);
			break;
		default:
			break;
		}
	}
	if (op == (unsigned int)-1 || (op == CMD_DEL && dev < 0)) {
		debug("%s [-p /dev/mcdev_control] [-a] [-d dev] -h", argv[0]);
		return 1;
	}

	fd = open(path, O_WRONLY);
	if (fd < 0) {
		debug("open %s rc %d errno %d", path, fd, errno);
		return 1;
	}
	rc = ioctl(fd, op, &dev);
	if (rc)
		debug("ioctl rc %d errno %d", rc, errno);
	else
		debug("op %d ok", op);
	close(fd);
}
