// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Author: Abby Cin
 * Mail: abbytsing@gmail.com
 * Create Time: 2023-02-21 21:22:46
 */

#include "linux/container_of.h"
#include "linux/export.h"
#include "linux/fs.h"
#include "linux/cdev.h"
#include "linux/device.h"
#include "linux/vmalloc.h"

static ulong cdev_size = (1UL << 20);
#define MAX_NR_CDEV 1
static int curr_cdev_idx;
module_param(cdev_size, ulong, S_IWUSR | S_IWGRP);
MODULE_PARM_DESC(cdev_size, "per cdev size");
static char *g_basename;
static void init_basename(void)
{
	char *p = __FILE__;
	int len = strlen(p);

	while (len > 0) {
		if (*p != '/')
			--p;
		len -= 1;
	}
	g_basename = p;
}

#define debug(fmt, ...)                                                        \
	printk(KERN_NOTICE "%s:%d" fmt "", g_basename, __LINE__, ##__VA_ARGS__)

struct my_cdev {
	char *data;
	struct class *class;
	struct device *devfs;
	struct cdev dev;
	struct my_cdev *next;
};

static int my_open(struct inode *inode, struct file *fp)
{
	struct my_cdev *dev = container_of(inode->i_cdev, struct my_cdev, dev);

	fp->private_data = dev;
	return 0;
}

static int my_release(struct inode *inode, struct file *fp)
{
	return 0;
}

static loff_t my_llseek(struct file *fp, loff_t off, int op)
{
	loff_t rc = 0;

	switch (op) {
	case SEEK_SET:
		if (off < 0)
			return -EINVAL;
		if (off > cdev_size)
			return -EINVAL;
		fp->f_pos = off;
		rc = fp->f_pos;
		break;
	case SEEK_CUR:
		if ((fp->f_pos + off) > cdev_size)
			return -EINVAL;
		if ((fp->f_pos + off) < 0)
			return -EINVAL;
		fp->f_pos += off;
		rc = fp->f_pos;
		break;
	default:
		rc = -EINVAL;
		break;
	}
	return rc;
}

static ssize_t
my_read(struct file *fp, char __user *data, size_t size, loff_t *off)
{
	struct my_cdev *cdev = fp->private_data;

	if (*off >= cdev_size)
		return 0; // EOF

	// there at most cdev_size - *off left to be read
	if (size > cdev_size - *off)
		size = cdev_size - *off;

	if (copy_to_user(data, cdev->data + *off, size))
		return -EFAULT;

	debug("read %zu bytes from %lld", size, *off);
	*off += size;
	return size;
}

static ssize_t
my_write(struct file *fp, const char __user *data, size_t size, loff_t *off)
{
	struct my_cdev *cdev = fp->private_data;

	if (*off >= cdev_size)
		return 0;
	if (size > cdev_size - *off)
		size = cdev_size - *off;

	if (copy_from_user(cdev->data + *off, data, size))
		return -EFAULT;

	debug("write %zu bytes from %lld", size, *off);
	*off += size;
	return size;
}

static long my_ioctl(struct file *fp, unsigned int op, unsigned long data)
{
	return 0;
}

static struct file_operations g_fops = {
	.owner = THIS_MODULE,
	.open = my_open,
	.release = my_release,
	.llseek = my_llseek,
	.read = my_read,
	.write = my_write,
	.unlocked_ioctl = my_ioctl,
};

static int major_id;
static dev_t g_devno;
static struct my_cdev *g_cdev_head;

static void setup_my_cdev(struct my_cdev *head, int index)
{
	int err, devno = MKDEV(major_id, index);

	debug();
	cdev_init(&head->dev, &g_fops);
	debug();
	head->dev.owner = THIS_MODULE;
	err = cdev_add(&head->dev, devno, 1);
	debug();
	if (err)
		printk(KERN_NOTICE "cdev_add err %d cdev index %d", err, index);
}

static int __init init_my_cdev(void)
{
	int rc;
	struct my_cdev *cdev = NULL;
	char name[10] = { 0 };

	init_basename();
	debug();
	if (curr_cdev_idx == MAX_NR_CDEV) {
		debug("too many device registered");
		return -ENOMEM;
	}
	snprintf(name, sizeof(name), "mycdev_%d", curr_cdev_idx);
	if (major_id) {
		dev_t devno = MKDEV(major_id, curr_cdev_idx);
		rc = register_chrdev_region(devno, 1, name);
	} else {
		rc = alloc_chrdev_region(&g_devno, 0, 1, name);
		if (!major_id)
			major_id = MAJOR(g_devno);
	}
	if (rc) {
		printk(KERN_ERR "init my_cdev fail, rc %d", rc);
		return rc;
	}

	debug();
	cdev = kzalloc(sizeof(*g_cdev_head), GFP_KERNEL);
	if (!cdev) {
		printk(KERN_ERR "can't alloc memory");
		rc = -ENOMEM;
		goto err;
	}
	cdev->data = vmalloc(cdev_size);
	if (!cdev->data) {
		printk(KERN_ERR "can't alloc memory from storage");
		rc = -ENOMEM;
		goto err;
	}
	cdev->class = class_create(THIS_MODULE, "chardrv");
	if (IS_ERR(cdev->class)) {
		printk(KERN_ERR "create class for cdev %d fail, rc %d",
		       curr_cdev_idx,
		       (int)PTR_ERR(cdev->class));
		rc = PTR_ERR(cdev->class);
		goto err;
	}
	cdev->devfs = device_create(cdev->class,
				    NULL,
				    MKDEV(major_id, curr_cdev_idx),
				    NULL,
				    "my_cdev");
	if (IS_ERR(cdev->devfs)) {
		printk(KERN_ERR "create device for cdev %d fail, rc %d",
		       curr_cdev_idx,
		       (int)PTR_ERR(cdev->devfs));
		rc = PTR_ERR(cdev->devfs);
		goto err;
	}

	debug();
	setup_my_cdev(cdev, curr_cdev_idx);
	curr_cdev_idx += 1;
	if (!g_cdev_head) {
		g_cdev_head = cdev;
	} else {
		g_cdev_head->next = cdev;
		g_cdev_head = cdev;
	}

	printk(KERN_NOTICE "mycdev %d inited", curr_cdev_idx - 1);
	return 0;
err:
	if (cdev) {
		if (cdev->devfs && !IS_ERR(cdev->devfs))
			device_destroy(cdev->class,
				       MKDEV(major_id, curr_cdev_idx));
		if (cdev->class && !IS_ERR(cdev->class))
			class_destroy(cdev->class);
		if (cdev->data)
			vfree(cdev->data);
		kfree(cdev);
	}
	unregister_chrdev_region(MKDEV(major_id, curr_cdev_idx), 1);
	return rc;
}

static void __exit exit_my_cdev(void)
{
	struct my_cdev *head = g_cdev_head, *next;
	int idx = 0;

	while (head) {
		next = head->next;
		device_destroy(head->class, MKDEV(major_id, idx));
		class_destroy(head->class);
		cdev_del(&head->dev);
		vfree(head->data);
		kfree(head);
		head = next;
		idx += 1;
	}
	unregister_chrdev_region(MKDEV(major_id, 0), curr_cdev_idx);
}

module_init(init_my_cdev);
module_exit(exit_my_cdev);
MODULE_DESCRIPTION("a in memory char device driver");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("abbycin (abbytsing@gmail.com)");
MODULE_VERSION("1.0");
