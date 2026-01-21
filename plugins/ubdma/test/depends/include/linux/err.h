/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_ERR_H
#define _LINUX_ERR_H

#include <asm/errno.h>
#include <linux/types.h>
#include <linux/compiler_attributes.h>
#include <linux/compiler_types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_ERRNO    4095    /* MAX_ERRNO */

#ifndef unlikely
# define unlikely(x)        __builtin_expect(!!(x), 0)
#endif

#ifndef likely
# define likely(x)        __builtin_expect(!!(x), 1)
#endif

#define IS_ERR_VALUE(x) unlikely((unsigned long)(void *)(x) >= (unsigned long)-MAX_ERRNO)

static inline void * __must_check ERR_PTR(long error)
{
    return (void *) error;
}

static inline bool __must_check IS_ERR_OR_NULL(__force const void *ptr)
{
    return unlikely(!ptr) || IS_ERR_VALUE((unsigned long)ptr);
}
long PTR_ERR(const void *ptr);
bool IS_ERR(const void *ptr);

#ifdef __cplusplus
}
#endif

#endif

