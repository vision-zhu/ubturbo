// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 * Description: SMAP : hist_dev
 */
#include <asm/types.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/kthread.h>
#include <linux/hrtimer.h>
#include <linux/vmalloc.h>
#include <linux/mmzone.h>
#include <linux/pfn.h>
#include <linux/workqueue.h>
#include <linux/hugetlb.h>
#include <linux/ktime.h>
#include <linux/version.h>
#include <linux/spinlock.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include <linux/migrate.h>

#include "check.h"
#include "access_iomem.h"
#include "access_acpi_mem.h"
#include "access_tracking.h"
#include "bus.h"
#include "ub_hist.h"
#include "hist_ops.h"
#include "hist_tracking.h"
#include "smap_rmap.h"

#define to_access_tracking_dev(n) \
	container_of(n, struct access_tracking_dev, ldev)
#define to_delay_work(n) container_of(n, struct delayed_work, work)
#define delay_work_to_dev(n) \
	container_of(n, struct access_tracking_dev, scan_work)
#define WORK_QUEUE_NAME_LEN 32
#define HIST_TRACKING_DEFAULT_PERIOD 200
#define SCAN_TIME_MAX 100000

#undef pr_fmt
#define pr_fmt(fmt) "hist: " fmt

extern struct list_head access_dev;

/* 热页迁回阈值配置 */
u32 hot_page_threshold = HOT_PAGE_DEFAULT_THRESHOLD;

/* 轮询选择本地 NUMA 的计数器（用于进程无 CPU 限制时的备选方案） */
static atomic_t hot_page_rr_counter = ATOMIC_INIT(0);

/* 根据页面获取进程亲和的 NUMA 节点 */
static int get_page_task_numa(struct page *page)
{
	struct page_task_arg pta = { .type = PAGE_NODE_TYPE };
	struct page *head;

	head = compound_head(page);
	find_page_task(head, 0, &pta);

	/* 如果进程有 CPU 限制，使用其绑定的 NUMA */
	if (pta.found && pta.nr_cpus_allowed < num_online_cpus())
		return pta.node;

	return NUMA_NO_NODE;
}

static inline void reset_actc_data(struct access_tracking_dev *hdev)
{
	size_t len = hdev->page_count * sizeof(actc_t);

	if (hdev->access_bit_actc_data)
		memset(hdev->access_bit_actc_data, 0, len);
}

static void hist_tracking_enable(struct device *ldev)
{
	struct access_tracking_dev *hdev;

	hdev = to_access_tracking_dev(ldev);
	down_write(&hdev->buffer_lock);
	reset_actc_data(hdev);
	up_write(&hdev->buffer_lock);
	hdev->enable_on = true;
	hist_thread_resume();
}

static int hist_tracking_disable(struct device *ldev)
{
	struct access_tracking_dev *hdev;

	hdev = to_access_tracking_dev(ldev);
	hdev->enable_on = false;
	hist_thread_pause();
	return 0;
}

static void actc_buffer_deinit(struct access_tracking_dev *hdev)
{
	hdev->page_count = 0;
	if (hdev->access_bit_actc_data) {
		vfree(hdev->access_bit_actc_data);
		hdev->access_bit_actc_data = NULL;
	}
}

static inline int hist_get_page_size(struct access_tracking_dev *hdev)
{
	if (hdev->page_size_mode == PAGE_MODE_2M) {
		return g_pagesize_huge;
	}
	return PAGE_SIZE;
}

static u64 calc_access_len(struct access_tracking_dev *hdev)
{
	int page_size = hist_get_page_size(hdev);
	u64 page_count;
	if (hdev->node >= nr_local_numa) {
		page_count = get_node_page_cnt_iomem(hdev->node, page_size);
	} else {
		page_count = get_node_actc_len(hdev->node, page_size);
	}
	pr_debug("histogram tracking node: %d, got page count: %llu\n",
		 hdev->node, page_count);

	return page_count;
}

static void hist_dev_pgsize_update(u8 page_size_mode)
{
	u32 pgsize = page_size_mode == PAGE_MODE_2M ? SIZE_2M : SIZE_4K;
	hist_update_pgsize(pgsize);
}

static int actc_buffer_reinit(struct access_tracking_dev *hdev)
{
	u64 page_count;
	page_count = calc_access_len(hdev);
	if (!page_count) {
		pr_info("no page found on node: %d\n", hdev->node);
	}
	if (hdev->page_count == page_count) {
		reset_actc_data(hdev);
		return 0;
	}
	actc_buffer_deinit(hdev);
	if (page_count) {
		hdev->access_bit_actc_data = vzalloc(page_count * sizeof(actc_t));
		if (!hdev->access_bit_actc_data) {
			return -ENOMEM;
		}
	}
	hdev->page_count = page_count;
	return 0;
}

