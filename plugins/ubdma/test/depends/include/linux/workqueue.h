/* SPDX-License-Identifier: GPL-2.0 */
/*
 * workqueue.h --- work queue handling for Linux.
 */

#ifndef _LINUX_WORKQUEUE_H
#define _LINUX_WORKQUEUE_H

#include <linux/gfp.h>
#include <linux/types.h>
struct timer_list {
    struct hlist_node entry;
    unsigned long expires;
    void (*function)(struct timer_list *);
    u32 flags;
};

enum {
    __WQ_ORDERED_EXPLICIT = 1 << 19,
    __WQ_LEGACY = 1 << 18,
    __WQ_ORDERED = 1 << 17,
    __WQ_DRAINING = 1 << 16,

    WQ_POWER_EFFICIENT = 1 << 7,
    WQ_SYSFS = 1 << 6,
    WQ_CPU_INTENSIVE = 1 << 5,
    WQ_HIGHPRI = 1 << 4,
    WQ_MEM_RECLAIM = 1 << 3,
    WQ_FREEZABLE = 1 << 2,
    WQ_UNBOUND = 1 << 1,

    WQ_MAX_UNBOUND_PER_CPU = 4,
    WQ_MAX_ACTIVE = 512,
    WQ_DFL_ACTIVE = WQ_MAX_ACTIVE / 2,
};

struct work_struct;
typedef void (*work_func_t)(struct work_struct *work);
typedef atomic_t atomic_long_t;
struct work_struct {
    atomic_long_t data; // atomic_long_t -> atomic64_t
    struct list_head entry;
    work_func_t func;
};

struct delayed_work {
    struct work_struct work;    // main work
    struct timer_list timer;
    struct workqueue_struct *wq;
    int cpu;
    unsigned long data;
};

#define INIT_DELAYED_WORK(_work, _func)
#define INIT_WORK(_work, _func)

struct workqueue_struct {
    struct list_head pwqs;
    struct list_head list;
    int work_color;
    int flush_color;
    atomic_t nr_pwqs_to_flush;
};

#define create_singlethread_workqueue(name) (struct workqueue_struct *)malloc(sizeof(struct workqueue_struct))
#endif