/*
 * smap is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#define _GNU_SOURCE
#include <fcntl.h>
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sched.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/param.h>

#include "smap_user_log.h"
#include "securec.h"
#include "device.h"
#include "access_ioctl.h"
#include "advanced-strategy/scene.h"
#include "smap_config.h"
#include "strategy/strategy_config.h"
#include "strategy/strategy.h"
#include "strategy/migration.h"
#include "manage.h"
#include "manage_internal.h"
#include "smap_inner_interface.h"

/* 写入 grouped 配置；attr 已注册进槽位表时调用方须持该槽位 attrLock */
void SetGroupedProcessConfig(ProcessAttr *attr, pid_t pid, const NumaBitmap *nodeBitmap,
                             const GroupMigrationPolicy *policy)
{
    attr->pid = pid;
    attr->scanTime = SCAN_TIME_2M;
    attr->duration = 0;
    attr->scanType = NORMAL_SCAN;
    attr->type = VM_TYPE;
    attr->migrateMode = MIG_MEMSIZE_MODE;
    attr->remoteNumaCnt = NumaGetL2Count(nodeBitmap, GetNrLocalNuma());
    attr->enableSwap = true;
    attr->initLocalMemRatio = HUNDRED;
    (void)NumaBitmapCopy(&attr->numaAttr.numaNodes, nodeBitmap);
    attr->groupPolicy = *policy;
    attr->groupSwapLastTotalPages = 0;
    attr->groupSwapStableTotalRounds = 0;
    attr->groupSwapTotalPagesValid = false;
    attr->groupSwapFrozen = false;
    attr->pendingGroupPolicy.valid = false;
    attr->autoRemoveWhenRemoteEmpty = false;
    attr->syncWaitRemoteEmpty = false;
    attr->syncWaitRemoteEmptySnapshotValid = false;
    if (time(&attr->scanStart) == (time_t)-1) {
        SMAP_LOGGER_ERROR("get time error");
    }
}

static void ResetGroupedPolicyRuntime(GroupMigrationPolicy *policy)
{
    if (!policy) {
        return;
    }
    /*
     * Pending policy is copied from a new user request. Rebuild runtime counters
     * from current numa_maps before it replaces the active policy.
     */
    for (int i = 0; i < policy->groupCount; i++) {
        MigrationGroupAttr *group = &policy->groups[i];
        group->swapCandidateRounds = 0;
        for (int j = 0; j < group->targetCount; j++) {
            group->targets[j].usedPages = 0;
        }
    }
}

static int CollectGroupedTargetEntries(GroupMigrationPolicy *policy, int targetNid,
                                       int groupIdx[MAX_GROUP_TARGET_ENTRY], int targetIdx[MAX_GROUP_TARGET_ENTRY])
{
    int count = 0;
    for (int i = 0; i < policy->groupCount; i++) {
        MigrationGroupAttr *group = &policy->groups[i];
        for (int j = 0; j < group->targetCount; j++) {
            if (group->targets[j].nid != targetNid) {
                continue;
            }
            if (count >= MAX_GROUP_TARGET_ENTRY) {
                SMAP_LOGGER_ERROR("Grouped target entry count exceeds limit.");
                return -EINVAL;
            }
            groupIdx[count] = i;
            targetIdx[count] = j;
            count++;
        }
    }
    return count;
}

