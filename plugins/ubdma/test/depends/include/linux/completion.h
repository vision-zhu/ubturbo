/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_COMPLETION_H
#define __LINUX_COMPLETION_H

#define TASK_UNINTERRUPTIBLE 0X00000002

#include <linux/swait.h>

struct completion {
    unsigned int done;
    struct swait_queue_head wait;
};

#define init_completion(x) __init_completion(x)

static inline void __init_completion(struct completion *x)
{
    x->done = 0;
}

unsigned long wait_for_completion_timeout(struct completion *x, unsigned long timeout)
{
    return 1;
}

#endif
