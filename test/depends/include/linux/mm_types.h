/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_MM_TYPES_DEPENDS_H
#define _LINUX_MM_TYPES_DEPENDS_H

#include <linux/list.h>
#include <linux/maple_tree.h>
#include <linux/page-flags-layout.h>
#include <linux/rwsem.h>
#include <asm/page.h>
#include <linux/version.h>
#include <linux/spinlock.h>

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
	int _refcount;
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

struct vma_iterator {
	struct ma_state mas;
};


struct mm_struct {
	struct {
		struct maple_tree mm_mt;
    };
    spinlock_t page_table_lock;
	struct rw_semaphore mmap_lock;
};


struct vm_area_struct {
	struct vm_area_struct *vm_prev;
	struct vm_area_struct *vm_next;
	struct mm_struct *vm_mm;
	unsigned long vm_end;
	unsigned long vm_flags;
	unsigned long vm_start;
	struct mempolicy *vm_policy;
};

struct anon_vma {
	struct anon_vma *root;
};

struct rmap_walk_control {
	void *arg;
	bool (*rmap_one)(struct page *page, struct vm_area_struct *vma,
					unsigned long addr, void *arg);
	struct anon_vma *(*anon_lock)(struct page *page);
};


typedef struct {
    unsigned long val;
} swp_entry_t;

#endif
