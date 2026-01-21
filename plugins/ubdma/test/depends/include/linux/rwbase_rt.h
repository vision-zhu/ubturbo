/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_RWBASE_RT_H
#define _LINUX_RWBASE_RT_H
#define __RWBASE_INITIALIZER(name) \
    { \
        .readers = ATOMIC_INIT(READER_BIAS), .rtmutex = __RT_MUTEX_BASE_INITIALIZER((name).rtmutex), \
    }
#endif