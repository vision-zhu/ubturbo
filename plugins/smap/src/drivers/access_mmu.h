/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 * Description: smap access mmu module
 */

#ifndef _SRC_ACCESS_MMU_H
#define _SRC_ACCESS_MMU_H

#include "access_pid.h"

extern struct list_head remote_ram_list;
extern rwlock_t rem_ram_list_lock;

void walk_pid_pagemap(struct pagemapread *pm);
int ap_bm_update(struct access_pid *ap, int node_id, u64 acidx, struct page *page);
int set_non_anon_bm(struct access_pid *ap, u64 acidx, u64 paddr, int nid);
struct mm_struct *get_mm_by_pid(pid_t pid);
#endif /* _SRC_ACCESS_MMU_H */
