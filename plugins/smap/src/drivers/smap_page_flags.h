/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 * Description: SMAP page flags for accumulated access count
 */

#ifndef _SMAP_PAGE_FLAGS_H
#define _SMAP_PAGE_FLAGS_H

#include <linux/mm.h>
#include <linux/mmzone.h>

/*
 * 使用LAST_CPUPID字段存储累计访问频次
 * SMAP插件禁用了numa_balance，因此LAST_CPUPID是闲置的
 * 页面释放时LAST_CPUPID会自动重置为全1
 * prior字段保持u8，返回值截断到低8位，最大255
 */

#define SMAP_ACC_CNT_MAX     255  /* u8截断后的最大值 */

/*
 * 获取累计访问频次
 * 初始值是全F（LAST_CPUPID_MASK），返回0
 * 其他值加一后返回，最大不超过SMAP_ACC_CNT_MAX
 */
static inline u8 get_smap_acc_cnt(struct page *page)
{
	unsigned long flags = READ_ONCE(page->flags);
	u16 cnt = (flags >> LAST_CPUPID_PGSHIFT) & LAST_CPUPID_MASK;

	/* 初始值是全F，返回0 */
	if (cnt == LAST_CPUPID_MASK)
		return 0;

	/* 加一后返回，最大不超过SMAP_ACC_CNT_MAX */
	return min_t(u16, cnt + 1, SMAP_ACC_CNT_MAX);
}

/* 累计访问频次加一 */
static inline void inc_smap_acc_cnt(struct page *page)
{
	unsigned long flags = READ_ONCE(page->flags);
	u16 cnt = (flags >> LAST_CPUPID_PGSHIFT) & LAST_CPUPID_MASK;

	/* 已达最大值，不再增加 */
	if (cnt == SMAP_ACC_CNT_MAX - 1)
		return;

	/* 初始值（全F）或正常值都直接加一 */
	cnt++;
	flags &= ~(LAST_CPUPID_MASK << LAST_CPUPID_PGSHIFT);
	flags |= ((unsigned long)cnt << LAST_CPUPID_PGSHIFT);
	WRITE_ONCE(page->flags, flags);
}

#endif /* _SMAP_PAGE_FLAGS_H */