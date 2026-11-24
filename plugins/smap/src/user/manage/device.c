/*
 * smap is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#include <dirent.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <time.h>
#include "securec.h"
#include "smap_env.h"
#include "smap_user_log.h"
#include "manage.h"
#include "access_ioctl.h"
#include "smap_ioctl.h"
#include "device.h"

#define DISABLE_TRACKING_RETRY_DELAY_US 50000

#define SYS_NODE_CRITICAL_ERR_LEN 60

typedef enum {
    PGSIZE_FOUR_KB = 0,
    PGSIZE_TWO_MB = 9,
} PageSize;

bool IsNumaCriticalErr(int nid)
{
    char path[SYS_NODE_CRITICAL_ERR_LEN] = { 0 };

    int ret = snprintf_s(path, sizeof(path), sizeof(path) - 1, "%s/node%d/critical_err", SYS_NODE_PATH, nid);
    if (ret == -1) {
        SMAP_LOGGER_ERROR("Failed to build node%d critical_err path.", nid);
        return false;
    }

    FILE *file = fopen(path, "r");
    if (!file) {
        SMAP_LOGGER_DEBUG("Not find node%d critical_err file, assume available.", nid);
        return false;
    }

    int c = fgetc(file);
    if (fclose(file) != 0) {
        SMAP_LOGGER_WARNING("Failed to close node%d critical_err file: %d.", nid, errno);
    }
    if (c == EOF) {
        SMAP_LOGGER_DEBUG("Node%d critical_err file empty, assume available.", nid);
        return false;
    }

    return c == '1';
}

int RefreshRemoteRam(struct ProcessManager *manager)
{
    if (manager->fds.access < 0) {
        SMAP_LOGGER_ERROR("access device fd is invalid: %d", manager->fds.access);
        return -EINVAL;
    }
    if (ioctl(manager->fds.access, SMAP_ACCESS_REFRESH_REMOTE_RAM, 0) < 0) {
        int err = errno; /* 日志可能改写 errno，先保存 */
        SMAP_LOGGER_ERROR("refresh_remote_ram ioctl failed: %s.", strerror(err));
        return -err;
    }
    return 0;
}

static int ConfigTrackingDev(struct ProcessManager *manager, uint32_t pageSize)
{
    int arg;

    arg = pageSize == PAGESIZE_2M ? PGSIZE_TWO_MB : PGSIZE_FOUR_KB;
    if (ioctl(manager->fds.access, SMAP_IOCTL_PAGE_SIZE_SET_CMD, arg) < 0) {
        int err = errno; /* 日志可能改写 errno，先保存 */
        SMAP_LOGGER_ERROR("ioctl set tracking page size failed: %s.", strerror(err));
        return -err;
    }
    return 0;
}

static bool IsLocalNuma(unsigned long nid)
{
#define SYS_NODE_REMOTE_LEN 50
    char path[SYS_NODE_REMOTE_LEN] = { 0 };
#undef SYS_NODE_REMOTE_LEN

    if (nid >= SMAP_MAX_NUMNODES) {
        return false;
    }

    int ret = snprintf_s(path, sizeof(path), sizeof(path) - 1, "%s/node%lu/remote", SYS_NODE_PATH, nid);
    if (ret == -1) {
        SMAP_LOGGER_ERROR("Failed to build node%lu remote path.", nid);
        return false;
    }

    FILE *file = fopen(path, "r");
    if (!file) {
        SMAP_LOGGER_ERROR("Failed to open node%lu remote file.", nid);
        return false;
    }

    int c = fgetc(file);
    if (fclose(file) != 0) {
        SMAP_LOGGER_WARNING("Failed to close node%lu remote file: %d.", nid, errno);
    }
    if (c == EOF) {
        SMAP_LOGGER_ERROR("Failed to read node%lu remote file.", nid);
        return false;
    }

    return c == '0';
}

