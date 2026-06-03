// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 * Description: SMAP3.0 Tiering Memory Solution: access 内存地址模块
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/mmzone.h>
#include <linux/memory.h>
#include <linux/spinlock.h>

#include "check.h"
#include "access_tracking.h"
#include "access_iomem.h"

#define REMOTE_RAM_NAME "System RAM (Remote)"
#undef pr_fmt
#define pr_fmt(fmt) "access-iomem: " fmt

LIST_HEAD(remote_ram_list);
EXPORT_SYMBOL(remote_ram_list);

DEFINE_RWLOCK(rem_ram_list_lock);
EXPORT_SYMBOL(rem_ram_list_lock);
bool remote_ram_changed; /* Protected by rem_ram_list_lock */

struct index_table_entry *remote_ram_index_table;
EXPORT_SYMBOL(remote_ram_index_table);
bool index_table_valid;
EXPORT_SYMBOL(index_table_valid);
u64 node_total_4k_pages[SMAP_MAX_NUMNODES];
EXPORT_SYMBOL(node_total_4k_pages);
u64 node_total_2m_pages[SMAP_MAX_NUMNODES];
EXPORT_SYMBOL(node_total_2m_pages);

static void free_remote_ram(struct list_head *head)
{
	struct ram_segment *seg, *tmp;
	list_for_each_entry_safe(seg, tmp, head, node) {
		list_del(&seg->node);
		kfree(seg);
	}
}

static void move_remote_ram(struct list_head *dst, struct list_head *src)
{
	struct ram_segment *seg, *tmp;
	list_for_each_entry_safe(seg, tmp, src, node) {
		list_move_tail(&seg->node, dst);
	}
}

static void merge_ram_segments(struct list_head *head)
{
	struct ram_segment *cur, *next, *tmp;

	if (list_empty(head))
		return;

	cur = list_first_entry(head, struct ram_segment, node);
	while (cur) {
		next = list_next_entry(cur, node);
		if (list_entry_is_head(next, head, node))
			break;
		if (cur->numa_node == next->numa_node &&
		    cur->end + 1 == next->start) {
			cur->end = next->end;
			list_del(&next->node);
			kfree(next);
			tmp = cur;
		} else {
			tmp = next;
		}
		cur = tmp;
	}
}

static int insert_remote_ram(u64 pa_start, u64 pa_end, struct list_head *head)
{
	struct ram_segment *seg, *tmp;
	u64 start, end;
	unsigned long pfn;
	int nid;

	start = pa_start;
	while (start < pa_end) {
		pfn = PHYS_PFN(start);
		if (!pfn_valid(pfn) || !pfn_to_online_page(pfn)) {
			start += MIN_MEMORY_BLOCK_SIZE;
			continue;
		}
		nid = page_to_nid(pfn_to_online_page(pfn));
		if (nid == NUMA_NO_NODE)
			return -EINVAL;
		end = start + MIN_MEMORY_BLOCK_SIZE - 1;
		if (nid >= SMAP_MAX_NUMNODES) {
			start = end + 1;
			continue;
		}
		seg = kmalloc(sizeof(*seg), GFP_KERNEL);
		if (!seg)
			return -ENOMEM;

		end = MIN(pa_end, end);
		seg->start = start;
		seg->end = end;
		seg->numa_node = nid;

		if (list_empty(head)) {
			list_add_tail(&seg->node, head);
		} else {
			tmp = list_last_entry(head, struct ram_segment, node);
			if (seg->start == tmp->end + 1 &&
			    nid == tmp->numa_node) {
				tmp->end = seg->end;
				kfree(seg);
			} else {
				list_add_tail(&seg->node, head);
			}
		}

		start = end + 1;
	}
	return 0;
}

static int fixed_remote_ram(struct list_head *head)
{
	struct ram_segment *seg = kmalloc(sizeof(*seg), GFP_KERNEL);
	if (!seg)
		return -ENOMEM;
	seg->start = REMOTE_PA_START;
	seg->end = REMOTE_PA_END;
	seg->numa_node = REMOTE_NUMA_ID;
	list_add_tail(&seg->node, head);
	return 0;
}

