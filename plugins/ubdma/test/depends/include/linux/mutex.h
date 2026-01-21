/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef TEST_MUTEX_H
#define TEST_MUTEX_H

struct mutex {
    int		owner;
};

#define mutex_init(mutex)	0

void mutex_lock(struct mutex *lock);

void mutex_unlock(struct mutex *lock);

void mutex_destroy(struct mutex *lock);

#endif // TEST_MUTEX_H