static int InitGroupedTargetUsedPages(pid_t pid, GroupMigrationPolicy *policy, int targetNid, uint64_t residentPages)
{
    int groupIdx[MAX_GROUP_TARGET_ENTRY] = { 0 };
    int targetIdx[MAX_GROUP_TARGET_ENTRY] = { 0 };
    int entryCount = CollectGroupedTargetEntries(policy, targetNid, groupIdx, targetIdx);
    if (entryCount <= 0) {
        SMAP_LOGGER_ERROR("pid %d has unmanaged remote node %d resident pages %llu.", pid, targetNid, residentPages);
        return -EINVAL;
    }

    uint64_t quotaSum = 0;
    for (int i = 0; i < entryCount; i++) {
        GroupTargetAttr *target = &policy->groups[groupIdx[i]].targets[targetIdx[i]];
        if (UINT64_MAX - quotaSum < target->quotaPages) {
            SMAP_LOGGER_ERROR("pid %d remote node %d quota sum overflow.", pid, targetNid);
            return -EINVAL;
        }
        quotaSum += target->quotaPages;
    }
    if (quotaSum == 0) {
        SMAP_LOGGER_ERROR("pid %d remote node %d quota sum is zero.", pid, targetNid);
        return -EINVAL;
    }
    if (residentPages > quotaSum) {
        SMAP_LOGGER_ERROR("pid %d remote node %d resident pages %llu exceed quota sum %llu.", pid, targetNid,
                          residentPages, quotaSum);
        return -EINVAL;
    }

    uint64_t assignedPages = 0;
    for (int i = 0; i < entryCount; i++) {
        GroupTargetAttr *target = &policy->groups[groupIdx[i]].targets[targetIdx[i]];
        target->usedPages = (__uint128_t)residentPages * target->quotaPages / quotaSum;
        assignedPages += target->usedPages;
    }

    uint64_t remainingPages = residentPages - assignedPages;
    while (remainingPages > 0) {
        bool progressed = false;
        for (int i = 0; i < entryCount && remainingPages > 0; i++) {
            GroupTargetAttr *target = &policy->groups[groupIdx[i]].targets[targetIdx[i]];
            if (target->usedPages >= target->quotaPages) {
                continue;
            }
            target->usedPages++;
            remainingPages--;
            progressed = true;
        }
        if (!progressed) {
            SMAP_LOGGER_ERROR("pid %d remote node %d used pages cannot fit quota.", pid, targetNid);
            return -EINVAL;
        }
    }

    for (int i = 0; i < entryCount; i++) {
        GroupTargetAttr *target = &policy->groups[groupIdx[i]].targets[targetIdx[i]];
        if (target->usedPages > target->quotaPages) {
            SMAP_LOGGER_ERROR("pid %d remote node %d used pages %llu exceed quota %llu.", pid, targetNid,
                              target->usedPages, target->quotaPages);
            return -EINVAL;
        }
        SMAP_LOGGER_INFO("pid %d remote node %d group %d target used pages %llu.", pid, targetNid, groupIdx[i],
                         target->usedPages);
    }
    return 0;
}

int InitGroupedUsedPages(pid_t pid, GroupMigrationPolicy *policy, const uint64_t numaPages[SMAP_MAX_NUMNODES])
{
    int nrLocalNuma = GetNrLocalNuma();
    int nid;
    for_each_remote_nid(nid)
    {
        if (numaPages[nid] == 0) {
            continue;
        }
        int ret = InitGroupedTargetUsedPages(pid, policy, nid, numaPages[nid]);
        if (ret) {
            return ret;
        }
    }
    return 0;
}

