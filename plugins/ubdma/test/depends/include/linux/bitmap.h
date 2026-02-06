/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_BITMAP_H
#define __LINUX_BITMAP_H

#include <string.h>
#include <linux/bitops.h>
#include <linux/kernel.h>

#define bitmap_size(nbits) (ALIGN(nbits, BITS_PER_LONG) / BITS_PER_BYTE)

static inline void bitmap_zero(unsigned long *dst, unsigned int nbits)
{
    memset(dst, 0, bitmap_size(nbits));
}

#endif /* __LINUX_BITMAP_H */
