// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Author: Abby Cin
 * Mail: abbytsing@gmail.com
 * Create Time: 2023-03-25 23:32:18
 */

#include <unistd.h>
#include <stdio.h>
#include <iostream>
#include <fcntl.h>
#include <sys/types.h>

#define debug(fmt, ...)                                                        \
	fprintf(stderr, "%s:%d " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__)

int main(int argc, char *argv[])
{
	if (argc != 3) {
		debug("%s <r|w> dev", argv[0]);
		return 1;
	}

	char op = argv[1][0];
	if (op != 'r' && op != 'w') {
		debug("invalid op, expect r or w");
		return 1;
	}

	int fd = -1;
	int rc = 0;
	ssize_t nr = 0;
	char buf[128] = { 0 };
	std::string line;

	if (op == 'r') {
		fd = open(argv[2], O_RDONLY);
		if (fd < 0) {
			debug("open rc %d errno %d", fd, errno);
			return 1;
		}
		nr = read(fd, buf, sizeof(buf));
		if (nr <= 0) {
			debug("read rc %ld errno %d", nr, errno);
			rc = 1;
			goto err;
		}
		debug("read: %s", buf);
	} else {
		fd = open(argv[2], O_WRONLY);
		if (fd < 0) {
			debug("open rc %d errno %d", fd, errno);
			return 1;
		}
		rc = lseek(fd, 0, SEEK_SET);
		if (rc) {
			perror("lseek");
			rc = 1;
			goto err;
		}
		debug("enter texts:");
		std::getline(std::cin, line);
		nr = write(fd, line.data(), line.size());
		if (nr <= 0) {
			debug("write rc %ld errno %d", nr, errno);
			goto err;
		}
		write(fd, buf, 1); // terminate by zero
		debug("write %ld bytes to %s", nr, argv[2]);
	}
err:
	close(fd);
	return rc;
}
