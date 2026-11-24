/*
 * smap is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#ifndef __DEVICE_H__
#define __DEVICE_H__

#include <sys/stat.h>

#include "manage.h"

#define TIERING_PATH "/dev/smap_migrate_dev"
#define SYS_NODE_PATH "/sys/devices/system/node"
#define MS_PER_SEC 1000
#define US_PER_MSEC 1000
#define US_PER_SEC (US_PER_MSEC * MS_PER_SEC)
#define SCAN_TIME_4K VM_LIGHT_STABLE_SCAN_CYCLE
#define SCAN_TIME_2M VM_LIGHT_STABLE_SCAN_CYCLE
#define SCAN_PERIOD_UNIT_MS 50
/* 本地节点位掩码为 uint32_t 且需计算 (1U << nrLocalNuma) - 1，支持的本地节点数上限 */
#define SMAP_MAX_LOCAL_NUMA 31

int RefreshRemoteRam(struct ProcessManager *manager);

int InitTrackingDev(struct ProcessManager *manager);

void DeinitTrackingDev(struct ProcessManager *manager);

bool IsNumaCriticalErr(int nid);

int ScanRemoteNids(int *out, int outCap);
void RebuildRemoteNids(void);
bool RemoteNidSetChanged(void);

struct UbFluxMbStatistic;
void GetUbFluxMb(void);
/* 在锁内拷贝最近一次发布的 UB 带宽数据（len 与 flux[] 一致），返回发布时的查询结果码 */
int SnapshotUbFlux(struct ProcessManager *manager, struct UbFluxMbStatistic *out);

int ConfigUbWatch(uint32_t durationMs);

static inline bool IsBwMonitorEnabled(struct ProcessManager *manager)
{
    return (manager->ubBwMonitor.ubBwThreshold > 0);
}

#endif /* __DEVICE_H__ */
