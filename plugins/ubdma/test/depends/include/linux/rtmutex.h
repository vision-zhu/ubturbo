/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_RMAP_H
#define _LINUX_RMAP_H
#define __RT_MUTEX_BASE_INITIALIZER(rtbasename) \
    { \
        .wait_lock = __RAW_SPIN_LOCK_UNLOCKED((rtbasename).wait_lock), .waiters = RB_ROOT_CACHED, .owner =NULL \
    }
#endif