static int GetNrLocalNumaFromKernel(struct ProcessManager *manager)
{
    if (manager->fds.access < 0) {
        SMAP_LOGGER_ERROR("access device fd is invalid: %d", manager->fds.access);
        return -EINVAL;
    }

    int nrLocalNuma = 0;
    int ret = ioctl(manager->fds.access, SMAP_ACCESS_GET_NR_LOCAL_NUMA, &nrLocalNuma);
    if (ret < 0) {
        int err = errno; /* 日志可能改写 errno，先保存 */
        SMAP_LOGGER_ERROR("failed to get nr_local_numa from kernel: %d, errno: %d", ret, err);
        return -err;
    }
    if (nrLocalNuma <= 0 || nrLocalNuma > SMAP_MAX_NUMNODES) {
        SMAP_LOGGER_ERROR("get nr_local_numa invalid :%d", nrLocalNuma);
        return -EINVAL;
    }
    /* 本地节点集合在用户态以 uint32_t 位掩码表达（managedLocalMask、Pair 容量掩码、
     * (1U << nrLocalNuma) - 1 等），超过 SMAP_MAX_LOCAL_NUMA 时移位溢出/混叠，
     * Pair 规划与纳管会静默出错，因此显式拒绝 */
    if (nrLocalNuma > SMAP_MAX_LOCAL_NUMA) {
        SMAP_LOGGER_ERROR("nr_local_numa %d exceeds supported limit %d.", nrLocalNuma, SMAP_MAX_LOCAL_NUMA);
        return -EOPNOTSUPP;
    }
    manager->nrLocalNuma = nrLocalNuma;
    SMAP_LOGGER_INFO("get nr_local_numa %d from kernel successfully", manager->nrLocalNuma);
    return 0;
}

int ConfigureTrackingDevices(struct ProcessManager *manager)
{
    int ret = GetNrLocalNumaFromKernel(manager);
    if (ret) {
        SMAP_LOGGER_ERROR("Unable to get nr_local_numa from kernel: %d.", ret);
        return ret;
    }

    ret = ConfigTrackingDev(manager, manager->tracking.pageSize);
    if (ret) {
        SMAP_LOGGER_ERROR("Error when config tracking-node devices.");
        return ret;
    }
    return 0;
}

static int OpenAndFlockFd(int *fd, const char *device)
{
    int tmp = open(device, O_RDWR);
    if (tmp < 0) {
        SMAP_LOGGER_ERROR("cannot find %s, skipped.", device);
        return -ENODEV;
    }

    if (flock(tmp, LOCK_EX | LOCK_NB) == -1) {
        if (errno == EWOULDBLOCK) {
            SMAP_LOGGER_ERROR("Device %s is already in use by another process.", device);
        } else {
            SMAP_LOGGER_ERROR("Flock %s failed.", device);
        }
        close(tmp);
        return -EACCES;
    }

    *fd = tmp;
    return 0;
}

int InitTrackingDev(struct ProcessManager *manager)
{
    int ret = 0;
    int fd;

    ret = OpenAndFlockFd(&fd, TIERING_PATH);
    if (ret) {
        return ret;
    }
    manager->fds.migrate = fd;
    fd = open(ACCESS_DEVICE, O_RDWR);
    if (fd < 0) {
        SMAP_LOGGER_ERROR("cannot find access dev under /dev.");
        return -ENODEV;
    }
    manager->fds.access = fd;

    /* cap 获取与设备无关（procfs 通道），但保持在设备初始化路径统一申请；
     * 解析失败由 GetNodeCapFromStatus 回退 SMAP_NODE_CAP_LIMIT，不拒启 */
    uint32_t nodeCap = 0;
    ret = GetNodeCapFromStatus(&nodeCap);
    if (ret) {
        SMAP_LOGGER_WARNING("parse node cap from status failed: %d, use fallback cap %u.", ret, nodeCap);
    }
    SetNumaCap(nodeCap);

    ret = ConfigureTrackingDevices(manager);
    if (ret) {
        SMAP_LOGGER_ERROR("config tracking devices failed : %d.", ret);
    }
    return ret;
}

