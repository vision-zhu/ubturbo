/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2024. All rights reserved.
 * Description: SMAP Tiering Memory Solution: get accessed bit模块
 */

#ifndef _SRC_ACCESSED_BIT_H
#define _SRC_ACCESSED_BIT_H

#include "access_pid.h"
#include "access_iomem.h"
#include "access_acpi_mem.h"
#include "check.h"
#include "kvm_pgtable.h"

#define SCAN_PERIOD_UNIT_MS 50

/* 扫描结果缓冲区容量：MMAPLOCK_BATCH_SIZE (64MB) / PAGE_SIZE (4KB) */
#define SCAN_RESULT_CAPACITY 16384

extern struct list_head remote_ram_list;
extern int nr_local_numa;
extern struct access_pid_struct ap_data;
extern bool remote_ram_changed;

/* 方案1：Segment缓存，避免链表遍历 */
#define SMAP_SEG_CACHE_SIZE 8

struct segment_cache_entry {
	u64 seg_start;		/* segment起始物理地址 */
	u64 seg_end;		/* segment结束物理地址 */
	u64 base_offset;	/* 该segment前所有同nid segment的累计offset（以页为单位） */
	int nid;		/* NUMA节点ID */
	bool valid;		/* 缓存是否有效 */
};

struct statistics_tracking_info {
	pid_t pid;
	int l1_node;
	int l2_node;
	u64 page_num[NR_LEVEL]; // 近远端页面数
	u64 *vaddr[NR_LEVEL]; // 虚地址数组, 顺序排序
	u32 scan_num; // 已扫描次数，次数 % 窗口个数 = 待更新窗口
	u8 **sliding_windows; // 统计时长除以单次周期 = 窗口个数， 单个窗口大小 = Pagenum(l1) + Pagenum(l2)
	struct list_head node;
	u64 window_num; // 窗口个数
};

struct ham_tracking_info {
	pid_t pid;
	int l1_node;
	int l2_node;
	struct proc_dir_entry *pde;
	u64 len[NR_LEVEL];
	u64 *paddr[NR_LEVEL];
	actc_t *freq[NR_LEVEL];
	struct list_head node;
};

struct hva_info {
	u64 va;
	int nid;
};

/* 扫描结果条目 - 用于延迟处理 nid/pa_idx 计算 */
struct scan_result_entry {
	phys_addr_t paddr;      /* 物理地址 */
	unsigned long addr;     /* 虚拟地址（用于冷页面匹配） */
	bool hot;  /* 是否需要更新 actc_data */
};

struct pte_walk {
	pid_t pid;
	scan_type type;
	int nid;
	actc_t *buf;
	int buf_len;
	bool flag;
	struct hva_info *hva_info;
	u64 index;
	int page_size;
	u64 nr_page[NR_LEVEL];
	u64 statistic_cnt;
	u64 *statistic_vaddr;
	struct access_pid *ap;
	bool group_hot;
	u64 group_hot_skip_cnt;		/* group_hot策略跳过的页面计数 */
	/* 冷页面统计相关（仅最后一次 NORMAL_SCAN 使用，双指针匹配） */
	u64 *cold_vas;			/* 已排序的冷页面 VA 数组 */
	u64 cold_count;			/* 冷页面数量 */
	u64 cold_match_idx;		/* 当前匹配位置索引 */
	u64 remote_cold_count[SMAP_MAX_NUMNODES]; /* 各远端 NUMA 冷页面计数 */
	/* 性能优化缓存（方案2：缓存 nid/adev/page 指针） */
	int last_nid;			/* 上一次的 NUMA 节点 ID */
	u64 last_paddr;			/* 上一次的物理地址 */
	u64 last_pa_idx;		/* 上一次的 pa_idx */
	bool last_is_hist;		/* 上一次的 is_hist */
	u64 last_segment_end;		/* 上一次的 segment end，用于边界检查 */
	struct access_tracking_dev *last_adev; /* 上一次的 adev 指针 */
	/* 扫描结果缓冲区：锁内收集，锁外批量处理（动态分配，避免栈溢出） */
	struct scan_result_entry *scan_results;
	u64 scan_result_cnt;		/* 当前缓冲区计数 */
};

struct freq_info {
	u64 hpa;
	actc_t freq;
};

int pid_pte_mkold(struct access_pid *ap);
int smap_create_tracking_info_file(struct ham_tracking_info *info);
int get_ham_pages_freqs(pid_t pid, struct freq_info **freq_info_array,
			uint64_t *freq_info_num);
struct file *get_kvm_file_from_task(struct task_struct *task);
int scan_accessed_bit_forward_vm(struct access_pid *ap, int page_size);
int scan_accessed_bit_forward_mm(struct access_pid *ap, int page_size);
int scan_hva_info(pid_t pid_nr, u64 *l1_page_num, u64 *l2_page_num,
		  u64 **l1_vaddr, u64 **l2_vaddr);
int scan_hva_info_4k(pid_t pid, u64 *l1_page_num, u64 *l2_page_num,
		     u64 **l1_vaddr, u64 **l2_vaddr);
#endif /* _SRC_ACCESSED_BIT_H */
