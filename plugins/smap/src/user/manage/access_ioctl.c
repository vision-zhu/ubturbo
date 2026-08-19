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

#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include "smap_user_log.h"

#include "manage.h"
#include "device.h"
#include "access_ioctl.h"
#include "smap_ioctl.h"
#include "thread.h"

int AccessIoctlAddPid(int len, struct AccessAddPidPayload *payload)
{
    if (len <= 0 || len > GetCurrentMaxNrPid()) {
        SMAP_LOGGER_ERROR("AccessIoctlAddPid: invalid len %d.", len);
        return -EINVAL;
    }
    struct AccessAddPidMsg accessMsg = { .count = len };
    accessMsg.payload = malloc(len * sizeof(struct AccessAddPidPayload));
    if (accessMsg.payload == NULL) {
        SMAP_LOGGER_ERROR("AccessIoctlAddPid: malloc failed.");
        return -ENOMEM;
    }
    struct ProcessManager *manager = GetProcessManager();
    for (int i = 0; i < len; i++) {
        accessMsg.payload[i].pid = payload[i].pid;
        accessMsg.payload[i].numaNodes = payload[i].numaNodes;
        if (payload[i].scanTime == 0) {
            SMAP_LOGGER_ERROR("AccessIoctlAddPid: invalid payload[%d].scanTime %u.", i, payload[i].scanTime);
            free(accessMsg.payload);
            return -EINVAL;
        }
        accessMsg.payload[i].scanTime = payload[i].scanTime;
        accessMsg.payload[i].duration = payload[i].duration;
        accessMsg.payload[i].nTimes = payload[i].duration / payload[i].scanTime;
        accessMsg.payload[i].type = payload[i].type;
        accessMsg.payload[i].pidType = payload[i].pidType;
    }
    int ret = ioctl(manager->fds.access, SMAP_ACCESS_ADD_PID, &accessMsg);
    free(accessMsg.payload);
    if (ret < 0) {
        SMAP_LOGGER_ERROR("access ioctl add pids error: %d.", ret);
        ret = -EBADF;
    } else {
        for (int i = 0; i < len; i++) {
            if (payload[i].type == NORMAL_SCAN)
                EventLoopRegisterPid(payload[i].pid);
        }
    }
    return ret;
}

int AccessIoctlRemovePid(int len, struct AccessRemovePidPayload *payload)
{
    if (len <= 0 || len > GetCurrentMaxNrPid()) {
        SMAP_LOGGER_ERROR("AccessIoctlRemovePid: invalid len %d.", len);
        return -EINVAL;
    }
    struct AccessRemovePidMsg accessMsg = { .count = len };
    accessMsg.payload = malloc(len * sizeof(struct AccessRemovePidPayload));
    if (accessMsg.payload == NULL) {
        SMAP_LOGGER_ERROR("AccessIoctlRemovePid: malloc failed.");
        return -ENOMEM;
    }
    struct ProcessManager *manager = GetProcessManager();

    for (int i = 0; i < len; i++) {
        accessMsg.payload[i].pid = payload[i].pid;
    }
    int ret = ioctl(manager->fds.access, SMAP_ACCESS_REMOVE_PID, &accessMsg);
    free(accessMsg.payload);
    if (ret < 0) {
        SMAP_LOGGER_ERROR("access ioctl remove pid error: %d.", ret);
        ret = -EBADF;
    }
    return ret;
}

int AccessIoctlRemoveAllPid(void)
{
    struct ProcessManager *manager = GetProcessManager();

    int ret = ioctl(manager->fds.access, SMAP_ACCESS_REMOVE_ALL_PID, 0);
    if (ret < 0) {
        SMAP_LOGGER_ERROR("access ioctl remove all pid error: %d.", ret);
        ret = -EBADF;
    }
    return ret;
}

int AccessIoctlGetPidPageNum(struct AccessPidPageNumMsg *msg)
{
    struct ProcessManager *manager = GetProcessManager();

    int ret = ioctl(manager->fds.access, SMAP_ACCESS_GET_PID_PAGE_NUM, msg);
    if (ret < 0) {
        SMAP_LOGGER_ERROR("access get pid %d page num error: %d.", msg->pid, ret);
        ret = -EBADF;
    }
    return ret;
}

int AccessIoctlCreateProcfs(struct UserInfo *ui)
{
    struct ProcessManager *manager = GetProcessManager();

    int ret = ioctl(manager->fds.access, SMAP_ACCESS_CREATE_PROCFS, ui);
    if (ret < 0) {
        SMAP_LOGGER_ERROR("access create procfs error: %d\n", -errno);
        ret = -EBADF;
    }
    return ret;
}

void IoctlUpdateUbDmaAvail(uint32_t value)
{
    struct ProcessManager *manager = GetProcessManager();
    uint32_t val = value;

    int ret = ioctl(manager->fds.migrate, SMAP_SET_UB_DMA_AVAIL, &val);
    if (ret < 0) {
        SMAP_LOGGER_ERROR("ioctl update ub dma avail failed: %d, errno %d", ret, errno);
        return;
    }

    SMAP_LOGGER_INFO("ioctl update ub dma avail: %u", val);
}

void IoctlSetScanCpuRange(uint32_t cpuMin, uint32_t cpuMax)
{
    struct ProcessManager *manager = GetProcessManager();
    struct SmapScanCpuRange range = { cpuMin, cpuMax };

    int ret = ioctl(manager->fds.access, SMAP_ACCESS_SET_SCAN_CPU, &range);
    if (ret < 0) {
        SMAP_LOGGER_ERROR("ioctl set scan cpu range failed: %d, errno %d.", ret, errno);
        return;
    }

    SMAP_LOGGER_INFO("ioctl set scan cpu range=%u-%u", cpuMin, cpuMax);
}
