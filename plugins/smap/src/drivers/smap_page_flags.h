/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 * Description: SMAP page->flags helpers for cold-period counting and init tracking
 *
 * On aarch64 the LAST_CPUPID field (16 bits) in page->flags is unused when
 * NUMA balancing is disabled.  We use two sub-fields:
 *
 *   bits [0..3]  (4 bits)  consecutive-cold-period counter  (0..14, 0xF = sentinel)
 *   bit  [4]     (1 bit)   init flag  (1 = kernel-uninitialised, 0 = SMAP-seen)
 *
 * The kernel sets LAST_CPUPID to all-1s when a page is freed, so both 0xF
 * (cold) and bit-4 (init) act as "never touched by SMAP" sentinels.
 */

#ifndef _SRC_SMAP_PAGE_FLAGS_H
#define _SRC_SMAP_PAGE_FLAGS_H

#include <linux/compiler.h>
#include <linux/bitops.h>
#include <linux/mm.h>
#include <linux/mmzone.h>
#include <asm/cmpxchg.h>

/* Cold-period counter: bits [0..3]. 0xF is sentinel (kernel all-1s);
 * effective range 0..14, saturates at 14. */
#define SMAP_COLD_PERIOD_WIDTH 4
#define SMAP_COLD_PERIOD_MASK  ((1UL << SMAP_COLD_PERIOD_WIDTH) - 1)
#define SMAP_COLD_FIELD_MASK   (SMAP_COLD_PERIOD_MASK << LAST_CPUPID_PGSHIFT)

/* Init flag: bit [4]. 1 = kernel-uninitialised, 0 = SMAP-seen. */
#define SMAP_INIT_FLAG_SHIFT   (LAST_CPUPID_PGSHIFT + SMAP_COLD_PERIOD_WIDTH)
#define SMAP_INIT_FLAG_MASK    (1UL << SMAP_INIT_FLAG_SHIFT)

/* Sentinel → 0 (0xF means kernel all-1s, not a real cold count). */
static inline u8 smap_page_cold_periods_get(struct page *page)
{
	u8 cnt = (page->flags >> LAST_CPUPID_PGSHIFT) & SMAP_COLD_PERIOD_MASK;
	return (cnt == SMAP_COLD_PERIOD_MASK) ? 0 : cnt;
}

/* Clear cold-period to 0; init flag preserved. */
static inline void smap_page_cold_periods_reset(struct page *page)
{
	unsigned long old, new;

	do {
		old = READ_ONCE(page->flags);
		new = old & ~SMAP_COLD_FIELD_MASK;
	} while (unlikely(cmpxchg(&page->flags, old, new) != old));
}

/* Saturating increment: 0xF→1 (sentinel→first cold), 0..13→+1, 14→stay.
 * Saturates at 14 so cold_period_threshold (1..15) always triggers. */
static inline u8 smap_page_cold_periods_inc(struct page *page)
{
	unsigned long old, new;
	u8 cnt;
	do {
		old = READ_ONCE(page->flags);
		cnt = (old >> LAST_CPUPID_PGSHIFT) & SMAP_COLD_PERIOD_MASK;
		if (cnt == SMAP_COLD_PERIOD_MASK)
			cnt = 1;
		else if (cnt < SMAP_COLD_PERIOD_MASK - 1)
			cnt++;
		new = old & ~SMAP_COLD_FIELD_MASK;
		new |= (unsigned long)cnt << LAST_CPUPID_PGSHIFT;
	} while (unlikely(cmpxchg(&page->flags, old, new) != old));
	return cnt;
}

/* Init flag: true if page never seen by SMAP (bit 4 = 1). */
static inline bool smap_page_init(struct page *page)
{
	return test_bit(SMAP_INIT_FLAG_SHIFT, &page->flags);
}

/* Clear init flag (bit 4 → 0); no other bits touched. */
static inline void smap_page_mark(struct page *page)
{
	clear_bit(SMAP_INIT_FLAG_SHIFT, &page->flags);
}

#endif /* _SRC_SMAP_PAGE_FLAGS_H */
