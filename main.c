// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Author: Abby Cin
 * Mail: abbytsing@gmail.com
 * Create Time: 2023-03-26 10:00:45
 */

#include "mcdev.h"
#include <linux/ioctl.h>

static struct my_control g_ctrl;
static dev_t g_ctrl_devno;
static const char *g_ctrl_name = "mcdev_control";
#define MAGIC ('a' | 'b' | 'b' | 'y')
#define CDEV_ADD _IOW(MAGIC, 1, int32_t *)
#define CDEV_DEL _IOW(MAGIC, 2, int32_t *)

static ssize_t
my_ctrl_read(struct file *fp, char __user *buf, size_t size, loff_t *off)
{
	return 0;
}

static ssize_t
my_ctrl_write(struct file *fp, const char __user *buf, size_t size, loff_t *off)
{
	return size;
}

static int my_ctrl_open(struct inode *inode, struct file *fp)
{
	int rc = mutex_trylock(&g_ctrl.mtx);

	if (rc == 0) {
		debug("other process is operating on %s", g_ctrl_name);
		return -EBUSY;
	}
	if (!fp->private_data)
		fp->private_data = &g_ctrl;
	mutex_unlock(&g_ctrl.mtx);
	return 0;
}

static int my_ctrl_release(struct inode *inode, struct file *fp)
{
	return 0;
}

static long my_ctrl_ioctl(struct file *fp, unsigned int op, unsigned long data)
{
	int32_t cmd = -1;
	int rc = 0;
	struct my_control *ctrl = fp->private_data;

	mutex_lock(&ctrl->mtx);
	switch (op) {
	case CDEV_ADD:
		if (copy_from_user(&cmd, (int32_t *)data, sizeof(cmd))) {
			debug("copy_from_user");
			rc = -EFAULT;
			goto err;
		}
		// NOTE: from add `cmd` was ignored
		rc = add_dev(ctrl->class, MAJOR(g_ctrl_devno));
		if (rc) {
			debug("add_dev, rc %d", rc);
			goto err;
		}
		break;
	case CDEV_DEL:
		if (copy_from_user(&cmd, (int32_t *)data, sizeof(cmd))) {
			debug("del: copy_from_user");
			rc = -EFAULT;
			goto err;
		}
		if (cmd < 1) {
			debug("bad cmd %d expect range [1, 10]", cmd);
			rc = -EINVAL;
			goto err;
		}
		rc = del_dev(MAJOR(g_ctrl_devno), cmd);
		if (rc) {
			debug("del_dev minior %d, rc %d", cmd, rc);
			goto err;
		}
		break;
	default:
		rc = -EINVAL;
		break;
	}
err:
	mutex_unlock(&ctrl->mtx);
	return rc;
}

static struct file_operations g_ctrl_ops = {
	.owner = THIS_MODULE,
	.read = my_ctrl_read,
	.write = my_ctrl_write,
	.open = my_ctrl_open,
	.release = my_ctrl_release,
	.unlocked_ioctl = my_ctrl_ioctl,
};

static __init int mcdev_init(void)
{
	int rc = 0;

	init_dev_map();
	rc = alloc_chrdev_region(&g_ctrl_devno, 0, 1, g_ctrl_name);
	if (rc) {
		debug("alloc_chrdev_region rc %d", rc);
		return -EFAULT;
	}

	cdev_init(&g_ctrl.dev, &g_ctrl_ops);

	mutex_init(&g_ctrl.mtx);
	g_ctrl.dev.owner = THIS_MODULE;
	rc = cdev_add(&g_ctrl.dev, g_ctrl_devno, 1);
	if (rc) {
		debug("cdev_add rc %d", rc);
		goto err1;
	}

	if (IS_ERR(g_ctrl.class = class_create(THIS_MODULE, g_ctrl_name))) {
		rc = (int)PTR_ERR(g_ctrl.class);
		debug("class create rc %d", rc);
		goto err;
	}
	if (IS_ERR(g_ctrl.device = device_create(g_ctrl.class,
						 NULL,
						 g_ctrl_devno,
						 NULL,
						 g_ctrl_name))) {
		rc = (int)PTR_ERR(g_ctrl.device);
		debug("device_create rc %d", rc);
		goto err;
	}
	debug("mcdev memory size %lu", dev_mem_size());
	return 0;
err:
	if (!IS_ERR(g_ctrl.class))
		class_destroy(g_ctrl.class);
	cdev_del(&g_ctrl.dev);
err1:
	mutex_destroy(&g_ctrl.mtx);
	unregister_chrdev_region(g_ctrl_devno, 1);
	return rc;
}

static __exit void mcdev_exit(void)
{
	int major = MAJOR(g_ctrl_devno);

	mutex_destroy(&g_ctrl.mtx);
	for (int i = 0; i <= MAX_NR_CDEV; ++i)
		del_dev(major, i);
	device_destroy(g_ctrl.class, g_ctrl_devno);
	class_destroy(g_ctrl.class);
	cdev_del(&g_ctrl.dev);
	unregister_chrdev_region(g_ctrl_devno, 1);
}

module_init(mcdev_init);
module_exit(mcdev_exit);
MODULE_DESCRIPTION("a in memory char device driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("abbycin (abbytsing@gmail.com)");
MODULE_VERSION("1.0");
