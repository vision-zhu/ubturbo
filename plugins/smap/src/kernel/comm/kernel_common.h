/* SPDX-License-Identifier: GPL-2.0 */
#ifndef SMAP_KERNEL_COMMON_H
#define SMAP_KERNEL_COMMON_H

#include <linux/mm.h>

#include "common.h"

#define MAX_NODE_NUM 32
#define DEVICE_BASE_MINOR 0
#define DEVICE_MINOR_COUNT 1
#define INVALID_PADDR 0

#ifndef MIN
#define MIN(a, b) ((a) <= (b) ? (a) : (b))
#endif

struct access_pid;

/*
 * smap_page_in_large_folio - @page is part of a THP/mTHP (a large folio that
 * is not hugetlb). PageTransHuge() only tests the head page, so tail pages of
 * a THP slipped through the 4K-mode filters that intend to skip THPs.
 */
static inline bool smap_page_in_large_folio(struct page *page)
{
	return PageCompound(page) && !folio_test_hugetlb(page_folio(page));
}

/*
 * smap_try_get_page_folio - pin the folio that currently contains @page
 *
 * page_folio() is evaluated without a reference: by the time folio_try_get()
 * succeeds the page may have been freed and reused in a different (possibly
 * compound) folio, so the reference would be taken on one folio and later
 * dropped on another. Re-check after pinning; while the reference is held
 * the folio can neither be freed nor split, so page_folio(page) is stable.
 */
static inline struct folio *smap_try_get_page_folio(struct page *page)
{
	struct folio *folio = page_folio(page);

	if (!folio_try_get(folio))
		return NULL;
	if (unlikely(page_folio(page) != folio)) {
		folio_put(folio);
		return NULL;
	}
	return folio;
}

typedef enum {
	NORMAL_MIGRATE,
	REMOTE_MIGRATE,
	MAX_MIGRATE_TYPE,
} migrate_type;

typedef struct {
	u64 pme;
} pagemap_entry_t;

struct remote_migrate_info {
	pid_t pid;
	u64 page_cnt;
	int remote_nid;
	unsigned int mig_cnt;
	u64 folios_len;
	struct folio **folios;
	/*
	 * Set by walk_pid_pagemap(): true when the walk collected hugetlb
	 * folios (no reference taken), false when it collected base pages
	 * (one folio reference held per entry). Release paths must use this
	 * instead of re-reading a global page-size mode.
	 */
	bool huge;
};

struct pagemapread {
	int pos, len; /* units: PM_ENTRY_BYTES, not bytes */
	migrate_type mig_type;
	struct remote_migrate_info mig_info;
	struct access_pid *ap;
};

struct freq_info {
	u64 hpa;
	u16 freq;
};

enum node_level { L1, L2, NR_LEVEL };

extern u32 g_pagesize_huge;

static inline u64 calc_huge_count(u64 range)
{
	return (range & ~PMD_MASK) == 0 ? range >> PMD_SHIFT
					: (range >> PMD_SHIFT) + 1;
}

static inline u64 calc_normal_count(u64 range)
{
	return (range & ~PAGE_MASK) == 0 ? range >> PAGE_SHIFT
					 : (range >> PAGE_SHIFT) + 1;
}

#endif /* SMAP_KERNEL_COMMON_H */
