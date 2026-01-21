/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _LINUX_SLAB_H
#define	_LINUX_SLAB_H

#include <stdlib.h>
#include <linux/types.h>

#ifdef __cplusplus
extern "C" {
#endif

void *kmalloc(size_t size, gfp_t flags);
void *kzalloc(size_t size, gfp_t flags);
void kfree(void *p);

#ifdef __cplusplus
}
#endif

#endif /* _LINUX_SLAB_H */
