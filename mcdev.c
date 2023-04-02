// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Author: Abby Cin
 * Mail: abbytsing@gmail.com
 * Create Time: 2023-02-21 21:22:46
 */

#include "mcdev.h"
#include <linux/container_of.h>
#include <linux/export.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/vmalloc.h>

static ulong cdev_size = (1UL << 20);
module_param(cdev_size, ulong, S_IRUSR | S_IWUSR);
MODULE_PARM_DESC(cdev_size, "memory size of mcdev, default is 1M");
static int g_dev_map[MAX_NR_CDEV] = { -1 };

void init_dev_map(void)
{
	for (int i = 0; i < MAX_NR_CDEV; ++i)
		g_dev_map[i] = -1;
}

// NOTE: minor -= 1 since control dev occupied 0
static int map_dev(int minor)
{
	minor -= 1;
	if (minor < 0 || minor == MAX_NR_CDEV)
		return -EINVAL;
	if (g_dev_map[minor] == 1)
		return -EEXIST;
	g_dev_map[minor] = 1;
	return 0;
}

// NOTE: minor -= 1 since control dev occupied 0
static int unmap_dev(int minor)
{
	minor -= 1;
	if (minor < 0 || minor == MAX_NR_CDEV)
		return -EINVAL;
	if (g_dev_map[minor] == -1)
		return -ENXIO;
	g_dev_map[minor] = -1;
	return 0;
}

// NOTE: i + 1 since control dev occupied 0
static int find_map_entry(void)
{
	for (int i = 0; i < MAX_NR_CDEV; ++i) {
		if (g_dev_map[i] == -1)
			return i + 1;
	}
	return -1;
}

struct my_cdev {
	char *data;
	int minor;
	struct class *class;
	struct device *devfs;
	struct cdev dev;
	struct mutex mtx;
	wait_queue_head_t r_wait;
	wait_queue_head_t w_wait;
	size_t r_cursor;
	size_t w_cursor;
	size_t size;
	size_t cap;
	struct my_cdev *next;
};

static size_t readable_bytes(struct my_cdev *cdev)
{
	return cdev->size;
}

static size_t writable_bytes(struct my_cdev *cdev)
{
	return cdev->cap - cdev->size;
}

static ssize_t push(struct my_cdev *cdev, const char __user *buf, size_t nbytes)
{
	nbytes = min(writable_bytes(cdev), nbytes);
	if (copy_from_user(cdev->data + cdev->w_cursor, buf, nbytes)) {
		debug("copy_from_user");
		return -EFAULT;
	}
	cdev->w_cursor = (cdev->w_cursor + nbytes) % cdev->cap;
	cdev->size += nbytes;
	return nbytes;
}

static ssize_t poll(struct my_cdev *cdev, char __user *buf, size_t nbytes)
{

	nbytes = min(readable_bytes(cdev), nbytes);
	if (copy_to_user(buf, cdev->data + cdev->r_cursor, nbytes)) {
		debug("copy_to_user");
		return -EFAULT;
	}
	cdev->r_cursor = (cdev->r_cursor + nbytes) % cdev->cap;
	cdev->size -= nbytes;
	return nbytes;
}

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
	return -ENOSYS;
}

static ssize_t
my_read(struct file *fp, char __user *data, size_t size, loff_t *off)
{
	ssize_t rc = 0;
	struct my_cdev *cdev = fp->private_data;
	DECLARE_WAITQUEUE(wait, current);

	mutex_lock(&cdev->mtx);
	add_wait_queue(&cdev->r_wait, &wait);

	while (readable_bytes(cdev) == 0) {
		if (fp->f_flags & O_NONBLOCK) {
			rc = -EAGAIN;
			goto err;
		}

		set_current_state(TASK_INTERRUPTIBLE);
		mutex_unlock(&cdev->mtx);

		schedule();

		if (signal_pending(current)) {
			rc = -ERESTARTSYS;
			goto err1;
		}
		mutex_lock(&cdev->mtx);
	}

	rc = poll(cdev, data, size);
	if (rc < 0)
		goto err;

	wake_up_interruptible(&cdev->w_wait);
err:
	mutex_unlock(&cdev->mtx);
err1:
	remove_wait_queue(&cdev->r_wait, &wait);
	set_current_state(TASK_RUNNING);
	return rc;
}