static int update_resource(struct resource *r, void *arg)
{
	int ret;
	struct list_head *head = (struct list_head *)arg;

	if (!r || !arg)
		return -EINVAL;

	if (r->flags & IORESOURCE_SYSRAM_DRIVER_MANAGED) {
		ret = insert_remote_ram(r->start, r->end, head);
		if (ret) {
			free_remote_ram(head);
			return ret;
		}
	}
	return 0;
}

static int walk_system_ram_remote_range(struct list_head *head)
{
	if (!head)
		return -EINVAL;
	return walk_iomem_res_desc(IORES_DESC_NONE, IORESOURCE_SYSTEM_RAM, 0,
				   -1, head, update_resource);
}

void release_remote_ram(void)
{
	write_lock(&rem_ram_list_lock);
	free_remote_ram(&remote_ram_list);
	if (remote_ram_index_table) {
		vfree(remote_ram_index_table);
		remote_ram_index_table = NULL;
	}
	index_table_valid = false;
	memset(node_total_4k_pages, 0, sizeof(node_total_4k_pages));
	memset(node_total_2m_pages, 0, sizeof(node_total_2m_pages));
	write_unlock(&rem_ram_list_lock);
}

static int build_index_table(void)
{
	struct ram_segment *seg;
	u64 block_start, block_end, block_idx;
	u64 cur_4k_idx[SMAP_MAX_NUMNODES] = { 0 };
	u64 cur_2m_idx[SMAP_MAX_NUMNODES] = { 0 };
	u64 seg_start_offset;

	if (list_empty(&remote_ram_list))
		return 0;

	remote_ram_index_table = vzalloc(INDEX_TABLE_SIZE *
					  sizeof(struct index_table_entry));
	if (!remote_ram_index_table)
		return -ENOMEM;

	list_for_each_entry(seg, &remote_ram_list, node) {
		if (seg->numa_node >= SMAP_MAX_NUMNODES)
			continue;

		/* 计算段在索引表中的范围 */
		seg_start_offset = seg->start - REMOTE_PA_START;
		block_start = seg_start_offset >> 21;
		block_end = (seg_start_offset + (seg->end - seg->start)) >> 21;

		for (block_idx = block_start; block_idx <= block_end; block_idx++) {
			if (block_idx >= INDEX_TABLE_SIZE)
				break;
			remote_ram_index_table[block_idx].numa_node = seg->numa_node;
			remote_ram_index_table[block_idx].node_4k_base_idx =
				cur_4k_idx[seg->numa_node];
			remote_ram_index_table[block_idx].node_2m_base_idx =
				cur_2m_idx[seg->numa_node];
			cur_4k_idx[seg->numa_node] += 512;
			cur_2m_idx[seg->numa_node] += 1;
		}
	}

	/* 存储每个节点的总页面数 */
	for (int node = 0; node < SMAP_MAX_NUMNODES; node++) {
		node_total_4k_pages[node] = cur_4k_idx[node];
		node_total_2m_pages[node] = cur_2m_idx[node];
	}

	index_table_valid = true;
	pr_info("built remote ram index table with %llu entries\n",
		(unsigned long long)INDEX_TABLE_SIZE);
	return 0;
}

int refresh_remote_ram(void)
{
	LIST_HEAD(tmp_head);
	int ret;
	if (smap_scene == UB_QEMU_SCENE)
		ret = fixed_remote_ram(&tmp_head);
	else
		ret = walk_system_ram_remote_range(&tmp_head);
	if (ret)
		return ret;
	merge_ram_segments(&tmp_head);
	write_lock(&rem_ram_list_lock);
	free_remote_ram(&remote_ram_list);
	if (remote_ram_index_table) {
		vfree(remote_ram_index_table);
		remote_ram_index_table = NULL;
		index_table_valid = false;
	}
	move_remote_ram(&remote_ram_list, &tmp_head);
	free_remote_ram(&tmp_head);
	ret = build_index_table();
	remote_ram_changed = true;
	pr_info("remote ram was changed\n");
	write_unlock(&rem_ram_list_lock);
	return ret;
}

