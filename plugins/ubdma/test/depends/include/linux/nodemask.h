/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_NODEMASK_H
#define __LINUX_NODEMASK_H

#include <linux/numa.h>
#include <linux/bitops.h>
#include <linux/bitmap.h>

typedef struct {
    unsigned long bits[BITS_TO_LONGS(MAX_NUMNODES)];
} nodemask_t;

#ifdef __cplusplus
extern "C" {
#endif


#define nodes_clear(dst) __nodes_clear(&(dst), MAX_NUMNODES)
static inline void __nodes_clear(nodemask_t *dstp, unsigned int nbits)
{
    bitmap_zero(dstp->bits, nbits);
}

#define node_set(n, dst) __node_set((n), &(dst))
static __always_inline void __node_set(int n, volatile nodemask_t *dstp)
{
    set_bit(n, dstp->bits);
}

#ifdef __cplusplus
}
#endif


#endif /* __LINUX_NODEMASK_H */