static int hist_tracking_reinit_actc_buffer(struct device *ldev)
{
	int ret;
	struct access_tracking_dev *hdev = to_access_tracking_dev(ldev);
	hist_set_iomem();
	down_write(&hdev->buffer_lock);
	ret = actc_buffer_reinit(hdev);
	if (ret) {
		pr_err("Actc buffer reinit failed. ret:%d\n", ret);
	}
	up_write(&hdev->buffer_lock);
	return ret;
}

static int hist_tracking_set_page_size(struct device *ldev, u8 pgsize)
{
	int ret;
	struct access_tracking_dev *hdev;
	hdev = to_access_tracking_dev(ldev);

	if (pgsize != PAGE_MODE_4K && pgsize != PAGE_MODE_2M) {
		pr_err("invalid page size\n");
		return -EINVAL;
	}
	down_write(&hdev->buffer_lock);
	hdev->enable_on = false;
	hdev->page_size_mode = pgsize;
	hist_dev_pgsize_update(pgsize);
	ret = actc_buffer_reinit(hdev);
	if (ret) {
		up_write(&hdev->buffer_lock);
		pr_err("Actc buffer reinit failed. ret:%d\n", ret);
		return ret;
	}
	up_write(&hdev->buffer_lock);
	return ret;
}

static struct tracking_operations g_hist_tracking_ops = {
	.tracking_enable = hist_tracking_enable,
	.tracking_disable = hist_tracking_disable,
	.tracking_reinit_actc_buffer = hist_tracking_reinit_actc_buffer,
	.tracking_set_page_size = hist_tracking_set_page_size,
};

static int actc_buffer_init(struct access_tracking_dev *hdev)
{
	hdev->page_count = calc_access_len(hdev);
	pr_info("page count: %llu for node: %d\n", hdev->page_count,
		hdev->node);
	if (!hdev->page_count)
		return 0;
	hdev->access_bit_actc_data = vzalloc(hdev->page_count * sizeof(actc_t));
	if (!hdev->access_bit_actc_data) {
		pr_err("unable to alloc mem for histogram tracking ACTC buffer\n");
		hdev->page_count = 0;
		return -ENOMEM;
	}
	return 0;
}

static void scan_hist(struct access_tracking_dev *hdev)
{
	u32 pgcount = 0, addr_pg = 0;
	u64 dev_page_count = hdev->page_count;
	struct ram_segment *rseg, *tmp;
	struct addr_seg addr_seg;
	if (PAGE_SIZE == PAGE_SIZE_64K)
		dev_page_count *= PAGE_SIZE_64K_DIV_4K;
	read_lock(&rem_ram_list_lock);
	list_for_each_entry_safe(rseg, tmp, &remote_ram_list, node) {
		if (rseg->numa_node != hdev->node) {
			continue;
		}
		addr_seg.start = rseg->start;
		addr_seg.size = rseg->end - rseg->start + 1;
		addr_pg = addr_seg.size >>
			  (hdev->page_size_mode == PAGE_MODE_2M ?
				   HIST_ADDR_SHIFT_2M :
				   HIST_ADDR_SHIFT_4K);
		if (pgcount + addr_pg > dev_page_count) {
			pr_warn("page index: %u exceeds upper bound of page count: %llu\n",
				pgcount + addr_pg, dev_page_count);
			break;
		}
		fetch_hist_actc_buf(hdev->access_bit_actc_data + pgcount,
				    &addr_seg);
		pgcount += addr_pg;
	}
	read_unlock(&rem_ram_list_lock);
}

/* 获取本地 NUMA 节点空闲页数量 */
static unsigned long get_local_node_nr_free_pages(int nid)
{
	pg_data_t *pgdat = NODE_DATA(nid);
	struct zone *zones;
	int i;
	unsigned long count = 0;

	if (!pgdat || nid >= nr_local_numa)
		return 0;

	zones = pgdat->node_zones;
	for (i = 0; i < MAX_NR_ZONES; i++)
		count += zone_page_state(zones + i, NR_FREE_PAGES);

	return count;
}

/* 简单轮询选择有空闲内存的本地 NUMA 节点（备选方案） */
static int select_local_numa_rr(void)
{
	int nid, start_nid;
	unsigned long max_free = 0;
	int best_nid = NUMA_NO_NODE;

	if (nr_local_numa <= 0)
		return NUMA_NO_NODE;

	start_nid = atomic_read(&hot_page_rr_counter) % nr_local_numa;

	for (nid = 0; nid < nr_local_numa; nid++) {
		int check_nid = (start_nid + nid) % nr_local_numa;
		unsigned long free_pages = get_local_node_nr_free_pages(check_nid);

		if (free_pages > max_free) {
			max_free = free_pages;
			best_nid = check_nid;
		}
	}

	if (best_nid >= 0)
		atomic_inc(&hot_page_rr_counter);

	return best_nid;
}

