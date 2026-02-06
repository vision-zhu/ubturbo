/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_SPINLOCK_H
#define __LINUX_SPINLOCK_H

#include <linux/spinlock_types.h>
#include <linux/rwlock.h>

#define spin_lock_irqsave(lock, flags)
#define spin_unlock_irqrestore(lock, flags)

#define spin_lock_init(x)
#define spin_lock_irq(x)
#define spin_unlock_irq(x)
#define spin_lock(x)
#define spin_unlock(x)

#endif /* __LINUX_SPINLOCK_H */
