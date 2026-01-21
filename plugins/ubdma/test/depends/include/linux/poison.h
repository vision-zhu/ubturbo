/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_POISON_DEPENDS_H
#define _LINUX_POISON_DEPENDS_H

# define POISON_POINTER_DELTA 0

#define LIST_POISON1  ((void *) 0x100 + POISON_POINTER_DELTA)
#define LIST_POISON2  ((void *) 0x122 + POISON_POINTER_DELTA)

#endif
