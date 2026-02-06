/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2006 PathScale, Inc.  All Rights Reserved.
 */

#ifndef _LINUX_IO_H
#define _LINUX_IO_H
#include <asm-generic/hwrpb.h>
#ifdef USE_48_BIT_KSEG
#define IDENT_ADDR 0Xffff800000000000UL
#else
#define IDENT_ADDR 0Xfffffc0000000000UL
#endif

#define _IOC_SIZEBITS	13
#define _IOC_TYPEBITS	8
#define _IOC_NRBITS	8

#define _IOC_WRITE	4U
#define _IOC_NRSHIFT 0
#define _IOC_TYPESHIFT (_IOC_NRBITS + _IOC_NRSHIFT)
#define _IOC_SIZESHIFT (_IOC_TYPEBITS + _IOC_TYPESHIFT)
#define _IOC_DIRSHIFT (_IOC_SIZEBITS + _IOC_SIZESHIFT)

#define _IOC(direction, type, number, size)			\
    ((unsigned int)				\
    (((direction) << _IOC_DIRSHIFT) |	((type) << _IOC_TYPESHIFT) |	\
    ((number) << _IOC_NRSHIFT) | ((size) << _IOC_SIZESHIFT)))

#define _IOW(type, number, size) _IOC(_IOC_WRITE, (type), (number), sizeof(size))

#ifdef USE_48_BIT_KSEG
static inline unsigned long virt_to_phys(volatile void *address)
{
    return (unsigned long)address - IDENT_ADDR;
}

static inline void *phys_to_virt(unsigned long address)
{
    return (void *)(address + IDENT_ADDR);
}
#else
static inline unsigned long virt_to_phys(volatile void *address)
{
    unsigned long phys = (unsigned long)address;
    phys <<= (64 - 41); // 64 41
    phys = (long)phys >> (64 - 41); // 64 41
    phys &= (1UL << 46) - 1; // 46
    return phys;
}

static inline void *phys_to_virt(unsigned long address)
{
    return (void *)(IDENT_ADDR + (address & ((1UL << 41) - 1))); // 41
}
#endif

#endif