int get_numa_by_pfn(unsigned long pfn)
{
	struct ram_segment *seg;
	u64 pa = PFN_PHYS(pfn);
	int nid = NUMA_NO_NODE;

	read_lock(&rem_ram_list_lock);
	list_for_each_entry(seg, &remote_ram_list, node) {
		if (pa >= seg->start && pa <= seg->end) {
			nid = seg->numa_node;
			break;
		}
	}
	read_unlock(&rem_ram_list_lock);
	return nid;
}

static u64 get_node_page_cnt_list(int nid, int page_size);

u64 get_node_page_cnt_iomem(int nid, int page_size)
{
	u64 len = 0;

	if (unlikely(nid < nr_local_numa || nid >= SMAP_MAX_NUMNODES))
		return 0;

	read_lock(&rem_ram_list_lock);
	if (index_table_valid) {
		if (page_size == PAGE_SIZE)
			len = node_total_4k_pages[nid];
		else if (page_size == PAGE_SIZE_2M)
			len = node_total_2m_pages[nid];
		read_unlock(&rem_ram_list_lock);
		return len;
	}
	read_unlock(&rem_ram_list_lock);

	/* 兜底：链表遍历 */
	return get_node_page_cnt_list(nid, page_size);
}

static u64 get_node_page_cnt_list(int nid, int page_size)
{
	u64 len = 0;
	struct ram_segment *seg, *tmp;

	read_lock(&rem_ram_list_lock);
	list_for_each_entry_safe(seg, tmp, &remote_ram_list, node) {
		if (seg->numa_node != nid)
			continue;

		if (page_size == g_pagesize_huge)
			len += calc_huge_count(seg->end - seg->start + 1);
		else if (page_size == PAGE_SIZE)
			len += calc_normal_count(seg->end - seg->start + 1);
	}
	read_unlock(&rem_ram_list_lock);

	return len;
}
EXPORT_SYMBOL(get_node_page_cnt_iomem);

static inline int calc_paddr_idx_o1(u64 pa, int nid, u64 *index, int page_size)
{
	u64 block_idx, offset_in_block;
	struct index_table_entry *entry;

	if (pa < REMOTE_PA_START || pa > REMOTE_PA_END)
		return -ERANGE;

	block_idx = (pa - REMOTE_PA_START) >> 21;
	if (block_idx >= INDEX_TABLE_SIZE)
		return -ERANGE;

	entry = &remote_ram_index_table[block_idx];

	if (entry->numa_node != nid)
		return -ERANGE;

	if (page_size == PAGE_SIZE) {
		offset_in_block = (pa - (REMOTE_PA_START + block_idx * PAGE_SIZE_2M)) >> 12;
		*index = entry->node_4k_base_idx + offset_in_block;
	} else if (page_size == PAGE_SIZE_2M) {
		*index = entry->node_2m_base_idx;
	} else {
		return -EINVAL;
	}

	return 0;
}

static int calc_paddr_idx_list(u64 pa, int nid, u64 *index, int page_size);
static int calc_paddr_acidx_iomem_list(u64 pa, int *nid, u64 *index, int page_size);

static int calc_paddr_acidx_iomem_o1(u64 pa, int *nid, u64 *index, int page_size)
{
	u64 block_idx;
	struct index_table_entry *entry;

	if (pa < REMOTE_PA_START || pa > REMOTE_PA_END)
		return -ERANGE;

	block_idx = (pa - REMOTE_PA_START) >> 21;
	if (block_idx >= INDEX_TABLE_SIZE)
		return -ERANGE;

	entry = &remote_ram_index_table[block_idx];
	*nid = entry->numa_node;

	return calc_paddr_idx_o1(pa, *nid, index, page_size);
}

int calc_paddr_acidx_iomem(u64 pa, int *nid, u64 *index, int page_size)
{
	int ret;

	read_lock(&rem_ram_list_lock);
	if (index_table_valid && remote_ram_index_table) {
		ret = calc_paddr_acidx_iomem_o1(pa, nid, index, page_size);
		read_unlock(&rem_ram_list_lock);
		return ret;
	}
	read_unlock(&rem_ram_list_lock);

	return calc_paddr_acidx_iomem_list(pa, nid, index, page_size);
}