int ProcessAddGroupedManage(pid_t pid, const NumaBitmap *nodeBitmap, const GroupMigrationPolicy *policy)
{
    int ret;
    int pidType = DetectPidType(pid);
    if (pidType < 0) {
        SMAP_LOGGER_ERROR("grouped pid %d check failed: %d.", pid, pidType);
        return pidType;
    }
    if (pidType != VM_TYPE) {
        SMAP_LOGGER_ERROR("grouped migrate out only supports VM, pid %d type %d.", pid, pidType);
        return -EINVAL;
    }
    if (!policy || !policy->enabled) {
        SMAP_LOGGER_ERROR("grouped policy of pid %d is invalid.", pid);
        return -EINVAL;
    }

    struct PidSlot *slot = PidSlotGetRef(pid);
    if (slot) {
        /* 活跃 attr 的配置/位图须持 attrLock 修改，与 worker 及其他 IPC 线程串行 */
        EnvMutexLock(&slot->attrLock);
        SetGroupedProcessConfig(slot->attr, pid, nodeBitmap, policy);
        EnvMutexUnlock(&slot->attrLock);
        PidSlotReleaseRefs(&slot, 1);
        SMAP_LOGGER_INFO("Update grouped pid %d success, group count %d.", pid, policy->groupCount);
        ret = SyncAllProcessConfig();
        if (ret) {
            SMAP_LOGGER_WARNING("Synchronize grouped pid %d config maybe failed: %d.", pid, ret);
        }
        return 0;
    }

    if (GetProcessManager()->nr[VM_TYPE] + GetProcessManager()->nr[PROCESS_TYPE] >= GetCurrentMaxNrPid()) {
        SMAP_LOGGER_ERROR("nr of grouped vm pid is out of limit.");
        return -EINVAL;
    }

    ProcessAttr *attr = calloc(1, sizeof(ProcessAttr));
    if (!attr) {
        SMAP_LOGGER_ERROR("Alloc memory for grouped process failed.");
        return -ENOMEM;
    }
    ret = InitProcessAttrArrays(attr);
    if (ret) {
        free(attr);
        return ret;
    }
    InitProcessMigrationTargetState(attr);
    (void)NumaBitmapCopy(&attr->numaAttr.numaNodes, nodeBitmap);
    ret = VMPreprocess(pid, attr);
    if (ret) {
        SMAP_LOGGER_ERROR("Preprocess grouped VM process %d failed: %d.", pid, ret);
        FreeProcessAttrArrays(attr);
        free(attr);
        return ret;
    }
    SetGroupedProcessConfig(attr, pid, nodeBitmap, policy);
    /* nr 检查通过不代表一定有空闲槽位（REMOVING/RESERVED 槽位仍被引用）：
     * 抢槽失败时释放 attr、不计数（计数在 PidSlotAdd 内部完成），向上返回错误 */
    ret = PidSlotAdd(GetProcessManager(), attr);
    if (ret < 0) {
        SMAP_LOGGER_ERROR("Add pid slot for grouped pid %d failed: %d.", pid, ret);
        FreeProcessAttrArrays(attr);
        free(attr);
        return ret;
    }
    SMAP_LOGGER_INFO("Add grouped pid %d success, group count %d.", pid, policy->groupCount);
    ret = SyncAllProcessConfig();
    if (ret) {
        SMAP_LOGGER_WARNING("Synchronize grouped pid %d config maybe failed: %d.", pid, ret);
    }
    return 0;
}

/*
 * 暂存 grouped pending 策略的唯一实现，调用方须持该 attr 所属槽位的 attrLock（单测未注册槽位时无并发写者）。
 * 先深拷贝位图，失败时不置 valid（避免带着旧位图应用）；成功后递增 pendingSeq，使 worker
 * ApplyPendingGroupedPolicy 在锁外 apply 期间到达的新 pending 不会被旧快照的清除覆盖。
 */
int StagePendingGroupedPolicyLocked(ProcessAttr *attr, const NumaBitmap *nodeBitmap, const GroupMigrationPolicy *policy)
{
    if (!attr || !nodeBitmap || !policy || !policy->enabled) {
        return -EINVAL;
    }
    if (!NumaBitmapCopy(&attr->pendingGroupPolicy.nodeBitmap, nodeBitmap)) {
        SMAP_LOGGER_ERROR("Copy pending grouped bitmap of pid %d failed.", attr->pid);
        return -ENOMEM;
    }
    attr->pendingGroupPolicy.policy = *policy;
    attr->pendingGroupPolicy.valid = true;
    attr->pendingSeq++;
    return 0;
}

/* apply 结束（成功或放弃）时清除 pending；apply 期间被重新暂存（序号变化）则保留，下一周期再应用 */
static void FinishPendingGroupedPolicyLocked(ProcessAttr *attr, uint32_t pendingSeq)
{
    if (attr->pendingSeq == pendingSeq) {
        attr->pendingGroupPolicy.valid = false;
    } else {
        attr->pendingGroupPolicy.valid = true;
        SMAP_LOGGER_INFO("Pending grouped policy of pid %d restaged during apply, defer to next cycle.", attr->pid);
    }
}

static void GroupedSlotLock(struct PidSlot *slot)
{
    if (slot) {
        EnvMutexLock(&slot->attrLock);
    }
}

static void GroupedSlotUnlock(struct PidSlot *slot)
{
    if (slot) {
        EnvMutexUnlock(&slot->attrLock);
    }
}

/*
 * worker 迁移收尾时应用 grouped pending（调用方持有该 attr 的槽位引用）。
 * 下发 ADD_PID 会改变内核跟踪，必须与移除接口串行：持 g_manageLock（trylock，拿不到则保留
 * pending 下一周期再应用）期间在 attrLock 内确认槽位仍为 INUSE；槽位已被移除（REMOVING）时
 * 放弃 pending，不得把已撤销跟踪的 pid 重新加入内核。
 */
