// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Author: Abby Cin
 * Mail: abbytsing@gmail.com
 * Create Time: 2023-03-26 10:01:25
 */

#ifndef MC_DEV_H_
#define MC_DEV_H_

#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/device.h>

#define MAX_NR_CDEV 10
#define debug(fmt, ...)                                                        \
	printk(KERN_NOTICE "%s:%d " fmt "", __func__, __LINE__, ##__VA_ARGS__)

struct my_control {
	struct cdev dev;
	struct class *class;
	struct device *device;
};

void init_dev_map(void);

int add_dev(struct class *parent, int major);

int del_dev(int major, int minor);

#endif
