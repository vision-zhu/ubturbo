/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_TYPES_H
#define _LINUX_TYPES_H

#define __EXPORTED_HEADERS__
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <uapi/linux/types.h>
#include <asm/posix_types.h>


#ifndef __ASSEMBLY__

#define DECLARE_BITMAP(name, bits) \
    unsigned long name[BITS_TO_LONGS(bits)]

typedef u32 __kernel_dev_t;

typedef unsigned short        umode_t;
typedef unsigned int  gfp_t;
typedef unsigned long        uintptr_t;

#ifndef _CADDR_T
#define _CADDR_T
typedef __kernel_caddr_t    caddr_t;
#endif

#ifndef _CLOCK_T
#define _CLOCK_T
typedef __kernel_clock_t    clock_t;
#endif

#ifndef _PTRDIFF_T
#define _PTRDIFF_T
typedef __kernel_ptrdiff_t    ptrdiff_t;
#endif

#ifndef _SSIZE_T
#define _SSIZE_T
// ssize_t is used for sizes that can be negative, such as return values of system calls
typedef __kernel_ssize_t    ssize_t;
#endif

#ifndef _SIZE_T
#define _SIZE_T
// size_t is used for sizes of objects, typically the size of memory blocks
typedef __kernel_size_t        size_t;
#endif

typedef u32            uint32_t;
typedef u16            uint16_t;
typedef u8            uint8_t;

#define aligned_u64        __aligned_u64

typedef unsigned int __bitwise gfp_t;

typedef u64 dma_addr_t;

typedef u64 phys_addr_t;

struct list_head {
    struct list_head *next, *prev;
};

#ifdef CONFIG_64BIT
typedef struct {
    s64 counter;
} atomic64_t;
#endif

typedef struct {
    int counter;
} atomic_t;

#define ATOMIC_INIT(i) { (i) }

struct hlist_head {
    struct hlist_node *first;
};

struct hlist_node {
    struct hlist_node *next, **pprev;
};
typedef phys_addr_t resource_size_t;
#endif
#endif
