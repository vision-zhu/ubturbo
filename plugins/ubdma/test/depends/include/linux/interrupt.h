/* SPDX-License-Identifier: GPL-2.0 */
/* interrupt.h */
#ifndef _LINUX_INTERRUPT_H
#define _LINUX_INTERRUPT_H

#include <linux/container_of.h>
#include <linux/wait.h>


struct tasklet_struct {
    struct tasklet_struct *next;
    unsigned long state;
    atomic_t count;
    bool use_callback;
    union {
        void (*func)(unsigned long data);
        void (*callback)(struct tasklet_struct *t);
    };
    unsigned long data;
};

#define from_tasklet(var, callback_tasklet, tasklet_fieldname) \
    container_of(callback_tasklet, typeof(*(var)), tasklet_fieldname)
static inline void tasklet_setup(struct tasklet_struct *t, void (*callback)(struct tasklet_struct *))
{
}
static inline void tasklet_kill(struct tasklet_struct *t)
{
}

static inline void tasklet_schedule(struct tasklet_struct *t){};
#endif
