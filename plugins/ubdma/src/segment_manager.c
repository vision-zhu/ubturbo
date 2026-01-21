/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * Description: ub-dma manager
 */

#include <linux/kernel.h>

#include "ub_dma_log.h"
#include "segment_manager.h"

LIST_HEAD(urma_meta_sge_list);

DEFINE_RWLOCK(urma_meta_sge_list_lock);

static int check_mem_info(u64 pa_start, u64 pa_end)
{
    unsigned long pfn_start;
    unsigned long pfn_end;
    int start_nid;
    int end_nid;

    if (pa_end <= pa_start) {
        ub_dma_log_err("pa_end is less than pa_start\n");
        return -EINVAL;
    }
    pfn_start = PHYS_PFN(pa_start);
    pfn_end = PHYS_PFN(pa_end);
    if (!pfn_valid(pfn_start) || !pfn_to_online_page(pfn_start) ||
        !pfn_valid(pfn_end) || !pfn_to_online_page(pfn_end)) {
        ub_dma_log_err("pfn_start or pfn_end is invalid\n");
        return -EINVAL;
    }
    start_nid = pfn_to_nid(pfn_start);
    end_nid = pfn_to_nid(pfn_end);
    if (start_nid != end_nid) {
        ub_dma_log_err("start_nid(%d) is not equal to end_nid(%d)\n", start_nid, end_nid);
        return -EINVAL;
    }

    return 0;
}

static inline bool is_target_segment(uint64_t start_va, uint64_t end_va, uint64_t addr, uint32_t len)
{
    if (start_va <= addr && end_va >= (addr + len - 1)) {
        return true;
    }
    return false;
}

int get_urma_trans_segment(struct urma_trans_segment_info *src_info, struct urma_trans_segment_info *dst_info)
{
    struct urma_sge_info *sge;

    write_lock(&urma_meta_sge_list_lock);
    list_for_each_entry(sge, &urma_meta_sge_list, node) {
        if (src_info->sge == NULL && is_target_segment(sge->start_va, sge->end_va, src_info->addr, src_info->len)) {
            src_info->sge = sge->sge;
        }
        if (dst_info->sge == NULL && is_target_segment(sge->start_va, sge->end_va, dst_info->addr, dst_info->len)) {
            dst_info->sge = sge->i_seg;
        }
        if (dst_info->sge != NULL && src_info->sge != NULL) {
            write_unlock(&urma_meta_sge_list_lock);
            return 0;
        }
    }

    write_unlock(&urma_meta_sge_list_lock);
    ub_dma_log_err("fail get urma trans segment\n");
    return -EINVAL;
}

int ub_dma_register_segment(u64 pa_start, u64 pa_end)
{
    int ret;
    uint64_t start_va;
    uint64_t end_va;
    struct urma_sge_info *sge;
    struct urma_sge_info *_sge;

    ret = check_mem_info(pa_start, pa_end);
    if (ret) {
        return ret;
    }

    sge = kzalloc(sizeof(struct urma_sge_info), GFP_KERNEL);
    if (!sge) {
        ub_dma_log_err("alloc sge failed");
        return -ENOMEM;
    }

    start_va = (uint64_t)phys_to_virt(pa_start);
    end_va = (uint64_t)phys_to_virt(pa_end);
    write_lock(&urma_meta_sge_list_lock);
    list_for_each_entry(_sge, &urma_meta_sge_list, node) {
        if (start_va >= _sge->start_va && start_va <= _sge->end_va) {
            ub_dma_log_err("register start_va is in meta sge\n");
            ret = -EINVAL;
            goto err_register_urma_sge;
        }
    }

    ret = urma_register_segment(start_va, end_va, sge);
    if (ret) {
        ub_dma_log_err("register segment failed\n");
        goto err_register_urma_sge;
    }

    sge->start_va = start_va;
    sge->end_va = end_va;
    list_add_tail(&sge->node, &urma_meta_sge_list);
    ub_dma_log_debug("register segment success\n");
    write_unlock(&urma_meta_sge_list_lock);

    return 0;

err_register_urma_sge:
    write_unlock(&urma_meta_sge_list_lock);
    kfree(sge);

    return ret;
}

int ub_dma_unregister_segment(u64 start_pa, u64 end_pa)
{
    struct urma_sge_info *sge;
    struct urma_sge_info *_sge;
    u64 start_va;
    u64 end_va;

    start_va = (uint64_t)phys_to_virt(start_pa);
    end_va = (uint64_t)phys_to_virt(end_pa);
    write_lock(&urma_meta_sge_list_lock);
    list_for_each_entry_safe(sge, _sge, &urma_meta_sge_list, node) {
        if (start_va >= sge->start_va && end_va <= sge->end_va) {
            unregister_urma_segment(sge);
            list_del(&sge->node);
            kfree(sge);
            write_unlock(&urma_meta_sge_list_lock);
            return 0;
        }
    }
    write_unlock(&urma_meta_sge_list_lock);

    ub_dma_log_err("unregister segment failed\n");
    return -ENOENT;
}

void ub_dma_unregister_all_segment(void)
{
    struct urma_sge_info *sge;
    struct urma_sge_info *_sge;

    write_lock(&urma_meta_sge_list_lock);
    list_for_each_entry_safe(sge, _sge, &urma_meta_sge_list, node) {
        unregister_urma_segment(sge);
        list_del(&sge->node);
        kfree(sge);
    }
    write_unlock(&urma_meta_sge_list_lock);
}