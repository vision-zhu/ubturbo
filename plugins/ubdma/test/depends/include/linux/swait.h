/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SWAIT_H
#define _LINUX_SWAIT_H

#include <linux/list.h>
#include <linux/spinlock.h>

struct swait_queue_head {
    raw_spinlock_t        lock;
    struct list_head    task_list;
};

#define init_swait_queue_head(q) do { \
    static struct lock_class_key __key; \
    __init_swait_queue_head((q), #q, &__key); \
} while (0)

#endif /* _LINUX_SWAIT_H */