static int calc_paddr_acidx_iomem_list(u64 pa, int *nid, u64 *index, int page_size)
{
	struct ram_segment *seg;
	int shift = __builtin_ctz(page_size);
	int numa = NUMA_NO_NODE;
	u64 idx[SMAP_MAX_NUMNODES] = { 0 };

	read_lock(&rem_ram_list_lock);
	list_for_each_entry(seg, &remote_ram_list, node) {
		if (seg->numa_node >= SMAP_MAX_NUMNODES) {
			pr_err("numa %d is invalid\n", seg->numa_node);
			read_unlock(&rem_ram_list_lock);
			return -ERANGE;
		}
		if (pa < seg->start)
			break;
		if (pa <= seg->end) {
			numa = seg->numa_node;
			idx[numa] += ((pa - seg->start) >> shift);
			break;
		}
		idx[seg->numa_node] += ((seg->end - seg->start + 1) >> shift);
	}

	if (unlikely(list_entry_is_head(seg, &remote_ram_list, node))) {
		read_unlock(&rem_ram_list_lock);
		return -ERANGE;
	}
	read_unlock(&rem_ram_list_lock);
	if (unlikely(numa == NUMA_NO_NODE))
		return -ERANGE;
	*nid = numa;
	*index = idx[numa];
	return 0;
}

int calc_paddr_acidx_iomem_known_nid(u64 pa, int nid, u64 *index, int page_size)
{
	int ret;

	read_lock(&rem_ram_list_lock);
	if (index_table_valid && remote_ram_index_table) {
		ret = calc_paddr_idx_o1(pa, nid, index, page_size);
		read_unlock(&rem_ram_list_lock);
		return ret;
	}
	read_unlock(&rem_ram_list_lock);

	return calc_paddr_idx_list(pa, nid, index, page_size);
}

static int calc_paddr_idx_list(u64 pa, int nid, u64 *index, int page_size)
{
	struct ram_segment *seg;
	int shift = __builtin_ctz(page_size);
	u64 idx = 0;

	read_lock(&rem_ram_list_lock);
	list_for_each_entry(seg, &remote_ram_list, node) {
		if (seg->numa_node != nid)
			continue;
		if (pa < seg->start)
			break;
		if (pa <= seg->end) {
			idx += ((pa - seg->start) >> shift);
			*index = idx;
			read_unlock(&rem_ram_list_lock);
			return 0;
		}
		idx += ((seg->end - seg->start + 1) >> shift);
	}
	read_unlock(&rem_ram_list_lock);
	return -ERANGE;
}

int calc_acidx_paddr_iomem(int nid, u64 acidx, u64 *paddr, int page_size)
{
	struct ram_segment *seg;
	u64 range;
	int shift = __builtin_ctz(page_size);
	u64 offset = acidx << shift;

	read_lock(&rem_ram_list_lock);
	list_for_each_entry(seg, &remote_ram_list, node) {
		if (seg->numa_node != nid)
			continue;
		range = seg->end - seg->start + 1;
		if (offset >= range) {
			offset -= range;
			continue;
		}
		*paddr = seg->start + offset;
		read_unlock(&rem_ram_list_lock);
		return 0;
	}
	read_unlock(&rem_ram_list_lock);
	return -ERANGE;
}

int smap_is_remote_addr_valid(int nid, u64 pa_start, u64 pa_end)
{
	struct ram_segment *seg = NULL;

	if (pa_start >= pa_end) {
		pr_err("start physical address is greater than end physical address\n");
		return -EINVAL;
	}

	read_lock(&rem_ram_list_lock);
	list_for_each_entry(seg, &remote_ram_list, node) {
		if (seg->numa_node != nid)
			continue;
		if (pa_start >= seg->start && pa_end <= seg->end + 1) {
			read_unlock(&rem_ram_list_lock);
			return 0;
		}
	}
	read_unlock(&rem_ram_list_lock);
	return -EINVAL;
}
EXPORT_SYMBOL(smap_is_remote_addr_valid);