static ssize_t
my_write(struct file *fp, const char __user *data, size_t size, loff_t *off)
{
	ssize_t rc = 0;
	struct my_cdev *cdev = fp->private_data;
	DECLARE_WAITQUEUE(wait, current);

	mutex_lock(&cdev->mtx);
	add_wait_queue(&cdev->w_wait, &wait);

	while (writable_bytes(cdev) == 0) {
		if (fp->f_flags & O_NONBLOCK) {
			rc = -EAGAIN;
			goto err;
		}

		set_current_state(TASK_INTERRUPTIBLE);
		mutex_unlock(&cdev->mtx);

		schedule();

		if (signal_pending(current)) {
			rc = -ERESTARTSYS;
			goto err1;
		}
		mutex_lock(&cdev->mtx);
	}

	rc = push(cdev, data, size);
	if (rc < 0)
		goto err;

	wake_up_interruptible(&cdev->r_wait);

err:
	mutex_unlock(&cdev->mtx);
err1:
	remove_wait_queue(&cdev->w_wait, &wait);
	set_current_state(TASK_RUNNING);
	return rc;
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

static struct my_cdev *g_cdev_head;

static int setup_my_cdev(struct my_cdev *head, dev_t devno)
{
	int err = 0;

	cdev_init(&head->dev, &g_fops);
	head->dev.owner = THIS_MODULE;
	head->minor = MINOR(devno);
	err = cdev_add(&head->dev, devno, 1);
	if (err) {
		debug("cdev_add cdev %d rc %d", head->minor, err);
		return err;
	}
	head->cap = cdev_size;
	mutex_init(&head->mtx);
	init_waitqueue_head(&head->r_wait);
	init_waitqueue_head(&head->w_wait);
	return 0;
}

int add_dev(struct class *parent, int major)
{
	int rc;
	int minor = -1;
	dev_t devno;
	struct my_cdev *cdev = NULL;
	const char *prefix = "mcdev";
	char name[10] = { 0 };

	minor = find_map_entry();
	if (minor == -1) {
		debug("too many device registered");
		return -ENOMEM;
	}
	snprintf(name, sizeof(name), "%s%d", prefix, minor);
	devno = MKDEV(major, minor);
	rc = register_chrdev_region(devno, 1, name);
	if (rc) {
		debug("register_chrdev_region %s rc %d", name, rc);
		return rc;
	}

	cdev = kzalloc(sizeof(*g_cdev_head), GFP_KERNEL);
	if (!cdev) {
		debug("can't alloc memory for %s", name);
		rc = -ENOMEM;
		goto err;
	}
	cdev->data = vmalloc(cdev_size);
	if (!cdev->data) {
		debug("can't alloc memory from storage of %s", name);
		rc = -ENOMEM;
		goto err;
	}

	cdev->class = parent;
	cdev->devfs = device_create(parent, NULL, devno, NULL, name);
	if (IS_ERR(cdev->devfs)) {
		rc = PTR_ERR(cdev->devfs);
		debug("device_create for cdev %s fail, rc %d", name, rc);
		goto err;
	}

	rc = setup_my_cdev(cdev, devno);
	if (rc)
		goto err;

	if (!g_cdev_head) {
		g_cdev_head = cdev;
	} else {
		cdev->next = g_cdev_head;
		g_cdev_head = cdev;
	}
	map_dev(minor);

	debug("add cdev %p", cdev);
	return 0;
err:
	if (cdev) {
		if (cdev->devfs && !IS_ERR(cdev->devfs))
			device_destroy(cdev->class, devno);
		if (cdev->data)
			vfree(cdev->data);
		kfree(cdev);
	}
	unregister_chrdev_region(devno, 1);
	return rc;
}
EXPORT_SYMBOL(add_dev);

static void remove_dev(struct my_cdev *dev, dev_t devno)
{
	mutex_destroy(&dev->mtx);
	device_destroy(dev->class, devno);
	cdev_del(&dev->dev);
	vfree(dev->data);
	kfree(dev);
	unregister_chrdev_region(devno, 1);
}

static struct my_cdev *
remove_node(struct my_cdev *head, int minor, struct my_cdev **r)
{
	struct my_cdev d = { .next = head };
	struct my_cdev *p = &d;

	while (p) {
		if (p->next && p->next->minor == minor) {
			*r = p->next;
			p->next = p->next->next;
			break;
		}
		p = p->next;
	}
	return d.next;
}

int del_dev(int major, int minor)
{
	struct my_cdev *dev = NULL;
	int rc = unmap_dev(minor);
	dev_t devno = MKDEV(major, minor);

	if (rc != 0)
		return rc;
	if (!g_cdev_head)
		return -ENOENT;

	g_cdev_head = remove_node(g_cdev_head, minor, &dev);
	if (dev) {
		debug("del cdev %p", dev);
		remove_dev(dev, devno);
	}
	return 0;
}
EXPORT_SYMBOL(del_dev);

ulong dev_mem_size(void)
{
	return cdev_size;
}

MODULE_LICENSE("GPL");
