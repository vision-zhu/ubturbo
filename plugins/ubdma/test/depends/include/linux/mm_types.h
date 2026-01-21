/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_MM_TYPES_DEPENDS_H
#define _LINUX_MM_TYPES_DEPENDS_H

#include <asm/page.h>
#include <linux/list.h>

struct page {
    unsigned long flags;
    union {
        struct {
            union {
                struct list_head lru;
            };
        };
        struct {
            unsigned char compound_order;
        };
        struct {
            spinlock_t ptl;
        };
    };
};


struct folio {
    union {
        struct {
            unsigned long flags;
            union {
                struct list_head lru;
            };
        };
        struct page page;
    };
    union {
        struct {
            unsigned long _flags_1;
        };
    };
};


#endif
