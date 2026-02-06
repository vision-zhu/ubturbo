/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_MINMAX_H
#define _LINUX_MINMAX_H

#include <linux/const.h>
#include <linux/compiler.h>

#define __cmp(x, y, op)	((x) op (y) ? (x) : (y))

#define min_t(type, x, y)	__cmp((type)(x), (type)(y), <)
#define min_stub(x, y) ((x) < (y) ? (x) : (y))
#define max_t(type, x, y)	__cmp((type)(x), (type)(y), >)

#endif // _LINUX_MINMAX_H