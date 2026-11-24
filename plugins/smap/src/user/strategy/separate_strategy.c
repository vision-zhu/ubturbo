/*
 * smap is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include <stdlib.h>
#include <errno.h>
#include <sys/param.h>
#include <math.h>
#include "securec.h"
#include "smap_user_log.h"
#include "manage/manage.h"
#include "manage/node_matrix.h"
#include "strategy.h"
#include "strategy_config.h"
#include "separate_strategy.h"

#define PRIOR_MAX_VALUE 63

static void FreeMlist(struct MigList *mlist)
{
    size_t count = MigListMatrixCount();
    for (size_t i = 0; i < count; i++) {
        free(mlist[i].addr);
        mlist[i].addr = NULL;
        mlist[i].nr = 0;
    }
}

void FindThreshold(const SelectionMode mode, uint64_t nrMig, const uint32_t *buckets, int *thresholdFreq,
                   uint32_t *takeAtThreshold)
{
    (*thresholdFreq) = mode == SELECT_TOP_K ? 0 : FREQ_BUCKETS_SIZE;
    (*takeAtThreshold) = 0;
    size_t countSoFar = 0;

    if (mode == SELECT_TOP_K) {
        for (int i = FREQ_BUCKETS_SIZE - 1; i >= 0; --i) {
            if (buckets[i] == 0) {
                continue;
            }
            if (countSoFar + buckets[i] >= nrMig) {
                (*thresholdFreq) = i;
                (*takeAtThreshold) = nrMig - countSoFar;
                break;
            }
            countSoFar += buckets[i];
        }
    } else {
        for (int i = 0; i < FREQ_BUCKETS_SIZE; ++i) {
            if (buckets[i] == 0) {
                continue;
            }
            if (countSoFar + buckets[i] >= nrMig) {
                (*thresholdFreq) = i;
                (*takeAtThreshold) = nrMig - countSoFar;
                break;
            }
            countSoFar += buckets[i];
        }
    }
}

static size_t CollectPagesVm(const SelectionMode mode, uint64_t actcLen, ActcData *currentData,
                             struct MigList *currMlist, uint64_t nrMig, int thresholdFreq, uint32_t takeAtThreshold,
                             uint32_t *selectedBuckets)
{
    int nrLocalNuma = GetNrLocalNuma();

    /* Pre-pass: 统计边界频次页面的 prior 分布 */
    uint64_t priorCounts[PRIOR_MAX_VALUE + 1] = { 0 };
    for (size_t i = 0; i < actcLen; ++i) {
        if (currentData[i].isSelected) {
            continue;
        }
        if (currentData[i].freq != (actc_t)thresholdFreq) {
            continue;
        }
        priorCounts[currentData[i].prior]++;
    }

    /* 计算每个 prior 级别需取多少页 */
    uint32_t remaining = takeAtThreshold;
    uint64_t takeFrom[PRIOR_MAX_VALUE + 1] = { 0 };
    for (int p = PRIOR_MAX_VALUE; p >= 0 && remaining > 0; --p) {
        uint64_t take = MIN(priorCounts[p], remaining);
        takeFrom[p] = take;
        remaining -= take;
    }

    /* Unified pass: 一次遍历收集全部页面，天然升序 */
    size_t collectedCount = 0;
    for (size_t i = 0; i < actcLen && collectedCount < nrMig; ++i) {
        if (currentData[i].isSelected) {
            continue;
        }

        int freq = currentData[i].freq;
        bool shouldTake = (mode == SELECT_TOP_K) ? (freq > thresholdFreq) : (freq < thresholdFreq);

        if (!shouldTake && freq == thresholdFreq && takeFrom[currentData[i].prior] > 0) {
            shouldTake = true;
            takeFrom[currentData[i].prior]--;
        }
        if (!shouldTake) {
            continue;
        }

        if (currMlist->from < nrLocalNuma && currentData[i].isWhiteListPage) {
            continue;
        }

        currMlist->addr[collectedCount++] = i;
        selectedBuckets[MIN(freq, FREQ_BUCKETS_SIZE - 1)]++;
        currentData[i].isSelected = 1;
    }

    return collectedCount;
}

static size_t CollectPages4K(const SelectionMode mode, uint64_t actcLen, ActcData *currentData,
                             struct MigList *currMlist, uint64_t nrMig, int thresholdFreq, uint32_t takeAtThreshold,
                             uint32_t *selectedBuckets)
{
    int nrLocalNuma = GetNrLocalNuma();
    uint32_t tmp = takeAtThreshold;
    size_t collectedCount = 0;
    for (size_t i = 0; i < actcLen && collectedCount < nrMig; ++i) {
        if (currentData[i].isSelected) {
            continue;
        }
        int freq = currentData[i].freq;
        bool shouldTake = false;

        if (mode == SELECT_TOP_K) {
            shouldTake = (freq > thresholdFreq) || (freq == thresholdFreq && tmp > 0);
        } else {
            shouldTake = (freq < thresholdFreq) || (freq == thresholdFreq && tmp > 0);
        }

        if (shouldTake) {
            if (currMlist->from < nrLocalNuma && currentData[i].isWhiteListPage) {
                continue;
            }
            currMlist->addr[collectedCount++] = i;
            int bucketIdx = MIN(freq, FREQ_BUCKETS_SIZE - 1);
            selectedBuckets[bucketIdx]++;
            currentData[i].isSelected = 1;
            if (freq == thresholdFreq) {
                tmp--;
            }
        }
    }

    return collectedCount;
}

