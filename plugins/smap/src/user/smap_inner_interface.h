/*
 * smap is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#ifndef __SMAP_INNER_INTERFACE_H__
#define __SMAP_INNER_INTERFACE_H__

#include "smap_interface.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    DISABLE_MIG_INFO,
    ENABLE_MIG_INFO,
};

enum {
    DISABLE_ADAPT_MEM,
    ENABLE_ADAPT_MEM,
};

struct VmRatio {
    pid_t pid;
    double ratio;
};

struct VmRatioMsg {
    int nrVm;
    struct VmRatio vr[MAX_2M_PROCESSES_CNT];
};

/*
 * 接口生命周期门：每个对外接口进入时 SmapApiEnter()（仅 RUNNING 时成功并计入在途），
 * 返回前 SmapApiExit()；ubturbo_smap_stop 先切 STOPPING 拒绝新调用，再等在途计数归零才销毁。
 * 接口内的长等待循环须周期性检查 SmapStopRequested() 并及时退出，避免 stop 长时间阻塞。
 */
bool SmapApiEnter(void);
void SmapApiExit(void);
bool SmapStopRequested(void);
/* g_manageLock（纳管集合变更锁）的 worker 侧入口：返回 true 表示已加锁，须配对 SmapManageUnlock */
bool SmapManageTryLock(void);
void SmapManageUnlock(void);

int SmapEnableAdaptMem(int flag);
int SmapQueryVmMemRatio(struct VmRatioMsg *vrMsg);
int SmapSetLogLevel(int level);

/* NORMAL_SCAN only. Milliseconds: scan [50, 60000], migrate [500, 300000], scan <= migrate.
 * Directly updates the PID's scanTime/migPeriod under its slot attrLock.
 * Automatic periods, when enabled, take priority. Otherwise enabled fixed file periods override
 * PID values. A fresh PID starts in the fast profile when automatic periods are enabled.
 * Kernel scanning uses these values from the next window; ioctl failures retry next window.
 * Errors: -EPERM, -ENOENT, -EINVAL, -EOPNOTSUPP.
 */
int SmapSetPidPeriod(pid_t pid, uint32_t scanPeriodMs, uint32_t migratePeriodMs);
/* Restore PID values to 200/2000 ms; enabled file configuration still takes priority. */
int SmapResetPidPeriod(pid_t pid);

#ifdef __cplusplus
}
#endif

#endif /* __SMAP_INNER_INTERFACE_H__ */