void DeinitTrackingDev(struct ProcessManager *manager)
{
    if (manager->fds.migrate >= 0) {
        close(manager->fds.migrate);
        manager->fds.migrate = DEFAULT_FD;
    }
    if (manager->fds.access >= 0) {
        close(manager->fds.access);
        manager->fds.access = DEFAULT_FD;
    }
}

/* 保护 manager->ubBwMonitor.currentFluxMb/currentFluxRet 的发布与快照读取 */
static pthread_mutex_t g_ubFluxLock = PTHREAD_MUTEX_INITIALIZER;

void GetUbFluxMb(void)
{
    struct ProcessManager *manager = GetProcessManager();
    int ret = -ENODEV;

    // ubBwThreshold == 0 表示不开启迁移限制：跳过带宽查询与流量统计
    if (!IsBwMonitorEnabled(manager)) {
        return;
    }

    /* ioctl 先写入私有缓冲区：内核 copy_to_user 直接覆写共享结构会让并发读者
     * 看到新 len 配旧 flux（或相反）；成功后再在锁内整体发布 */
    struct UbFluxMbStatistic *result = calloc(1, sizeof(*result));
    if (result == NULL) {
        SMAP_LOGGER_ERROR("Alloc UB flux buffer failed.");
        ret = -ENOMEM;
    } else {
        ret = ioctl(manager->fds.access, SMAP_IOCTL_UB_WATCH_CMD, result);
        if (ret == 0 && (result->len < 0 || result->len > SMAP_MAX_NUMNODES)) {
            SMAP_LOGGER_ERROR("UB flux len %d out of range.", result->len);
            ret = -EINVAL;
        }
    }

    (void)pthread_mutex_lock(&g_ubFluxLock);
    manager->ubBwMonitor.currentFluxRet = ret;
    if (ret == 0) {
        manager->ubBwMonitor.currentFluxMb = *result;
    }
    (void)pthread_mutex_unlock(&g_ubFluxLock);

    if (ret) {
        SMAP_LOGGER_ERROR("ioctl SMAP_IOCTL_UB_WATCH_CMD failed on scan device.");
        free(result);
        return;
    }

    for (int i = 0; i < result->len; i++) {
        uint32_t totalBw = result->flux[i].readMb + result->flux[i].writeMb;
        SMAP_LOGGER_INFO("UB business flux: numaId: %d, readMb: %uMB/s, writeMb: %uMB/s, total: %uMB/s",
                         result->flux[i].numaId, result->flux[i].readMb, result->flux[i].writeMb, totalBw);
    }
    free(result);
}

int SnapshotUbFlux(struct ProcessManager *manager, struct UbFluxMbStatistic *out)
{
    if (manager == NULL || out == NULL) {
        return -EINVAL;
    }
    (void)pthread_mutex_lock(&g_ubFluxLock);
    int ret = manager->ubBwMonitor.currentFluxRet;
    out->len = 0;
    if (ret == 0) {
        int len = manager->ubBwMonitor.currentFluxMb.len;
        if (len < 0) {
            len = 0;
        } else if (len > SMAP_MAX_NUMNODES) {
            len = SMAP_MAX_NUMNODES;
        }
        out->len = len;
        for (int i = 0; i < len; i++) {
            out->flux[i] = manager->ubBwMonitor.currentFluxMb.flux[i];
        }
    }
    (void)pthread_mutex_unlock(&g_ubFluxLock);
    return ret;
}