static int BuildSelectKMlistAddr(ProcessAttr *process, struct MigList *mlist,
                                 const uint64_t numaOffset[SMAP_MAX_NUMNODES], int from, int to, SelectionMode mode)
{
    int nrLocalNuma = GetNrLocalNuma();
    uint64_t offset = numaOffset[from];
    uint64_t n = process->scanAttr.actcLen[from];
    ActcData *currentData = process->scanAttr.actcData[from];
    struct MigList *currentMig = MigListCell(mlist, from, to);
    ActCount *actCount = &process->scanAttr.actCount[from];
    uint32_t *selectedBuckets = process->scanAttr.selectedBuckets[from];
    uint32_t remainBuckets[FREQ_BUCKETS_SIZE];

    if (offset >= n) {
        currentMig->nr = 0;
        return 0;
    }

    uint64_t remainLen = n - offset;
    uint64_t nrMig = MigListCell(mlist, from, to)->nr;
    nrMig = MIN(nrMig, remainLen);
    currentMig->nr = nrMig;
    if (nrMig == 0) {
        return 0;
    }
    if (!currentData) {
        currentMig->nr = 0;
        currentMig->addr = NULL;
        return -EINVAL;
    }
    currentMig->addr = calloc(nrMig, sizeof(uint64_t));
    if (!currentMig->addr) {
        return -ENOMEM;
    }

    /* 计算剩余范围的频次桶 = freqBuckets - selectedBuckets */
    /* freqBuckets 在 CalcActcStats 中已排除白名单页面，迁出场景正确 */
    for (int f = 0; f < FREQ_BUCKETS_SIZE; f++) {
        remainBuckets[f] =
            actCount->freqBuckets[f] > selectedBuckets[f] ? actCount->freqBuckets[f] - selectedBuckets[f] : 0;
    }

    int thresholdFreq;
    uint32_t takeAtThreshold;
    FindThreshold(mode, nrMig, remainBuckets, &thresholdFreq, &takeAtThreshold);
    size_t collected;
    if (process->type == VM_TYPE) {
        collected =
            CollectPagesVm(mode, n, currentData, currentMig, nrMig, thresholdFreq, takeAtThreshold, selectedBuckets);
    } else {
        collected =
            CollectPages4K(mode, n, currentData, currentMig, nrMig, thresholdFreq, takeAtThreshold, selectedBuckets);
    }

    currentMig->nr = collected;
    return 0;
}

static int BuildPairMlist(ProcessAttr *process, struct MigList *mlist, uint64_t numaOffset[SMAP_MAX_NUMNODES], int from,
                          int to, uint32_t nrMigrate, SelectionMode mode)
{
    if (nrMigrate == 0) {
        return 0;
    }

    MigListCell(mlist, from, to)->nr = nrMigrate;
    int ret = BuildSelectKMlistAddr(process, mlist, numaOffset, from, to, mode);
    if (ret) {
        return ret;
    }
    numaOffset[from] += MigListCell(mlist, from, to)->nr;
    return 0;
}

int PairMigrationStrategy(ProcessAttr *process, struct MigList *mlist)
{
    int nrLocalNuma = GetNrLocalNuma();
    if (!process || nrLocalNuma <= 0 || nrLocalNuma > SMAP_MAX_NUMNODES) {
        return -EINVAL;
    }

    uint64_t numaOffset[SMAP_MAX_NUMNODES] = { 0 };
    StrategyAttribute *strategy = &process->strategyAttr;
    for (int localNid = 0; localNid < nrLocalNuma; localNid++) {
        int remoteNid;
        for_each_remote_nid(remoteNid)
        {
            uint32_t localToRemote = NM_AT(strategy->nrMigratePages, localNid, remoteNid);
            uint32_t remoteToLocal = NM_AT(strategy->nrMigrateBackPages, localNid, remoteNid);
            if (IsNodeForbidden(remoteNid)) {
                if (localToRemote > 0 || remoteToLocal > 0) {
                    SMAP_LOGGER_INFO("Pid %d pair %d-%d skips migration to and from disabled remote.", process->pid,
                                     localNid, remoteNid);
                }
                continue;
            }
            /*
             * 该远端可能在本轮 plan 之后被 remove 出管理范围：numaNodes 位图与目标配置已清，
             * 矩阵里的残留计划不能再生效，否则页面会迁到不再跟踪的节点上且无法迁回。
             */
            if (NotInAttrL2(process, remoteNid)) {
                if (localToRemote > 0 || remoteToLocal > 0) {
                    SMAP_LOGGER_INFO("Pid %d pair %d-%d skips migration to unmanaged remote.", process->pid, localNid,
                                     remoteNid);
                }
                continue;
            }
            uint32_t swapPages = MIN(localToRemote, remoteToLocal);
            uint32_t demotePages = localToRemote - swapPages;
            uint32_t promotePages = remoteToLocal - swapPages;
            if (strategy->ubBwRestrict[remoteNid] == UB_BW_SWAP_STOP) {
                SMAP_LOGGER_INFO("Pid %d pair %d-%d UB bandwidth restricts swap, swapPages %u -> 0, "
                                 "demotePages %u, promotePages %u.",
                                 process->pid, localNid, remoteNid, swapPages, demotePages, promotePages);
                swapPages = 0;
            }

            int ret = BuildPairMlist(process, mlist, numaOffset, localNid, remoteNid, demotePages + swapPages,
                                     SELECT_BOTTOM_K);
            if (!ret) {
                ret = BuildPairMlist(process, mlist, numaOffset, remoteNid, localNid, promotePages + swapPages,
                                     SELECT_TOP_K);
            }
            if (ret) {
                SMAP_LOGGER_ERROR("Build pid %d pair %d-%d migration list failed: %d.", process->pid, localNid,
                                  remoteNid, ret);
                FreeMlist(mlist);
                return ret;
            }
        }
    }
    return 0;
}

