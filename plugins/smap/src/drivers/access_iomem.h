/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2024. All rights reserved.
 * Description: SMAP3.0 Tiering Memory Solution: access 内存地址模块
 */

#ifndef _SRC_ACCESS_IOMEM_H
#define _SRC_ACCESS_IOMEM_H

#include <linux/list.h>
#include <linux/spinlock.h>

#include "check.h"

#define REMOTE_NUMA_ID 4
#define REMOTE_PA_START 0x40000000000ULL
#define REMOTE_PA_END 0x47fffffffffULL
#define REMOTE_RAM_RANGE_SIZE (REMOTE_PA_END - REMOTE_PA_START + 1ULL)
#define INDEX_TABLE_SIZE (REMOTE_RAM_RANGE_SIZE / PAGE_SIZE_2M)

extern struct list_head remote_ram_list;
extern int nr_local_numa;
extern rwlock_t rem_ram_list_lock;
extern bool remote_ram_changed;
extern unsigned int smap_scene;

struct index_table_entry {
	u8 numa_node;
	u64 node_4k_base_idx;
	u64 node_2m_base_idx;
};

extern struct index_table_entry *remote_ram_index_table;
extern bool index_table_valid;
extern u64 node_total_4k_pages[SMAP_MAX_NUMNODES];
extern u64 node_total_2m_pages[SMAP_MAX_NUMNODES];
struct ram_segment {
	struct list_head node;
	int numa_node;
	u64 start;
	u64 end;
};

void release_remote_ram(void);
int refresh_remote_ram(void);
int get_numa_by_pfn(unsigned long pfn);
u64 get_node_page_cnt_iomem(int nid, int page_size);
int calc_paddr_acidx_iomem(u64 pa, int *nid, u64 *index, int page_size);
int calc_paddr_acidx_iomem_known_nid(u64 pa, int nid, u64 *index,
				     int page_size);
int calc_acidx_paddr_iomem(int nid, u64 acidx, u64 *paddr, int page_size);

#endif /* _SRC_ACCESS_IOMEM_H */