/* 热页迁回的新页分配回调 */
static struct folio *hot_page_alloc_new_folio(struct folio *src, unsigned long private)
{
	int nid = (int)private;
	gfp_t gfp = GFP_HIGHUSER_MOVABLE | __GFP_THISNODE | __GFP_NORETRY;

	if (folio_test_hugetlb(src))
		return NULL;  /* 大页暂不处理 */

	return __folio_alloc(gfp, folio_order(src), nid, NULL);
}

/* 热页检测与迁回函数 */
static void hot_page_migrate_back(struct access_tracking_dev *hdev)
{
	u32 pg_idx = 0;
	u64 pa, page_size;
	struct ram_segment *rseg;
	struct addr_seg addr_seg;
	struct folio **folios;
	unsigned int nr_folios = 0, nr_migrated = 0;
	unsigned long pfn;
	struct page *page;
	int dest_nid;
	int ret;

	if (!hdev->enable_on || hot_page_threshold == 0)
		return;

	page_size = hist_get_page_size(hdev);
	folios = vzalloc(MAX_HOT_PAGES_PER_BATCH * sizeof(struct folio *));
	if (!folios)
		return;

	/* 检测热页 */
	read_lock(&rem_ram_list_lock);
	list_for_each_entry(rseg, &remote_ram_list, node) {
		u32 addr_pg_idx;
		u32 idx;

		if (rseg->numa_node != hdev->node)
			continue;

		addr_seg.start = rseg->start;
		addr_seg.size = rseg->end - rseg->start + 1;
		addr_pg_idx = addr_seg.size >> (hdev->page_size_mode == PAGE_MODE_2M ?
				HIST_ADDR_SHIFT_2M : HIST_ADDR_SHIFT_4K);

		for (pa = rseg->start; pa <= rseg->end && nr_folios < MAX_HOT_PAGES_PER_BATCH;
		     pa += page_size) {
			idx = pg_idx + ((pa - rseg->start) / page_size);

			if (idx >= hdev->page_count)
				break;

			/* 检查频次是否超过阈值 */
			if (hdev->access_bit_actc_data[idx] <= hot_page_threshold)
				continue;

			pfn = PHYS_PFN(pa);
			if (!pfn_valid(pfn))
				continue;

			page = pfn_to_online_page(pfn);
			if (!page || !PageHead(page))
				continue;

			/* 尝试获取 folio 引用 */
			if (!folio_try_get(page_folio(page)))
				continue;

			folios[nr_folios++] = page_folio(page);
		}
		pg_idx += addr_pg_idx;
	}
	read_unlock(&rem_ram_list_lock);

	if (nr_folios == 0) {
		vfree(folios);
		return;
	}

	/* 选择目标 NUMA：优先使用 PID 亲和的 NUMA */
	dest_nid = get_page_task_numa(&folios[0]->page);
	if (dest_nid < 0 || dest_nid >= nr_local_numa) {
		/* 进程无 CPU 限制时，使用轮询备选方案 */
		dest_nid = select_local_numa_rr();
	}

	if (dest_nid < 0 || dest_nid >= nr_local_numa) {
		pr_debug("hot_page: no valid local NUMA found\n");
		goto putback;
	}

	/* 使用内核导出的 isolate_and_migrate_folios 执行迁移 */
	ret = isolate_and_migrate_folios(folios, nr_folios, hot_page_alloc_new_folio,
					  NULL, dest_nid, MIGRATE_ASYNC, &nr_migrated);

	pr_debug("hot_page migrate: %u pages from node %d to node %d, migrated %u, ret: %d\n",
		 nr_folios, hdev->node, dest_nid, nr_migrated, ret);

putback:
	vfree(folios);
}

static void update_hist_actc_batch(void)
{
	struct access_tracking_dev *hdev, *n;
	list_for_each_entry_safe(hdev, n, &access_dev, list) {
		if (!hdev->is_hist)
			continue;
		down_write(&hdev->buffer_lock);
		scan_hist(hdev);
		/* 热页检测与迁回 */
		hot_page_migrate_back(hdev);
		up_write(&hdev->buffer_lock);
	}
}

static void hist_tracking_deinit(void)
{
	struct access_tracking_dev *hdev, *n;
	list_for_each_entry_safe(hdev, n, &access_dev, list) {
		if (!hdev->is_hist)
			continue;
		tracking_dev_remove(hdev->tracking_dev);
		actc_buffer_deinit(hdev);
		device_unregister(&hdev->ldev);
		kfree(hdev);
	}
}

/* debugfs 热页阈值配置接口 */
static ssize_t hot_threshold_read(struct file *filp, char __user *buf,
				   size_t count, loff_t *ppos)
{
	char tmp[32];
	int len = scnprintf(tmp, sizeof(tmp), "%u\n", hot_page_threshold);
	return simple_read_from_buffer(buf, count, ppos, tmp, len);
}

