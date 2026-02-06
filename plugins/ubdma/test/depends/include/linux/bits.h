/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_BITS_H
#define __LINUX_BITS_H

#include <asm/bitsperlong.h>
#include <linux/limits.h>

#define BITS_PER_BYTE 8

#define BIT_WORD(nr)		((nr) / BITS_PER_LONG)

#endif // __LINUX_BITS_H