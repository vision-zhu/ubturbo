// SPDX-License-Identifier: GPL-2.0

#ifndef _DEVICE_CLASS_DEPENDS_H_
#define _DEVICE_CLASS_DEPENDS_H_

#include <linux/kobject.h>
#include <linux/version.h>

struct class_stub {
};

#define class_create(name) ((struct class_stub *)(int64_t)((name) ? 1 : 0))

#ifdef __cplusplus
extern "C" {
#endif
void class_destroy(struct class_stub *cls);
#ifdef __cplusplus
}
#endif

#endif
