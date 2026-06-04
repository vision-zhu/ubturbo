/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 *
 * smap is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include <stdint.h>
#include <string.h>
#include <sys/param.h>
#include "securec.h"
#include "smap_user_log.h"
#include "manage/manage.h"
#include "separate_strategy.h"
#include "grouped_strategy.h"
#include "strategy.h"

static uint64_t GetRemoteAvailablePages(int remoteNid)
{
    struct ProcessManager *manager = GetProcessManager();
    if (!manager || !IsRemoteNidValid(remoteNid)) {
        SMAP_LOGGER_ERROR("Invalid manager or remote nid %d.", remoteNid);
        return 0;
    }

    struct RemoteNumaInfo *numaInfo = &manager->remoteNumaInfo;
    int column = remoteNid - GetNrLocalNuma();
    if (column < 0 || column >= REMOTE_NUMA_NUM) {
        return 0;
    }

    EnvMutexLock(&numaInfo->lock);
    uint64_t size = numaInfo->usedInfo[column].size;
    uint64_t used = numaInfo->usedInfo[column].used;
    EnvMutexUnlock(&numaInfo->lock);

    uint64_t available = (size > used) ? (size - used) : 0;
    SMAP_LOGGER_DEBUG("Remote NUMA %d available pages: %llu (size=%llu, used=%llu).",
                      remoteNid, available, size, used);
    return available;
}

static void LimitMigratePagesByRemoteAvailable(ProcessAttr *process)
{
    int nrLocalNuma = GetNrLocalNuma();

    for (int l1Node = 0; l1Node < nrLocalNuma; l1Node++) {
        if (NotInAttrL1(process, l1Node)) {
            continue;
        }

        for (int l2Node = nrLocalNuma; l2Node < nrLocalNuma + REMOTE_NUMA_NUM; l2Node++) {
            if (NotInAttrL2(process, l2Node)) {
                continue;
            }

            uint64_t remoteAvailable = GetRemoteAvailablePages(l2Node);
            uint32_t *nrMigOut = &process->strategyAttr.nrMigratePages[l1Node][l2Node];
            uint32_t *nrMigIn = &process->strategyAttr.nrMigratePages[l2Node][l1Node];

            if (*nrMigOut > 0 && *nrMigOut > remoteAvailable) {
                SMAP_LOGGER_INFO("Pid %d migrate out %u pages from NUMA%d to NUMA%d limited to %llu.",
                                 process->pid, *nrMigOut, l1Node, l2Node, remoteAvailable);
                *nrMigOut = (uint32_t)remoteAvailable;
            }

            if (*nrMigIn > 0) {
                SMAP_LOGGER_DEBUG("Pid %d migrate in %u pages from NUMA%d to NUMA%d, no need to limit.",
                                  process->pid, *nrMigIn, l2Node, l1Node);
            }
        }
    }
}

uint64_t GetNrFreePagesByNode(int nid)
{
    int ret;
    uint64_t nr = 0;
    FILE *file;
    char filename[BUFFER_SIZE];
    char line[BUFFER_SIZE] = { 0 };
    char fmt[BUFFER_SIZE] = "/sys/devices/system/node/node%d/meminfo";
    char seps[] = " ";
    char *token = NULL;
    int tmpCnt = FREE_BYTES_INDEX;

    ret = snprintf_s(filename, BUFFER_SIZE, BUFFER_SIZE, fmt, nid);
    if (ret == -1) {
        SMAP_LOGGER_ERROR("snprintf_s failed.");
        return 0;
    }
    file = fopen(filename, "r");
    if (!file) {
        SMAP_LOGGER_ERROR("fopen %s failed, errno: %d.", filename, errno);
        return 0;
    }

    while (fgets(line, sizeof(line), file) != NULL) {
        if (strstr(line, "MemFree") == NULL) {
            continue;
        }
        token = strtok(line, seps);
        while (token && tmpCnt--) {
            token = strtok(NULL, seps);
        }
        if (sscanf_s(token, "%lu", &nr) <= 0) {
            SMAP_LOGGER_ERROR("sscanf_s failed.");
            nr = 0;
        }
        break;
    }
    nr >>= KB_TO_PAGE_SHIFT;

    ret = fclose(file);
    if (ret) {
        SMAP_LOGGER_ERROR("close %s failed, errno: %d.", filename, errno);
    }
    return nr;
}

uint64_t GetNrFreeHugePagesByNode(int nid)
{
    int ret;
    uint64_t nr = 0;
    FILE *file;
    char filename[BUFFER_SIZE];
    char line[BUFFER_SIZE];
    char fmt[BUFFER_SIZE] = "/sys/devices/system/node/node%d/hugepages/hugepages-2048kB/free_hugepages";

    ret = snprintf_s(filename, BUFFER_SIZE, BUFFER_SIZE, fmt, nid);
    if (ret == -1) {
        SMAP_LOGGER_ERROR("snprintf_s failed.");
        return 0;
    }
    file = fopen(filename, "r");
    if (!file) {
        SMAP_LOGGER_ERROR("fopen %s failed, errno: %d.", filename, errno);
        return 0;
    }

    if (fgets(line, sizeof(line), file)) {
        if (sscanf_s(line, "%llu", &nr) <= 0) {
            SMAP_LOGGER_ERROR("sscanf_s failed.");
            nr = 0;
        }
    }

    ret = fclose(file);
    if (ret) {
        SMAP_LOGGER_ERROR("close %s failed, errno: %d.", filename, errno);
    }
    return nr;
}

int RunStrategy(ProcessAttr *process, struct MigList mlist[MAX_NODES][MAX_NODES], size_t mlistSize)
{
    if (mlistSize < MAX_NODES) {
        SMAP_LOGGER_ERROR("Miglist size is small, size:%zu.", mlistSize);
        return -EINVAL;
    }
    if (!process || CheckActcDataValid(process)) {
        SMAP_LOGGER_ERROR("Invalid pid %d actc.", process ? process->pid : -1);
        return -EINVAL;
    }
    if (GetRunMode() == WATERLINE_MODE && !process->groupPolicy.enabled) {
        LimitMigratePagesByRemoteAvailable(process);
    }
    if (IsHugeMode()) {
        if (process->groupPolicy.enabled) {
            return GroupedMigrationStrategy(process, mlist);
        }
        if (IsMultiNumaVm(process)) {
            return SeparateStrategyMultiNumaVm(process, mlist);
        } else {
            return SeparateStrategy(process, mlist);
        }
    } else {
        return SeparateStrategy4K(process, mlist);
    }
}
