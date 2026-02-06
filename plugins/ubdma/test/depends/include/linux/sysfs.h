/* SPDX-License-Identifier: GPL-2.0 */
/*
 * sysfs.h - definitions for the device driver filesystem
 *
 * Copyright (c) 2001,2002 Patrick Mochel
 * Copyright (c) 2004 Silicon Graphics, Inc.
 * Copyright (c) 2007 SUSE Linux Products GmbH
 * Copyright (c) 2007 Tejun Heo <teheo@suse.de>
 *
 * Please see Documentation/filesystems/sysfs.rst for more information.
 */

#ifndef _SYSFS_H_
#define _SYSFS_H_

#include <linux/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct kobject;

struct attribute {
    const char *name;
    umode_t mode;
};

#ifdef __cplusplus
}
#endif

#endif /* _SYSFS_H_ */
