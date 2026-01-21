/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __ASM_DEPENDS_MEMORY_H
#define __ASM_DEPENDS_MEMORY_H

#define VA_BITS (CONFIG_ARM64_VA_BITS)
#define _PAGE_OFFSET(va) (-(UL(1) << (va)))
#define PAGE_OFFSET (_PAGE_OFFSET(VA_BITS))
#define page_to_virt(x) x
#define virt_to_page(x) ((struct page *)(int64_t)(x))
#define page_address(x) x

#endif
