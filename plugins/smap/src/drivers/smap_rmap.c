// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 * Description: SMAP reverse map module for drivers
 */

#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/rmap.h>
#include <linux/sched.h>
#include <linux/interval_tree_generic.h>

#include "smap_rmap.h"

#undef pr_fmt
#define pr_fmt(fmt) "smap_rmap: " fmt

/* anon_vma_chain 区间树辅助函数 */
static inline unsigned long avc_start_pgoff(struct anon_vma_chain *avc)
{
	return avc->vma->vm_pgoff;
}

static inline unsigned long avc_last_pgoff(struct anon_vma_chain *avc)
{
	return avc->vma->vm_pgoff + vma_pages(avc->vma) - 1;
}

/* vm_area_struct 区间树辅助函数 */
static inline unsigned long vma_start_pgoff(struct vm_area_struct *v)
{
	return v->vm_pgoff;
}

static inline unsigned long vma_last_pgoff(struct vm_area_struct *v)
{
	return v->vm_pgoff + vma_pages(v) - 1;
}

/* 定义 anon_vma_chain 区间树遍历函数 */
INTERVAL_TREE_DEFINE(struct anon_vma_chain, rb, unsigned long, rb_subtree_last,
		     avc_start_pgoff, avc_last_pgoff,
		     static inline, smap_anon_vma_interval_tree)

/* 定义 vm_area_struct 区间树遍历函数 */
INTERVAL_TREE_DEFINE(struct vm_area_struct, shared.rb, unsigned long,
		     shared.rb_subtree_last, vma_start_pgoff, vma_last_pgoff,
		     static inline, smap_vma_interval_tree)

/* foreach 宏 */
#define smap_anon_vma_interval_tree_foreach(avc, root, start, last) \
	for ((avc) = smap_anon_vma_interval_tree_iter_first(root, start, last); \
	     (avc); (avc) = smap_anon_vma_interval_tree_iter_next((avc), start, last))

#define smap_vma_interval_tree_foreach(vma, root, start, last) \
	for ((vma) = smap_vma_interval_tree_iter_first(root, start, last); \
	     (vma); (vma) = smap_vma_interval_tree_iter_next((vma), start, last))

static unsigned long vma_pgoff_address(pgoff_t pgoff, unsigned long nr_pages,
				       struct vm_area_struct *vma)
{
	unsigned long address;

	if (pgoff >= vma->vm_pgoff) {
		address = vma->vm_start + ((pgoff - vma->vm_pgoff) << PAGE_SHIFT);
		if (address < vma->vm_start || address >= vma->vm_end)
			address = -EFAULT;
	} else if (pgoff + nr_pages - 1 >= vma->vm_pgoff) {
		address = vma->vm_start;
	} else {
		address = -EFAULT;
	}
	return address;
}

static inline unsigned long vma_address(struct page *page,
					struct vm_area_struct *vma)
{
	return vma_pgoff_address(page_to_pgoff(page), compound_nr(page), vma);
}

static bool page_task_one(struct folio *folio, struct vm_area_struct *vma,
			  unsigned long address, void *arg)
{
	struct page_task_arg *pta = arg;
	struct task_struct *task;

	if (!(vma && vma->vm_mm))
		return true;

	rcu_read_lock();
	task = vma->vm_mm->owner;
	rcu_read_unlock();
	if (!task)
		return true;

	if (pta->type == PAGE_PID_TYPE) {
		if (pta->pid == task->pid) {
			pta->found = true;
			return false;
		}
		return true;
	}

	pta->found = true;
	pta->node = cpu_to_node(cpumask_first(&task->cpus_mask));
	pta->nr_cpus_allowed = task->nr_cpus_allowed;

	return false;
}

static struct anon_vma *smap_folio_anon_vma(struct folio *folio)
{
	unsigned long mapping = (unsigned long)folio->mapping;

	if ((mapping & PAGE_MAPPING_FLAGS) != PAGE_MAPPING_ANON)
		return NULL;
	return (void *)(mapping - PAGE_MAPPING_ANON);
}

static void smap_rmap_walk_anon(struct folio *folio, struct page_task_arg *pta)
{
	struct anon_vma *anon_vma;
	pgoff_t pgoff_start, pgoff_end;
	struct anon_vma_chain *anon_vmc;
	struct rmap_walk_control rwc = {
		.rmap_one = page_task_one,
		.arg = pta,
	};

	anon_vma = smap_folio_anon_vma(folio);
	if (!anon_vma)
		return;

	if (!anon_vma_trylock_read(anon_vma))
		return;

	pgoff_start = folio_pgoff(folio);
	pgoff_end = pgoff_start + folio_nr_pages(folio) - 1;
	smap_anon_vma_interval_tree_foreach(anon_vmc, &anon_vma->rb_root,
				    pgoff_start, pgoff_end) {
		struct vm_area_struct *vmas = anon_vmc->vma;
		unsigned long vm_address = vma_address(&folio->page, vmas);

		cond_resched();

		if (!rwc.rmap_one(folio, vmas, vm_address, rwc.arg))
			break;
	}

	anon_vma_unlock_read(anon_vma);
}

static void smap_rmap_walk_file(struct folio *folio, struct page_task_arg *pta)
{
	struct address_space *f_mapping = folio_mapping(folio);
	pgoff_t pgoff_start, pgoff_end;
	struct vm_area_struct *vmas;
	struct rmap_walk_control rwc = {
		.rmap_one = page_task_one,
		.arg = pta,
	};

	if (!f_mapping)
		return;

	pgoff_start = folio_pgoff(folio);
	pgoff_end = pgoff_start + folio_nr_pages(folio) - 1;

	if (!i_mmap_trylock_read(f_mapping))
		return;

	smap_vma_interval_tree_foreach(vmas, &f_mapping->i_mmap, pgoff_start,
			     pgoff_end) {
		unsigned long vm_address = vma_address(&folio->page, vmas);

		cond_resched();

		if (!rwc.rmap_one(folio, vmas, vm_address, rwc.arg))
			goto done;
	}

done:
	i_mmap_unlock_read(f_mapping);
}

static void *smap_page_rmapping(struct page *page)
{
	unsigned long mapping;
	page = compound_head(page);

	mapping = (unsigned long)page->mapping;
	mapping &= ~PAGE_MAPPING_FLAGS;

	return (void *)mapping;
}

void find_page_task(struct page *page, int is_locked, struct page_task_arg *pta)
{
	struct folio *folio = page_folio(page);

	if (!smap_page_rmapping(page))
		return;

	/* KSM 页面暂不处理 */
	if (folio_test_ksm(folio))
		return;

	if (folio_test_anon(folio))
		smap_rmap_walk_anon(folio, pta);
	else
		smap_rmap_walk_file(folio, pta);
}
EXPORT_SYMBOL(find_page_task);