static ssize_t hot_threshold_write(struct file *filp, const char __user *buf,
				    size_t count, loff_t *ppos)
{
	char tmp[32];
	unsigned long val;

	if (count >= sizeof(tmp))
		return -EINVAL;
	if (copy_from_user(tmp, buf, count))
		return -EFAULT;
	tmp[count] = '\0';

	if (kstrtoul(tmp, 10, &val))
		return -EINVAL;
	if (val > HOT_PAGE_MAX_THRESHOLD)
		return -EINVAL;

	hot_page_threshold = val;
	pr_info("hot_page_threshold set to %u\n", hot_page_threshold);
	return count;
}

static const struct file_operations hot_threshold_fops = {
	.owner = THIS_MODULE,
	.read = hot_threshold_read,
	.write = hot_threshold_write,
	.llseek = default_llseek,
};

static struct dentry *hot_page_debugfs_dir;

static int hot_page_debugfs_init(void)
{
	hot_page_debugfs_dir = debugfs_create_dir("smap", NULL);
	if (!hot_page_debugfs_dir) {
		pr_err("failed to create smap debugfs dir\n");
		return -ENOMEM;
	}

	debugfs_create_file("hot_threshold", 0644, hot_page_debugfs_dir, NULL,
			    &hot_threshold_fops);
	return 0;
}

static void hot_page_debugfs_exit(void)
{
	debugfs_remove_recursive(hot_page_debugfs_dir);
}

void hist_tracking_exit(void)
{
	hot_page_debugfs_exit();
	hist_tracking_deinit();
}
EXPORT_SYMBOL(hist_tracking_exit);

void access_tracking_dev_release(struct device *dev)
{
	pr_debug("Releasing device %s\n", dev_name(dev));
}

static int hist_tracking_init(void)
{
	int ret;
	unsigned int node;
	struct access_tracking_dev *hdev;

	if (nr_local_numa < 0)
		return -EINVAL;

	for (node = (unsigned int)nr_local_numa; node < SMAP_MAX_NUMNODES;
	     ++node) {
		hdev = kzalloc(sizeof(struct access_tracking_dev), GFP_KERNEL);
		if (!hdev) {
			pr_err("unable to alloc mem for histogram tracking device\n");
			goto put_dev;
		}

		hdev->node = node;
		hdev->page_size_mode = PAGE_MODE_2M;
		hdev->is_hist = true;

		ret = actc_buffer_init(hdev);
		if (ret) {
			pr_err("unable to init ACTC buffer, ret: %d\n", ret);
			goto free_hdev;
		}

		init_rwsem(&hdev->buffer_lock);
		device_initialize(&hdev->ldev);
		hdev->ldev.release = access_tracking_dev_release;
		ret = dev_set_name(&hdev->ldev, "hist_tracking_dev%d", node);
		if (ret) {
			pr_err("unable to set histogram tracking device name, ret: %d\n",
			       ret);
			goto deinit_buf;
		}

		ret = device_add(&hdev->ldev);
		if (ret) {
			pr_err("unable to add histogram tracking device\n");
			goto deinit_buf;
		}

		hdev->tracking_dev = tracking_dev_add(
			&hdev->ldev, &g_hist_tracking_ops, hdev->node);
		if (!hdev->tracking_dev) {
			pr_err("unable to add tracking for node: %d\n",
			       hdev->node);
			goto del_dev;
		}

		list_add_tail(&hdev->list, &access_dev);
	}

	return 0;

del_dev:
	device_del(&hdev->ldev);
deinit_buf:
	actc_buffer_deinit(hdev);
	put_device(&hdev->ldev);
free_hdev:
	kfree(hdev);
put_dev:
	hist_tracking_deinit();
	pr_err("smap tracking dev init failed.\n");
	return -ENODEV;
}

int hist_module_init(void)
{
	int ret;
	ret = init_acpi_mem();
	if (ret) {
		pr_err("parse ACPI table failed: %d\n", ret);
		return ret;
	}
	ret = hist_init(SIZE_2M);
	if (ret) {
		pr_err("init SMAP histogram device failed, ret: %d\n", ret);
		return ret;
	}

	ret = hist_tracking_init();
	if (ret) {
		pr_err("init histogram tracking device failed, ret: %d\n", ret);
		goto err_tracking_add;
	}

	/* 初始化 debugfs 热页阈值配置接口 */
	ret = hot_page_debugfs_init();
	if (ret) {
		pr_warn("hot page debugfs init failed\n");
		/* 不影响主功能，继续执行 */
	}

	hist_set_flush_actc_cb(update_hist_actc_batch);
	pr_info("smap hist tracking init success.\n");
	return 0;

err_tracking_add:
	hist_deinit();
	reset_acpi_mem();
	return ret;
}
