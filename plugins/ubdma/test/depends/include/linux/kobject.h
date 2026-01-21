// SPDX-License-Identifier: GPL-2.0

#ifndef _KOBJECT_DEPENDS_H_
#define _KOBJECT_DEPENDS_H_

#include <linux/types.h>
#include <linux/list.h>

struct kobject {
    unsigned int uevent_suppress;
    unsigned int state_remove_uevent_sent;
    struct list_head entry;
    unsigned int state_initialized;
    /* Depends kobj_attribute */
    unsigned int state_in_sysfs;
    struct kobject *parent;
    const struct kobj_type *ktype;
    const char *name;
    unsigned int state_add_uevent_sent;
};

struct kobj_type {
    void (*release)(struct kobject *kobj);
    const struct sysfs_ops *sysfs_ops;
};

#endif /* _KOBJECT_H_ */