/* 扫描 sysfs 收集远端在线 nid，升序写入 out，返回数量 */
int ScanRemoteNids(int *out, int outCap)
{
    DIR *dir = opendir("/sys/devices/system/node");
    if (!dir) {
        SMAP_LOGGER_ERROR("opendir /sys/devices/system/node failed: %d", errno);
        return -errno;
    }
    int n = 0;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL && n < outCap) {
        int nid;
        if (sscanf_s(ent->d_name, "node%d", &nid) != 1)
            continue;
        if (nid < 0 || nid < GetNrLocalNuma())
            continue;
        if (nid >= SMAP_MAX_NUMNODES) {
            SMAP_LOGGER_ERROR("node %d exceeds SMAP_MAX_NUMNODES %d, skip.", nid, SMAP_MAX_NUMNODES);
            continue;
        }
        out[n++] = nid;
    }
    closedir(dir);
    /* 升序保证结果确定、便于 diff */
    for (int i = 0; i < n - 1; i++) {
        for (int j = 0; j < n - 1 - i; j++) {
            if (out[j] > out[j + 1]) {
                int t = out[j];
                out[j] = out[j + 1];
                out[j + 1] = t;
            }
        }
    }
    return n;
}

/* g_remoteNids 的 seqlock 序号：奇数表示 RebuildRemoteNids 正在更新（协议见 numa_nodes.h） */
unsigned int g_remoteNidsSeq = 0;

/* 重建 g_remoteNids 列表（唯一写者：初始化路径与 daemon 线程，二者不并发） */
void RebuildRemoteNids(void)
{
    int tmp[SMAP_MAX_NUMNODES];
    int n = ScanRemoteNids(tmp, SMAP_MAX_NUMNODES);
    if (n < 0) {
        SMAP_LOGGER_WARNING("ScanRemoteNids failed, keep current remote list");
        return;
    }
    /* seqlock 写端：序号置奇 → release 栅栏 → 原地更新内容与计数 → release 置偶。
     * 读者（SnapshotRemoteNids）在序号为奇或前后不一致时重试，因此只会拿到
     * 完整的旧列表或完整的新列表，不会看到遍历中途被覆写的混合内容 */
    unsigned int seq = __atomic_load_n(&g_remoteNidsSeq, __ATOMIC_RELAXED);
    __atomic_store_n(&g_remoteNidsSeq, seq + 1U, __ATOMIC_RELAXED);
    __atomic_thread_fence(__ATOMIC_RELEASE);
    for (int i = 0; i < n; i++) {
        __atomic_store_n(&g_remoteNids[i], tmp[i], __ATOMIC_RELAXED);
    }
    __atomic_store_n(&g_nrRemote, n, __ATOMIC_RELAXED);
    __atomic_store_n(&g_remoteNidsSeq, seq + 2U, __ATOMIC_RELEASE);
}

/* 检测远端节点集合是否变化 */
bool RemoteNidSetChanged(void)
{
    int tmp[SMAP_MAX_NUMNODES];
    int n = ScanRemoteNids(tmp, SMAP_MAX_NUMNODES);
    if (n < 0)
        return false; // 扫描失败保留现状
    if (n != __atomic_load_n(&g_nrRemote, __ATOMIC_ACQUIRE))
        return true;
    for (int i = 0; i < n; i++) {
        if (g_remoteNids[i] != tmp[i])
            return true;
    }
    return false;
}

int ConfigUbWatch(uint32_t durationMs)
{
    struct ProcessManager *manager = GetProcessManager();
    struct UbWatchConfig config = { .durationMs = durationMs };
    if (durationMs == 0) {
        SMAP_LOGGER_ERROR("ConfigUbWatch: durationMs must be greater than 0");
        return -EINVAL;
    }

    if (ioctl(manager->fds.access, SMAP_IOCTL_UB_WATCH_CONFIG_CMD, &config) < 0) {
        int err = errno; /* 日志可能改写 errno，先保存 */
        SMAP_LOGGER_ERROR("ioctl SMAP_IOCTL_UB_WATCH_CONFIG_CMD failed on scan device: %d.", err);
        return -err;
    }
    return 0;
}
