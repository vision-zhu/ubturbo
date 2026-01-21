/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_CDEV_H
#define _LINUX_CDEV_H

#include <linux/device.h>
struct cdev {
    struct module *owner;
};

#ifdef __cplusplus
extern "C" {
#endif
struct file_operations;
void cdev_init(struct cdev *, const struct file_operations *);

int cdev_add(struct cdev *, dev_t, unsigned);
void cdev_del(struct cdev *p);
#ifdef __cplusplus
}
#endif

#endif //_LINUX_CDEV_H
