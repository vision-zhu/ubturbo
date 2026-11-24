/*
 * smap is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include "smap_env.h"
#include "smap_inner_interface.h"

#include "manage/manage.h"
#include "manage/node_matrix.h"
#include "advanced-strategy/scene.h"
#include "smap_user_log.h"
#include "smap_log_core.h"
#include "strategy/strategy_config.h"

int SmapEnableAdaptMem(int flag)
{
    if (flag == ENABLE_ADAPT_MEM) {
        SetAdaptMem(true);
        SMAP_LOGGER_INFO("Enable adapt mem ratio succeed!");
        return 0;
    } else if (flag == DISABLE_ADAPT_MEM) {
        SetAdaptMem(false);
        SMAP_LOGGER_INFO("Disable adapt mem ratio succeed!");
        return 0;
    } else {
        SMAP_LOGGER_ERROR("The arg: %d is invalid for adapt mem ratio!", flag);
        return -EINVAL;
    }
}

static int QueryVmMemRatio(struct VmRatioMsg *vrMsg)
{
    if (!vrMsg) {
        SMAP_LOGGER_ERROR("Query failed as msg is null.");
        return -EINVAL;
    }
    vrMsg->nrVm = 0;
    struct ProcessManager *manager = GetProcessManager();
    int nrLocalNuma = GetNrLocalNuma();
    struct PidSlot *all[MAX_PID_SLOTS];
    size_t n = PidSlotCollectRefs(manager, all, MAX_PID_SLOTS);
    for (size_t k = 0; k < n; k++) {
        ProcessAttr *current = all[k]->attr;
        /* 位图与比例矩阵由扫描/迁移线程持 attrLock 更新，读取同样持锁保证一致 */
        EnvMutexLock(&all[k]->attrLock);
        /* 先过滤非 VM：普通进程/跟踪进程首轮回读前 L1 可能为空，不得影响 VM 查询 */
        if (current->type != VM_TYPE) {
            EnvMutexUnlock(&all[k]->attrLock);
            continue;
        }
        int l1Node = GetAttrL1(current);
        int l2Node = GetAttrL2(current);
        if (l1Node < 0) {
            /* 单个 VM 的 L1 尚未确定（首轮回读前）：跳过该 VM，不让整个查询失败 */
            EnvMutexUnlock(&all[k]->attrLock);
            SMAP_LOGGER_WARNING("SmapQueryVmMemRatio pid %d L1 is invalid, skip.", current->pid);
            continue;
        }
        /* 兜底：VM 仅 2M 模式准入（上限 MAX_2M_PROCESSES_CNT），但恢复路径不经过准入校验，
         * 持久化残留可突破该不变式，此处对齐数组容量防止越界写 */
        if (vrMsg->nrVm >= MAX_2M_PROCESSES_CNT) {
            EnvMutexUnlock(&all[k]->attrLock);
            SMAP_LOGGER_ERROR("SmapQueryVmMemRatio vm count exceeds %d, truncate.", MAX_2M_PROCESSES_CNT);
            break;
        }
        vrMsg->vr[vrMsg->nrVm].pid = current->pid;
        vrMsg->vr[vrMsg->nrVm].ratio = HUNDRED;
        if (l2Node >= nrLocalNuma) {
            vrMsg->vr[vrMsg->nrVm].ratio -= NM_AT(current->strategyAttr.l3RemoteMemRatio, l1Node, l2Node);
        }
        vrMsg->nrVm++;
        EnvMutexUnlock(&all[k]->attrLock);
    }
    PidSlotReleaseRefs(all, n);
    return 0;
}

int SmapQueryVmMemRatio(struct VmRatioMsg *vrMsg)
{
    if (!SmapApiEnter()) {
        SMAP_LOGGER_ERROR("Smap already stopped, SmapQueryVmMemRatio failed.");
        return -EPERM;
    }
    int ret = QueryVmMemRatio(vrMsg);
    SmapApiExit();
    return ret;
}

int SmapSetLogLevel(int level)
{
    if (level < SMAP_LOG_CORE_TRACE || level >= SMAP_LOG_CORE_BUTT) {
        SMAP_LOGGER_ERROR("Invalid log level %d, valid range: %d-%d.", level, SMAP_LOG_CORE_TRACE,
                          SMAP_LOG_CORE_CRITICAL);
        return -EINVAL;
    }
    SmapLogCoreSetMinLogLevel(level);
    SMAP_LOGGER_INFO("Log level set to %s.", SmapLogCoreGetMinLogLevel() == level ? "success" : "failed");
    return 0;
}

static int SetPidPeriodInner(pid_t pid, uint32_t scanPeriodMs, uint32_t migratePeriodMs)
{
    if (pid <= 0) {
        return -EINVAL;
    }
    struct PidSlot *slot = PidSlotGetRef(pid);
    if (!slot) {
        return -ENOENT;
    }
    int ret = 0;
    EnvMutexLock(&slot->attrLock);
    ProcessAttr *attr = slot->attr;
    if (EnvAtomicRead(&slot->state) != PID_SLOT_INUSE || !attr) {
        ret = -ENOENT;
    } else if (attr->scanType != NORMAL_SCAN) {
        ret = -EOPNOTSUPP;
    } else {
        attr->scanTime = scanPeriodMs;
        attr->migPeriod = migratePeriodMs;
    }
    EnvMutexUnlock(&slot->attrLock);
    PidSlotReleaseRefs(&slot, 1);
    return ret;
}

static int SetPidPeriod(pid_t pid, uint32_t scanPeriodMs, uint32_t migratePeriodMs)
{
    if (!SmapApiEnter()) {
        return -EPERM;
    }
    int ret = SetPidPeriodInner(pid, scanPeriodMs, migratePeriodMs);
    SmapApiExit();
    return ret;
}

int SmapSetPidPeriod(pid_t pid, uint32_t scanPeriodMs, uint32_t migratePeriodMs)
{
    if (scanPeriodMs < MIN_SCAN_PERIOD || scanPeriodMs > MAX_SCAN_PERIOD || migratePeriodMs < MIN_MIGRATE_PERIOD ||
        migratePeriodMs > MAX_MIGRATE_PERIOD || scanPeriodMs > migratePeriodMs) {
        return -EINVAL;
    }
    return SetPidPeriod(pid, scanPeriodMs, migratePeriodMs);
}

int SmapResetPidPeriod(pid_t pid)
{
    return SetPidPeriod(pid, DEFAULT_SCAN_PERIOD, DEFAULT_MIGRATE_PERIOD);
}
