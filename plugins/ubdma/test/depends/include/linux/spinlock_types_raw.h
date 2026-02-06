/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __LINUX_SPINLOCK_TYPES_RAW_H
#define __LINUX_SPINLOCK_TYPES_RAW_H

#define SPINLOCK_MAGIC 0Xdead4ead
#define SPINLOCK_OWNER_INIT ((void *)-1L)

#define RAW_SPIN_DEP_MAP_INIT(lockname)

#ifdef CONFIG_DEBUG_SPINLOCK
#define SPIN_DEBUG_INIT(lockname) .magic = SPINLOCK_MAGIC, .owner_cpu = -1, .owner = SPINLOCK_OWNER_INIT,
#else
#define SPIN_DEBUG_INIT(lockname)
#endif

#define __RAW_SPIN_LOCK_INITIALIZER(lockname) \
    { \
        .raw_lock = __ARCH_SPIN_LOCK_UNLOCKED, SPIN_DEBUG_INIT(lockname) RAW_SPIN_DEP_MAP_INIT(lockname) \
    }
#define __RAW_SPIN_LOCK_UNLOCKED(lockname) (raw_spinlock_t) __RAW_SPIN_LOCK_INITIALIZER(lockname)
#endif /* __LINUX_SPINLOCK_TYPES_H */
