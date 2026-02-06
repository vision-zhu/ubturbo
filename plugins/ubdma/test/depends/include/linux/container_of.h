/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_CONTAINER_OF_H
#define _LINUX_CONTAINER_OF_H

#include <linux/err.h>
#include <linux/stddef.h>

#define container_of(p, type, member) ({				\
	void *__mptr = (void *)(p);					\
	((type *)(__mptr - offsetof(type, member))); })

#endif
