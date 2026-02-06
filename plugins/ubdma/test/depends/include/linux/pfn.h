/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_PFN_DEPENDS_H_
#define _LINUX_PFN_DEPENDS_H_

#include <asm/memory.h>
#define PHYS_PFN(x)	((unsigned long)((x) >> PAGE_SHIFT))
#define PFN_PHYS(x)	((phys_addr_t)(x) << PAGE_SHIFT)
#endif
