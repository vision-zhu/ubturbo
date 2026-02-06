/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __LINUX_SPINLOCK_TYPES_H
#define __LINUX_SPINLOCK_TYPES_H


#include <asm/spinlock_types.h>

#define MAX_LOCKDEP_SUBCLASSES 8UL
struct lockdep_subclass_key {
    char __one_byte;
} __attribute__((__packed__));

struct lock_class_key {
    union {
        struct hlist_node hash_entry;
        struct lockdep_subclass_key subkeys[MAX_LOCKDEP_SUBCLASSES];
    };
};

typedef struct {
    volatile unsigned int lock;
} arch_rwlock_t;

typedef struct raw_spinlock {
    // The underlying architecture-specific spinlock
    arch_spinlock_t raw_lock;
} raw_spinlock_t;

typedef struct spinlock {
    union {
        struct raw_spinlock rlock;
    };
} spinlock_t;
#include <linux/rwlock_types.h>

#endif /* __LINUX_SPINLOCK_TYPES_H */