int ApplyPendingGroupedPolicy(ProcessAttr *attr)
{
    if (!attr) {
        return 0;
    }

    /* 按持有的引用定位槽位（不要求 INUSE）；NULL 仅见于 attr 未注册进槽表的单测，无并发写者 */
    struct PidSlot *slot = PidSlotFindHeldByAttr(attr);
    GroupedSlotLock(slot);
    bool hasPending = attr->pendingGroupPolicy.valid;
    GroupedSlotUnlock(slot);
    if (!hasPending) {
        return 0;
    }
    if (!SmapManageTryLock()) {
        SMAP_LOGGER_INFO("Manage interface busy, defer pending grouped policy of pid %d.", attr->pid);
        return 0;
    }

    int ret = 0;
    NumaBitmap nodeBitmap = { 0 };
    GroupedSlotLock(slot);
    if (!attr->pendingGroupPolicy.valid) {
        GroupedSlotUnlock(slot);
        goto out;
    }
    if (slot && EnvAtomicRead(&slot->state) != PID_SLOT_INUSE) {
        attr->pendingGroupPolicy.valid = false;
        GroupedSlotUnlock(slot);
        SMAP_LOGGER_INFO("Grouped pid %d removed, drop its pending policy.", attr->pid);
        goto out;
    }
    /* 锁内取 pending 快照（位图深拷贝），锁外读 numa_maps / 下发 ioctl */
    GroupMigrationPolicy policy = attr->pendingGroupPolicy.policy;
    bool copied = NumaBitmapCopy(&nodeBitmap, &attr->pendingGroupPolicy.nodeBitmap);
    uint32_t pendingSeq = attr->pendingSeq;
    GroupedSlotUnlock(slot);
    if (!copied) {
        SMAP_LOGGER_ERROR("Copy pending grouped bitmap of pid %d failed.", attr->pid);
        ret = -ENOMEM; /* 保留 pending，下一周期重试 */
        goto out;
    }

    uint64_t numaPages[SMAP_MAX_NUMNODES] = { 0 };

    /* Apply is atomic at manager level: initialize the new policy first. */
    ResetGroupedPolicyRuntime(&policy);
    ret = GetPidNumaPagesFromNumaMaps(attr->pid, numaPages, true);
    if (ret) {
        SMAP_LOGGER_ERROR("Get pending grouped pid %d numa pages failed: %d.", attr->pid, ret);
    } else {
        ret = InitGroupedUsedPages(attr->pid, &policy, numaPages);
        if (ret) {
            SMAP_LOGGER_ERROR("Init pending grouped pid %d used pages failed: %d.", attr->pid, ret);
        }
    }
    if (ret) {
        /* 新策略无法生效：放弃该 pending（期间重新暂存的除外） */
        GroupedSlotLock(slot);
        FinishPendingGroupedPolicyLocked(attr, pendingSeq);
        GroupedSlotUnlock(slot);
        goto out;
    }

    struct AccessAddPidPayload payload = {
        .type = NORMAL_SCAN,
        .pid = attr->pid,
        .scanTime = SCAN_TIME_2M,
        .numaNodes = nodeBitmap,
        .pidType = attr->type,
    };
    ret = AccessIoctlAddPid(1, &payload);
    if (ret) {
        SMAP_LOGGER_ERROR("Update pending grouped pid %d tracking failed: %d.", attr->pid, ret);
        goto out;
    }

    /* Tracking has accepted the new node scope; publish policy to manager. */
    GroupedSlotLock(slot);
    SetGroupedProcessConfig(attr, attr->pid, &nodeBitmap, &policy);
    FinishPendingGroupedPolicyLocked(attr, pendingSeq);
    GroupedSlotUnlock(slot);
    SmapManageUnlock();
    NumaBitmapFree(&nodeBitmap);
    int syncRet = SyncAllProcessConfig();
    if (syncRet) {
        SMAP_LOGGER_WARNING("Synchronize pending grouped pid %d config maybe failed: %d.", attr->pid, syncRet);
    }
    SMAP_LOGGER_INFO("Apply pending grouped policy for pid %d success.", attr->pid);
    return 0;
out:
    SmapManageUnlock();
    NumaBitmapFree(&nodeBitmap);
    return ret;
}
