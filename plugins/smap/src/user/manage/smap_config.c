/*
 * smap is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <fcntl.h>
#include <pthread.h>
#include <errno.h>
#include <time.h>
#include <sys/stat.h>

#include "securec.h"
#include "smap_user_log.h"
#include "manage.h"
#include "node_matrix.h"
#include "advanced-strategy/scene.h"
#include "smap_config.h"

#define NORMAL_ERR (-1)

#define SMAP_PROC_STAT_PATH_LEN 64
#define SMAP_PROC_STAT_BUF_LEN 2048
/* /proc/<pid>/stat 中 ')' 之后跳过 20 个空格分隔即到第 22 字段 starttime */
#define SMAP_PROC_STAT_STARTTIME_SKIP 20
#define SMAP_DECIMAL_BASE 10
/* 墙上时间换算误差容忍（clock tick 截断、时钟取样先后） */
#define SMAP_PROC_START_SLACK_SEC 2

/*
 * 配置文件读写全局互斥：IPC 线程与线程池 worker 会并发调用 Sync*，
 * 快照内存状态到落盘必须整体串行，否则交错写会产生不一致文件、越过 EOF 写入或把已移除的 pid 写回。
 * 锁序：remoteNumaInfo.lock（SetRemoteNumaInfo 持有）-> g_smapConfigLock。运行期持本锁时不得再取 attrLock /
 * remoteNumaInfo.lock（已有 attrLock -> remoteNumaInfo.lock 的路径，反向会成环）；进程快照因此在锁外完成，
 * 用序号丢弃过期快照。仅启动恢复（worker 未运行）会在本锁内操作新建槽位。
 */
static pthread_mutex_t g_smapConfigLock = PTHREAD_MUTEX_INITIALIZER;
/* SyncAllProcessConfig 快照序号（原子递增）与已落盘的最大序号（g_smapConfigLock 保护） */
static uint64_t g_smapConfigSyncSeq = 0;
static uint64_t g_smapConfigWrittenSeq = 0;
/* 选定的配置文件路径（首次使用时确定，g_smapConfigLock 保护） */
static const char *g_smapConfigPath = NULL;

/*
 * Functions to calculate config/payload length
 */

/* CalcNumaPayloadNum - Calculate numa config number */
static inline size_t CalcNumaPayloadNum(void)
{
    int nrLocalNuma = GetNrLocalNuma();
    return nrLocalNuma * SMAP_MAX_NUMNODES + SMAP_MAX_NUMNODES;
}

/* CalcNumaPayloadLen - Calculate numa payload length */
static inline size_t CalcNumaPayloadLen(void)
{
    return CONFIG_NUMA_LEN * CalcNumaPayloadNum();
}

/* CalcNumaConfigLen - Calculate numa config length */
static inline size_t CalcNumaConfigLen(void)
{
    return PAYLOAD_HEADER_LEN + CalcNumaPayloadLen();
}

/* CalcRecordStride - Calculate single process record byte size for a given record header length */
static inline size_t CalcRecordStride(size_t recHeaderLen, uint32_t nrWords, int count)
{
    return recHeaderLen + (size_t)nrWords * sizeof(unsigned long) + (size_t)count * sizeof(struct PersistRemoteTarget);
}

/* CalcProcessPayloadStride - Calculate single record byte size in the current (V4) format */
static inline size_t CalcProcessPayloadStride(uint32_t nrWords, int count)
{
    return CalcRecordStride(CONFIG_PROC_HEADER_LEN, nrWords, count);
}

/* CalcProcessPayloadBytes - Calculate total serialized process payload bytes */
static size_t CalcProcessPayloadBytes(const struct ProcessPayload *payload, int nrPayload)
{
    size_t bytes = 0;
    uint32_t nrWords = NumaWordsForCap();

    if (payload == NULL) {
        return 0;
    }
    for (int i = 0; i < nrPayload; i++) {
        bytes += CalcProcessPayloadStride(nrWords, payload[i].count);
    }
    return bytes;
}

/* CalcProcessConfigLen - Calculate process config length */
static inline size_t CalcProcessConfigLen(size_t payloadBytes)
{
    return PAYLOAD_HEADER_LEN + payloadBytes;
}

/* GetProcessConfigLen - Get process config length from process payload header */
static inline size_t GetProcessConfigLen(struct PayloadHeader *header)
{
    return PAYLOAD_HEADER_LEN + header->len;
}

static inline size_t CalcConfigLen(size_t processPayloadBytes)
{
    return CONFIG_HEADER_LEN + CalcNumaConfigLen() + CalcProcessConfigLen(processPayloadBytes);
}

/* JumpToNumaConfig - jump to numa config from config base addr */
static inline char *JumpToNumaConfig(char *base)
{
    return base + CONFIG_HEADER_LEN;
}

/* JumpToNumaPayload - jump to numa payload from numa config base addr */
static inline char *JumpToNumaPayload(char *numaBase)
{
    return numaBase + PAYLOAD_HEADER_LEN;
}

/* JumpToProcessConfig - jump to process config from config base addr */
static inline char *JumpToProcessConfig(char *base)
{
    return JumpToNumaConfig(base) + CalcNumaConfigLen();
}

/* JumpToProcessPayload - jump to process payload from process config base addr */
static inline char *JumpToProcessPayload(char *processBase)
{
    return processBase + PAYLOAD_HEADER_LEN;
}

static inline void WriteRunMode(char *addr, RunMode mode)
{
    struct SmapConfigHeader *header = (struct SmapConfigHeader *)addr;
    header->runMode = mode;
}

static inline void WriteHeader(char *addr, RunMode mode, size_t len)
{
    struct SmapConfigHeader *header = (struct SmapConfigHeader *)addr;
    header->ver = SMAP_CONFIG_VER;
    header->runMode = mode;
    header->headerLen = CONFIG_HEADER_LEN;
    header->totalLen = len;
}

static void WriteNumaConfig(char *base)
{
    struct ProcessManager *manager = GetProcessManager();
    struct RemoteNumaInfo *numaInfo = &manager->remoteNumaInfo;
    struct PayloadHeader *header = (struct PayloadHeader *)base;
    struct NumaPayload *payload;
    int nrLocalNuma = GetNrLocalNuma();
    int cap = (int)GetNumaCap();

    header->len = CalcNumaPayloadLen();
    payload = (struct NumaPayload *)JumpToNumaPayload(base);

    for (int i = 0; i < nrLocalNuma; i++) {
        /* 行首指针提到内层循环外：避免每个元素经 NM_AT/NM_STRIDE 重复调用 GetNumaCap()；
         * 私有区数组尚未分配时（NUMA 探测前重建配置）按 0 写出 */
        const uint64_t *row = numaInfo->privateSize != NULL ? NM_ROW(numaInfo->privateSize, i) : NULL;
        for (int j = 0; j < SMAP_MAX_NUMNODES; j++) {
            payload->local = i;
            payload->remote = j;
            /* ABI 流固定 SMAP_MAX_NUMNODES 宽；j >= cap 的槽位非真实节点，补 0 对齐 */
            payload->size = (row != NULL && j < cap) ? row[j] : 0;
            payload++;
        }
    }
    for (int i = 0; i < SMAP_MAX_NUMNODES; i++) {
        payload->size = numaInfo->sharedSize[i];
        payload++;
    }
}

static void WriteEmptyProcessConfigs(char *base)
{
    char *processBase = JumpToProcessConfig(base);
    struct PayloadHeader *processHeader = (struct PayloadHeader *)processBase;

    processHeader->len = 0;
}

/*
 * BuildInitConfigImage - build an initial config image (header, numa config from memory, no process record)
 */
static int BuildInitConfigImage(struct SmapConfigImage *img)
{
    size_t len = CalcConfigLen(0);
    char *buf = calloc(1, len);
    if (buf == NULL) {
        SMAP_LOGGER_ERROR("Alloc smap config image %zu failed.", len);
        return -ENOMEM;
    }

    WriteHeader(buf, GetRunMode(), len);
    WriteNumaConfig(JumpToNumaConfig(buf));
    WriteEmptyProcessConfigs(buf);

    img->data = buf;
    img->len = len;
    return 0;
}

static inline bool IsRunModeValid(RunMode runMode)
{
    return runMode >= 0 && runMode < MAX_RUN_MODE;
}

/*
 * IsConfigVersionSupported - 可恢复的持久化格式版本
 *
 * V3 是 1.2.x 起发布的格式，V4 仅在进程记录头末尾追加 startTime，读取时兼容 V3。更早的 V1/V2（编译期定宽 NUMA 布局）无法可靠换算到运行期 cap 布局，
 * 不做就地转换：由调用方把旧文件改名备份并告警后重建，绝不静默删除。
 */
static inline bool IsConfigVersionSupported(uint8_t ver)
{
    return ver == SMAP_CONFIG_VER_V3 || ver == SMAP_CONFIG_VER_V4;
}

/* ConfigRecordHeaderLen - 进程记录定长头长度：V3 不含 startTime */
static inline size_t ConfigRecordHeaderLen(uint8_t ver)
{
    return ver == SMAP_CONFIG_VER_V3 ? CONFIG_PROC_HEADER_LEN_V3 : CONFIG_PROC_HEADER_LEN;
}

static bool IsConfigHeaderValid(struct SmapConfigHeader *header)
{
    if (!IsConfigVersionSupported(header->ver)) {
        SMAP_LOGGER_ERROR("Wrong smap config header ver: %hd.", header->ver);
        return false;
    }
    if (!IsRunModeValid(header->runMode)) {
        SMAP_LOGGER_ERROR("Wrong smap config header run mode: %hd.", header->runMode);
        return false;
    }
    if (header->headerLen != CONFIG_HEADER_LEN) {
        SMAP_LOGGER_ERROR("Wrong smap config header len: %hd.", header->headerLen);
        return false;
    }
    uint32_t minLen = CONFIG_HEADER_LEN + CalcNumaConfigLen();
    if (header->totalLen < minLen) {
        SMAP_LOGGER_ERROR("Wrong smap config total len: %d.", header->totalLen);
        return false;
    }
    return true;
}

static inline void ReadHeader(char *addr, struct SmapConfigHeader *header)
{
    struct SmapConfigHeader *h = (struct SmapConfigHeader *)addr;
    header->ver = h->ver;
    header->runMode = h->runMode;
    header->headerLen = h->headerLen;
    header->totalLen = h->totalLen;
}

static void FreeConfigImage(struct SmapConfigImage *img)
{
    free(img->data);
    img->data = NULL;
    img->len = 0;
    img->mtime = 0;
}

/* SmapConfigLegacyPath - 旧版本使用的配置位置（全局可写的 /dev/shm） */
static const char *SmapConfigLegacyPath(void)
{
    return SMAP_CONFIG_LEGACY_PATH;
}

/*
 * PrepareConfigDir - make sure @dir exists, is a real directory owned by the effective user and is not writable by
 * group/others (no symlink is followed)
 */
static int PrepareConfigDir(const char *dir)
{
    struct stat st;
    int ret = 0;

    if (mkdir(dir, SMAP_CONFIG_DIR_MODE) == NORMAL_ERR && errno != EEXIST) {
        return -errno;
    }
    int fd = open(dir, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (fd == NORMAL_ERR) {
        return -errno;
    }
    if (fstat(fd, &st) == NORMAL_ERR) {
        ret = -errno;
    } else if (!S_ISDIR(st.st_mode) || st.st_uid != geteuid() || (st.st_mode & (S_IWGRP | S_IWOTH)) != 0) {
        SMAP_LOGGER_ERROR("Untrusted smap config dir %s, uid %u, mode 0%o.", dir, (unsigned int)st.st_uid,
                          (unsigned int)st.st_mode);
        ret = -EPERM;
    }
    (void)close(fd);
    return ret;
}

/*
 * SmapConfigPath - smap config location, decided once per process (caller holds g_smapConfigLock)
 *
 * Prefer the private directory; fall back to the legacy /dev/shm location (with the per-file ownership checks done in
 * LoadConfigImage) when the directory cannot be prepared, e.g. tmpfiles.d entry not installed and no permission to
 * create it.
 */
static const char *SmapConfigPath(void)
{
    if (g_smapConfigPath == NULL) {
        int ret = PrepareConfigDir(SMAP_CONFIG_DIR);
        if (ret == 0) {
            g_smapConfigPath = SMAP_CONFIG_PATH;
        } else {
            SMAP_LOGGER_WARNING("Smap config dir %s unavailable: %d, fall back to %s.", SMAP_CONFIG_DIR, ret,
                                SmapConfigLegacyPath());
            g_smapConfigPath = SmapConfigLegacyPath();
        }
    }
    return g_smapConfigPath;
}

/* IsConfigFileTrusted - 只信任本进程有效用户拥有、组/其他用户无任何权限的普通文件 */
static bool IsConfigFileTrusted(const struct stat *st)
{
    return S_ISREG(st->st_mode) && st->st_uid == geteuid() && (st->st_mode & (S_IRWXG | S_IRWXO)) == 0;
}

/* ConfigMaxLen - 合法配置文件大小上界：头部 + NUMA 配置 + 槽位上限条最大宽度的进程记录 */
static size_t ConfigMaxLen(void)
{
    size_t maxWords = (size_t)SMAP_MAX_NUMNODES / (size_t)NUMA_BITS_PER_WORD;
    return CalcConfigLen((size_t)MAX_PID_SLOTS * CalcProcessPayloadStride((uint32_t)maxWords, SMAP_MAX_NUMNODES));
}

static int ConfigReadFull(int fd, char *buf, size_t len)
{
    size_t done = 0;
    while (done < len) {
        ssize_t n = read(fd, buf + done, len - done);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n < 0) {
            return -errno;
        }
        if (n == 0) {
            /* 读取期间文件被截短 */
            return -EIO;
        }
        done += (size_t)n;
    }
    return 0;
}

static int ConfigWriteFull(int fd, const char *buf, size_t len)
{
    size_t done = 0;
    while (done < len) {
        ssize_t n = write(fd, buf + done, len - done);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n < 0) {
            /* tmpfs 空间不足等错误以 -ENOSPC 等错误码返回，而不是 SIGBUS */
            return -errno;
        }
        if (n == 0) {
            return -EIO;
        }
        done += (size_t)n;
    }
    return 0;
}

/*
 * LoadConfigImage - read the whole smap config into memory and validate its header
 *
 * Returns 0 on success, -ENOENT if the file does not exist, -EPROTONOSUPPORT if it was written in a format version
 * this build cannot recover, -EINVAL if it is too short, too large or its header is invalid (including totalLen
 * beyond the file size), -EPERM if it is a symlink or not a private regular file of the effective user, or another
 * negative errno on I/O failure.
 */
static int LoadConfigImage(const char *path, struct SmapConfigImage *img)
{
    struct stat st;
    struct SmapConfigHeader header;
    int ret;

    /* 不跟随符号链接；O_NONBLOCK 避免被替换成 FIFO 时阻塞 */
    int fd = open(path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    if (fd == NORMAL_ERR) {
        ret = -errno;
        if (ret == -ELOOP) {
            SMAP_LOGGER_ERROR("Smap config %s is a symlink, refuse to use it.", path);
            return -EPERM;
        }
        return ret;
    }
    if (fstat(fd, &st) == NORMAL_ERR) {
        ret = -errno;
        (void)close(fd);
        return ret;
    }
    if (!IsConfigFileTrusted(&st)) {
        /* 非本用户创建或权限过宽：可能被他人预置/篡改，拒绝使用 */
        SMAP_LOGGER_ERROR("Untrusted smap config %s, uid %u, mode 0%o.", path, (unsigned int)st.st_uid,
                          (unsigned int)st.st_mode);
        (void)close(fd);
        return -EPERM;
    }
    if (st.st_size < (off_t)CONFIG_HEADER_LEN || (size_t)st.st_size > ConfigMaxLen()) {
        SMAP_LOGGER_ERROR("Invalid smap config file, mode 0%o, size %lld.", (unsigned int)st.st_mode,
                          (long long)st.st_size);
        (void)close(fd);
        return -EINVAL;
    }

    size_t size = (size_t)st.st_size;
    char *buf = malloc(size);
    if (buf == NULL) {
        (void)close(fd);
        return -ENOMEM;
    }
    ret = ConfigReadFull(fd, buf, size);
    (void)close(fd);
    if (ret) {
        SMAP_LOGGER_ERROR("Read smap config failed: %d.", ret);
        free(buf);
        return ret;
    }

    ReadHeader(buf, &header);
    if (!IsConfigVersionSupported(header.ver)) {
        SMAP_LOGGER_ERROR("Unsupported smap config version %u, current version %u.", (unsigned int)header.ver,
                          (unsigned int)SMAP_CONFIG_VER);
        free(buf);
        return -EPROTONOSUPPORT;
    }
    if (!IsConfigHeaderValid(&header) || header.totalLen > size) {
        SMAP_LOGGER_ERROR("Wrong smap config header, total len %u, file size %zu.", header.totalLen, size);
        free(buf);
        return -EINVAL;
    }
    SMAP_LOGGER_INFO("ver: %hu, header len: %hu, total len: %u.", header.ver, header.headerLen, header.totalLen);
    img->data = buf;
    img->len = header.totalLen;
    img->mtime = st.st_mtime;
    return 0;
}

/*
 * StoreConfigImage - atomically replace the smap config with @img
 *
 * The image is written to a temporary file in the same directory, fsync'ed and renamed over the config, so readers
 * and a crash at any point see either the old or the new complete file. Any failure (e.g. -ENOSPC on a full tmpfs)
 * leaves the old file untouched and is returned to the caller.
 */
static int StoreConfigImage(const char *path, const struct SmapConfigImage *img)
{
    char tmpPath[SMAP_CONFIG_PATH_MAX];
    int ret;

    if (snprintf_s(tmpPath, sizeof(tmpPath), sizeof(tmpPath) - 1, "%s.XXXXXX", path) < 0) {
        return -EINVAL;
    }
    int fd = mkstemp(tmpPath);
    if (fd == NORMAL_ERR) {
        ret = -errno;
        SMAP_LOGGER_ERROR("Create temporary smap config failed: %d.", ret);
        return ret;
    }
    (void)fcntl(fd, F_SETFD, FD_CLOEXEC);

    ret = ConfigWriteFull(fd, img->data, img->len);
    if (ret == 0 && fsync(fd) == NORMAL_ERR) {
        ret = -errno;
    }
    if (close(fd) == NORMAL_ERR && ret == 0) {
        ret = -errno;
    }
    if (ret == 0 && rename(tmpPath, path) == NORMAL_ERR) {
        ret = -errno;
    }
    if (ret) {
        SMAP_LOGGER_ERROR("Store smap config (%zu bytes) failed: %d.", img->len, ret);
        (void)unlink(tmpPath);
    }
    return ret;
}

static void RecoverRunMode(RunMode runMode)
{
    SetRunMode(runMode);
    SetAdaptMem(runMode == WATERLINE_MODE);
}

/*
 * IsNumaConfigValid - check whether numa config is valid
 *
 * @numaBase: numa config base addr
 * @totalLen: smap config total length
 */
static bool IsNumaConfigValid(char *numaBase, size_t totalLen)
{
    struct PayloadHeader *header = (struct PayloadHeader *)numaBase;
    uint32_t expectedLen = CalcNumaPayloadLen();
    if (header->len != expectedLen) {
        SMAP_LOGGER_ERROR("Numa payload actualLen != expectedLen: %u != %u.", header->len, expectedLen);
        return false;
    }
    return (CONFIG_HEADER_LEN + CalcNumaConfigLen()) <= totalLen;
}

static int RecoverNumaConfig(char *numaBase)
{
    struct ProcessManager *manager = GetProcessManager();
    struct RemoteNumaInfo *numaInfo = &manager->remoteNumaInfo;
    struct NumaPayload *payload;

    SMAP_LOGGER_DEBUG("Enter RecoverNumaConfig.");
    payload = (struct NumaPayload *)JumpToNumaPayload(numaBase);
    /* cap 提到两层循环外：避免逐 (i,j) 重复调用 GetNumaCap() */
    int cap = (int)GetNumaCap();
    /* 本地节点数提到循环外：避免循环条件重复调用 GetNrLocalNuma() */
    int nrLocalNuma = GetNrLocalNuma();
    for (int i = 0; i < nrLocalNuma; i++) {
        /* 顺序读取私有区，j 与 WriteOneNumaConfig 的绝对 nid 布局对应；
         * [0, nrLocalNuma) 区间槽位闲置为 0，不参与远端语义 */
        for (int j = 0; j < SMAP_MAX_NUMNODES; j++) {
            // only called during initialization, no need to lock
            if (j < cap) {
                NM_AT(numaInfo->privateSize, i, j) = payload->size;
                NM_AT(numaInfo->privateUsedInfo, i, j).size = MBToPage(payload->size);
            }
            SMAP_LOGGER_INFO("Numa privateSize[%d][%d] %u.", i, j, payload->size);
            payload++;
        }
    }
    for (int i = 0; i < SMAP_MAX_NUMNODES; i++) {
        numaInfo->sharedSize[i] = payload->size;
        SMAP_LOGGER_INFO("Numa sharedSize[%d] %u.", i, payload->size);
        payload++;
    }
    /* payload 流（上两个循环）为固定 SMAP_MAX_NUMNODES 宽，此处仅汇总内存账本，按 cap 收敛 */
    for (int j = nrLocalNuma; j < cap; j++) {
        numaInfo->usedInfo[j].size = MBToPage(numaInfo->sharedSize[j]);
        for (int i = 0; i < nrLocalNuma; i++) {
            numaInfo->usedInfo[j].size += MBToPage(NM_AT(numaInfo->privateSize, i, j));
        }
        SMAP_LOGGER_INFO("Numa usedInfo[%d] %u.", j, numaInfo->usedInfo[j].size);
    }
    SMAP_LOGGER_DEBUG("Exit RecoverNumaConfig.");
    return 0;
}

static void WriteOneNumaConfig(char *numaBase, struct NumaPayload *np)
{
    struct NumaPayload *payload;
    int nrLocalNuma = GetNrLocalNuma();
    size_t offset;

    SMAP_LOGGER_DEBUG("Enter WriteOneNumaPayload.");
    payload = (struct NumaPayload *)JumpToNumaPayload(numaBase);

    // Calculate numa payload pointer offset according to local and remote numa
    // 布局按绝对 nid 下标：私有区每行占 SMAP_MAX_NUMNODES 槽位（[0, nrLocalNuma) 区间闲置），
    // 共享区紧跟其后；与 RecoverNumaConfig 的顺序读取一一对应
    if (np->local < 0) {
        offset = nrLocalNuma * SMAP_MAX_NUMNODES + np->remote;
    } else {
        offset = (np->local * SMAP_MAX_NUMNODES) + np->remote;
    }
    SMAP_LOGGER_DEBUG("Numa payload pointer offset: %zu.", offset);
    payload += offset;

    payload->size = np->size;
    SMAP_LOGGER_DEBUG("Exit WriteOneNumaPayload.");
}

/* ConfigRemoteNidLimit - 持久化目标远端 nid 的上界（不含）：运行期 cap，且不超过 remoteSeen 数组宽度 */
static inline int ConfigRemoteNidLimit(void)
{
    int cap = (int)GetNumaCap();
    return cap < SMAP_MAX_NUMNODES ? cap : SMAP_MAX_NUMNODES;
}

static int RestoreProcessTargetConfig(ProcessTargetConfig *config, uint8_t migrateMode, int srcNid, int count,
                                      const struct PersistRemoteTarget targets[])
{
    if (!config || !targets || count < 0 || count > SMAP_MAX_NUMNODES ||
        (migrateMode != MIG_RATIO_MODE && migrateMode != MIG_MEMSIZE_MODE) || srcNid < NUMA_NO_NODE ||
        srcNid >= GetNrLocalNuma()) {
        return -EINVAL;
    }

    InitProcessTargetConfig(config);
    config->migrateMode = (MigrateMode)migrateMode;
    config->sourceLocalNid = srcNid;
    uint64_t totalRatio = 0;
    uint64_t pageSizeKB = GetPageSize() / KIB;
    bool remoteSeen[SMAP_MAX_NUMNODES] = { false };
    /* 策略矩阵按 nrLocalNuma x cap 分配（NM_AT 无边界检查），远端 nid 必须落在运行期 cap 内 */
    int maxNid = ConfigRemoteNidLimit();
    for (int i = 0; i < count; i++) {
        const struct PersistRemoteTarget *target = &targets[i];
        bool valid = target->nid >= GetNrLocalNuma() && target->nid < maxNid && !remoteSeen[target->nid];
        if (valid && migrateMode == MIG_RATIO_MODE) {
            valid = target->ratio <= HUNDRED && totalRatio + target->ratio <= HUNDRED;
        }
        if (valid && migrateMode == MIG_MEMSIZE_MODE) {
            valid = pageSizeKB != 0 && target->memSize % pageSizeKB == 0 && target->memSize / pageSizeKB <= UINT32_MAX;
        }
        if (!valid) {
            SMAP_LOGGER_ERROR("Invalid persisted V1 target nid %d.", target->nid);
            return -EINVAL;
        }

        remoteSeen[target->nid] = true;
        config->targets[config->count++] = (ProcessRemoteTarget){
            .remoteNid = target->nid,
            .ratio = target->ratio,
            .memSizeKB = target->memSize,
        };
        totalRatio += target->ratio;
    }
    return 0;
}

static void ApplyRecoveredTargetConfig(ProcessAttr *attr, const ProcessTargetConfig *config)
{
    int totalRatio = 0;
    attr->targetConfig = *config;
    attr->migrateMode = config->migrateMode;
    attr->remoteNumaCnt = config->count;
    /* 本地节点数提到循环外：避免内层循环条件重复调用 GetNrLocalNuma() */
    int nrLocalNuma = GetNrLocalNuma();
    for (uint32_t i = 0; i < config->count; i++) {
        int remoteNid = config->targets[i].remoteNid;
        attr->migrateParam[i].nid = remoteNid;
        attr->migrateParam[i].memSize = config->targets[i].memSizeKB;
        bool firstL1 = true;
        for (int localNid = 0; localNid < nrLocalNuma; localNid++) {
            if (!InAttrL1(attr, localNid)) {
                continue;
            }
            NM_AT(attr->strategyAttr.initRemoteMemRatio, localNid, remoteNid) = config->targets[i].ratio;
            NM_AT(attr->strategyAttr.memSize, localNid, remoteNid) = firstL1 ? config->targets[i].memSizeKB : 0;
            firstL1 = false;
        }
        AddAttrL2(attr, remoteNid);
        totalRatio += config->targets[i].ratio;
    }
    attr->initLocalMemRatio = config->migrateMode == MIG_RATIO_MODE ? HUNDRED - totalRatio : HUNDRED;
}

static bool IsRecoveredTargetConfigZero(const ProcessTargetConfig *config)
{
    if (config->count == 0) {
        return true;
    }
    for (uint32_t i = 0; i < config->count; i++) {
        if ((config->migrateMode == MIG_RATIO_MODE && config->targets[i].ratio != 0) ||
            (config->migrateMode == MIG_MEMSIZE_MODE && config->targets[i].memSizeKB != 0)) {
            return false;
        }
    }
    return true;
}

static int AssignProcessAttr(ProcessAttr *attr, const struct ProcessPayloadHeader *header, const NumaBitmap *numaNodes,
                             const struct PersistRemoteTarget *targets)
{
    /* 磁盘 payload 属于外部输入：type 会被用作 nr[] 数组下标，scanType 参与枚举比较，
     * 越界值必须拒绝，防止恢复后越界写 */
    if (header->type >= TYPE_MAX || header->scanType >= SCAN_TYPE_MAX) {
        SMAP_LOGGER_ERROR("Invalid persisted process %d type %u scanType %u.", header->pid, header->type,
                          header->scanType);
        return -EINVAL;
    }

    ProcessTargetConfig activeConfig;
    int ret = RestoreProcessTargetConfig(&activeConfig, header->migrateMode, header->srcNid, header->count, targets);
    if (ret) {
        return ret;
    }

    attr->pid = header->pid;
    /* 变长位图：解码出的尾部缓冲区为借用，深拷贝到 attr 自有缓冲区 */
    if (!NumaBitmapCopy(&attr->numaAttr.numaNodes, numaNodes)) {
        SMAP_LOGGER_ERROR("Copy numa bitmap for pid %d failed.", header->pid);
        return -ENOMEM;
    }
    attr->type = header->type;
    attr->state = header->state == PROC_MIGRATE ? PROC_IDLE : header->state;
    attr->scanType = header->scanType;
    attr->scanTime = header->scanTime;
    attr->duration = header->duration;
    attr->enableSwap = true;
    attr->isFirstScan = true;
    ApplyRecoveredTargetConfig(attr, &activeConfig);
    /* 恢复持久化的 sync 旁路容量标记：记录存 uint8_t，非 0 视为 true */
    attr->ignoreRemoteCapacity = (header->ignoreRemoteCapacity == 1);
    attr->autoRemoveWhenRemoteEmpty = attr->scanType == NORMAL_SCAN && IsRecoveredTargetConfigZero(&activeConfig);
    if (time(&attr->scanStart) == (time_t)-1) {
        SMAP_LOGGER_ERROR("get time error.");
    }
    return 0;
}

/*
 * IsProcessConfigValid - check whether process config is valid
 *
 * @processBase: process config processBase addr
 * @totalLen: smap config total length
 * @recHeaderLen: fixed record header length of the file's format version
 */
static bool IsProcessConfigValid(char *processBase, size_t totalLen, size_t recHeaderLen)
{
    struct PayloadHeader *header = (struct PayloadHeader *)processBase;
    size_t processConfigLen = GetProcessConfigLen(header);
    uint32_t nrWords = NumaWordsForCap();

    if ((CONFIG_HEADER_LEN + CalcNumaConfigLen() + processConfigLen) > totalLen) {
        SMAP_LOGGER_ERROR("Process config has wrong length: %u.", header->len);
        return false;
    }

    /* V3 记录变长，按定长头 + 尾部柔性区逐条走 */
    char *cur = JumpToProcessPayload(processBase);
    size_t remaining = header->len;
    while (remaining > 0) {
        if (remaining < recHeaderLen) {
            SMAP_LOGGER_ERROR("Process config truncated, remaining %zu.", remaining);
            return false;
        }
        struct ProcessPayloadHeader *h = (struct ProcessPayloadHeader *)cur;
        if (h->nrWords != nrWords || h->count < 0 || h->count > SMAP_MAX_NUMNODES) {
            SMAP_LOGGER_ERROR("Process config invalid record: nrWords %u, count %d.", h->nrWords, h->count);
            return false;
        }
        size_t stride = CalcRecordStride(recHeaderLen, h->nrWords, h->count);
        if (stride > remaining) {
            SMAP_LOGGER_ERROR("Process config record overflows: %zu > %zu.", stride, remaining);
            return false;
        }
        cur += stride;
        remaining -= stride;
    }
    return true;
}

static inline bool HasProcessConfig(size_t totalLen)
{
    SMAP_LOGGER_DEBUG("HasProcessConfig, totalLen %zu.", totalLen);
    SMAP_LOGGER_DEBUG("HasProcessConfig, header + numa config %zu.", CONFIG_HEADER_LEN + CalcNumaConfigLen());
    return totalLen > CONFIG_HEADER_LEN + CalcNumaConfigLen();
}

/* IsPidRecovered - 恢复期间检查 pid 是否已占用槽位（配置文件中出现重复记录时只恢复第一条） */
static bool IsPidRecovered(struct ProcessManager *manager, pid_t pid)
{
    for (int i = 0; i < MAX_PID_SLOTS; i++) {
        if (EnvAtomicRead(&manager->slots[i].state) == PID_SLOT_INUSE && manager->slots[i].pid == pid) {
            return true;
        }
    }
    return false;
}

/*
 * ConfigReadProcStartTime - read field 22 (starttime, clock ticks since boot) of /proc/<pid>/stat
 *
 * Returns 0 and fills @startTime on success, negative errno otherwise (e.g. -ENOENT if the process does not exist).
 */
static int ConfigReadProcStartTime(pid_t pid, uint64_t *startTime)
{
    char path[SMAP_PROC_STAT_PATH_LEN];
    char buf[SMAP_PROC_STAT_BUF_LEN];

    if (pid <= 0 || snprintf_s(path, sizeof(path), sizeof(path) - 1, "/proc/%d/stat", pid) < 0) {
        return -EINVAL;
    }
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd == NORMAL_ERR) {
        return -errno;
    }
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    int err = errno;
    (void)close(fd);
    if (n <= 0) {
        return n < 0 ? -err : -EIO;
    }
    buf[n] = '\0';

    /* comm 可含空格与 ')'，从最后一个 ')' 之后开始数：其后第 1 个空格起为第 3 字段，第 20 个空格后为第 22 字段 */
    char *p = strrchr(buf, ')');
    if (p == NULL) {
        return -EINVAL;
    }
    for (int i = 0; i < SMAP_PROC_STAT_STARTTIME_SKIP; i++) {
        p = strchr(p, ' ');
        if (p == NULL) {
            return -EINVAL;
        }
        p++;
    }
    char *end = NULL;
    errno = 0;
    unsigned long long val = strtoull(p, &end, SMAP_DECIMAL_BASE);
    if (end == p || errno != 0) {
        return -EINVAL;
    }
    *startTime = (uint64_t)val;
    return 0;
}

/* ConfigProcStartedAfter - whether a process that started @startTime ticks after boot started after wall time @t */
static bool ConfigProcStartedAfter(uint64_t startTime, time_t t)
{
    struct timespec boot;
    struct timespec real;
    long hz = sysconf(_SC_CLK_TCK);

    if (hz <= 0 || clock_gettime(CLOCK_BOOTTIME, &boot) != 0 || clock_gettime(CLOCK_REALTIME, &real) != 0) {
        return false;
    }
    /* 进程已运行的秒数 = 开机至今 - 开机到进程启动；换算为墙上时间的启动时刻 */
    int64_t ageSec = (int64_t)boot.tv_sec - (int64_t)(startTime / (uint64_t)hz);
    int64_t startWall = (int64_t)real.tv_sec - ageSec;
    return startWall > (int64_t)t + SMAP_PROC_START_SLACK_SEC;
}

/*
 * IsPersistedProcessSame - whether the live process @pid is the one recorded in the config
 *
 * @savedStart: starttime recorded with the pid (V4), 0 if unknown
 * @fileMtime: last write time of the config file, 0 if unknown
 *
 * With a recorded starttime the live one must match exactly. Without it (V3 records, or the value could not be read
 * when writing), a process that started after the config was last written cannot be the managed one, because the
 * record was written while the managed process was alive: such a pid has been reused.
 */
static bool IsPersistedProcessSame(pid_t pid, uint64_t savedStart, time_t fileMtime)
{
    uint64_t cur = 0;

    if (savedStart == 0 && fileMtime == 0) {
        return true;
    }
    if (ConfigReadProcStartTime(pid, &cur) != 0) {
        return false;
    }
    if (savedStart != 0) {
        return cur == savedStart;
    }
    return !ConfigProcStartedAfter(cur, fileMtime);
}

static int RecoverProcessConfig(char *processBase, const struct ConfigRecoverCtx *ctx)
{
    struct PayloadHeader *header = (struct PayloadHeader *)processBase;
    struct ProcessManager *manager = GetProcessManager();
    ProcessAttr *attr;
    bool errFlag = false;

    SMAP_LOGGER_DEBUG("Enter RecoverProcessConfig.");
    /* V3 记录变长：按定长头 + 尾部柔性区逐条解码 */
    char *cur = JumpToProcessPayload(processBase);
    size_t remaining = header->len;
    size_t recHeaderLen = ctx->recHeaderLen;
    while (remaining >= recHeaderLen) {
        /* 按文件版本的定长头长度拷出记录头：V3 头不含 startTime，缺失字段保持 0 */
        struct ProcessPayloadHeader rec = { 0 };
        struct ProcessPayloadHeader *h = &rec;
        if (recHeaderLen > sizeof(rec) || memcpy_s(&rec, sizeof(rec), cur, recHeaderLen) != EOK) {
            errFlag = true;
            break;
        }
        size_t stride = CalcRecordStride(recHeaderLen, h->nrWords, h->count);
        if (h->nrWords == 0 || stride > remaining) {
            SMAP_LOGGER_ERROR("Corrupted process record, remaining %zu.", remaining);
            errFlag = true;
            break;
        }
        NumaBitmap numaNodes;
        NumaBitmapBind(&numaNodes, (unsigned long *)(cur + recHeaderLen), h->nrWords);
        const struct PersistRemoteTarget *targets =
            (const struct PersistRemoteTarget *)((unsigned long *)(cur + recHeaderLen) + h->nrWords);
        cur += stride;
        remaining -= stride;

        if (IsPidRecovered(manager, h->pid)) {
            SMAP_LOGGER_WARNING("Duplicated persisted process %d, skip recover.", h->pid);
            continue;
        }
        /* 身份校验：daemon 停止期间受管进程退出且 pid 被复用时，不得接管并迁移无关进程 */
        if (!IsPersistedProcessSame(h->pid, h->startTime, ctx->fileMtime)) {
            SMAP_LOGGER_WARNING("pid %d is not the persisted process (start time %llu), skip recover.", h->pid,
                                (unsigned long long)h->startTime);
            continue;
        }

        /* 页类型一致性校验：pid 实际页类型须与 smap 当前模式匹配，否则内核扫描返回 -EINVAL；
         * 进程已退出（-ESRCH）同样跳过恢复 */
        int hugeFlag = IsPidUsingHugePages(h->pid);
        if (hugeFlag == -ESRCH || (hugeFlag != 0) != IsHugeMode()) {
            SMAP_LOGGER_WARNING("pid %d page type mismatch smap mode, skip recover.", h->pid);
            continue;
        }
        /* pidType 与全局 pageType 解耦：按 payload 携带的 per-pid type 恢复，不再做全局一致性过滤 */
        attr = calloc(1, sizeof(ProcessAttr));
        if (!attr) {
            SMAP_LOGGER_ERROR("Malloc for process %d failed.", h->pid);
            errFlag = true;
            break;
        }
        /* strategyAttr 二维数组为指针成员，须在此分配，否则 ApplyRecoveredTargetConfig 写 NULL 指针 */
        if (InitProcessAttrArrays(attr)) {
            SMAP_LOGGER_ERROR("Init arrays for process %d failed.", h->pid);
            free(attr);
            errFlag = true;
            break;
        }
        InitProcessMigrationTargetState(attr);
        int ret = AssignProcessAttr(attr, h, &numaNodes, targets);
        if (ret) {
            SMAP_LOGGER_ERROR("Skip invalid persisted process %d: %d.", h->pid, ret);
            FreeProcessAttrArrays(attr);
            free(attr);
            continue;
        }
        SMAP_LOGGER_INFO("ProcessPayload %d, type %hu, nrWords %u, state %hu, scan type %hu, scan time %u.", attr->pid,
                         attr->type, h->nrWords, attr->state, attr->scanType, attr->scanTime);
        for (uint32_t j = 0; j < attr->targetConfig.count; j++) {
            SMAP_LOGGER_INFO("ProcessPayload %d, destNid %d, ratio %u, memsize %llu.", attr->pid,
                             attr->targetConfig.targets[j].remoteNid, attr->targetConfig.targets[j].ratio,
                             attr->targetConfig.targets[j].memSizeKB);
        }
        int addRet = PidSlotAdd(manager, attr);
        if (addRet < 0) {
            /* attr 未被任何槽位持有，必须就地释放（nr[] 由 PidSlotAdd 内部计数，失败时不计）。
             * 重复 pid 只跳过该记录；槽位耗尽时剩余记录同样无槽可用，停止恢复 */
            SMAP_LOGGER_ERROR("Add pid slot for persisted process %d failed: %d.", attr->pid, addRet);
            FreeProcessAttrArrays(attr);
            free(attr);
            if (addRet == -EEXIST) {
                continue;
            }
            break;
        }
    }
    if (errFlag) {
        for (int i = 0; i < MAX_PID_SLOTS; i++) {
            if (EnvAtomicRead(&manager->slots[i].state) == PID_SLOT_INUSE) {
                PidSlotRemove(manager, manager->slots[i].pid);
            }
        }
        manager->nr[VM_TYPE] = manager->nr[PROCESS_TYPE] = 0;
        return -ENOMEM;
    }
    SMAP_LOGGER_DEBUG("Exit RecoverProcessConfig.");
    return 0;
}

/* FreeProcessPayloadBitmaps - 回收构建载荷内深拷贝出的变长位图（幂等） */
static void FreeProcessPayloadBitmaps(struct ProcessPayload *payload, int len)
{
    if (payload == NULL) {
        return;
    }
    for (int i = 0; i < len; i++) {
        NumaBitmapFree(&payload[i].numaNodes);
    }
}

static int WriteProcessConfig(char *processBase, const struct ProcessPayload *p, int nrPayload)
{
    struct PayloadHeader *header = (struct PayloadHeader *)processBase;
    uint32_t nrWords = NumaWordsForCap();

    SMAP_LOGGER_DEBUG("Enter WriteProcessConfig.");
    header->len = (uint32_t)CalcProcessPayloadBytes(p, nrPayload);
    if (header->len == 0) {
        return 0;
    }

    char *cur = JumpToProcessPayload(processBase);
    for (int i = 0; i < nrPayload; i++) {
        struct ProcessPayloadHeader *h = (struct ProcessPayloadHeader *)cur;
        size_t stride = CalcProcessPayloadStride(nrWords, p[i].count);
        /* 整条记录先清零：位图字数不足或 bits 为空时，不得残留目标区中的旧内容（含 reserved） */
        if (memset_s(cur, stride, 0, stride) != EOK) {
            SMAP_LOGGER_ERROR("Clear record for pid %d failed.", p[i].pid);
            return -EINVAL;
        }
        h->pid = p[i].pid;
        h->scanType = p[i].scanType;
        h->type = p[i].type;
        h->state = p[i].state;
        h->migrateMode = p[i].migrateMode;
        h->srcNid = p[i].srcNid;
        h->scanTime = p[i].scanTime;
        h->duration = p[i].duration;
        h->nrWords = nrWords;
        h->count = p[i].count;
        h->ignoreRemoteCapacity = p[i].ignoreRemoteCapacity;
        h->startTime = p[i].startTime;

        unsigned long *bits = (unsigned long *)(cur + CONFIG_PROC_HEADER_LEN);
        if (p[i].numaNodes.bits != NULL && p[i].numaNodes.nWords > 0) {
            uint32_t n = p[i].numaNodes.nWords < nrWords ? p[i].numaNodes.nWords : nrWords;
            if (memcpy_s(bits, (size_t)nrWords * sizeof(unsigned long), p[i].numaNodes.bits,
                         (size_t)n * sizeof(unsigned long)) != EOK) {
                SMAP_LOGGER_ERROR("Write bitmap for pid %d failed.", p[i].pid);
                return -EINVAL;
            }
        }
        struct PersistRemoteTarget *targets = (struct PersistRemoteTarget *)(bits + nrWords);
        if (p[i].count > 0) {
            size_t bytes = (size_t)p[i].count * sizeof(*targets);
            if (memcpy_s(targets, bytes, p[i].migrateParam, bytes) != EOK) {
                SMAP_LOGGER_ERROR("Write targets for pid %d failed.", p[i].pid);
                return -EINVAL;
            }
        }
        cur += stride;
    }
    SMAP_LOGGER_DEBUG("Exit WriteProcessConfig.");
    return 0;
}

static int WriteTargetConfigToPayload(struct PersistRemoteTarget payloadTargets[], int *payloadCount,
                                      uint8_t *payloadMode, const ProcessTargetConfig *config)
{
    if (!payloadTargets || !payloadCount || !payloadMode || !config || config->count > SMAP_MAX_NUMNODES ||
        (config->migrateMode != MIG_RATIO_MODE && config->migrateMode != MIG_MEMSIZE_MODE) ||
        config->sourceLocalNid < NUMA_NO_NODE || config->sourceLocalNid >= GetNrLocalNuma()) {
        return -EINVAL;
    }
    *payloadCount = (int)config->count;
    *payloadMode = (uint8_t)config->migrateMode;
    uint64_t totalRatio = 0;
    uint64_t pageSizeKB = GetPageSize() / KIB;
    bool remoteSeen[SMAP_MAX_NUMNODES] = { false };
    int maxNid = ConfigRemoteNidLimit();
    for (uint32_t i = 0; i < config->count; i++) {
        const ProcessRemoteTarget *target = &config->targets[i];
        if (target->remoteNid < GetNrLocalNuma() || target->remoteNid >= maxNid || remoteSeen[target->remoteNid] ||
            (config->migrateMode == MIG_RATIO_MODE &&
             (target->ratio > HUNDRED || totalRatio + target->ratio > HUNDRED)) ||
            (config->migrateMode == MIG_MEMSIZE_MODE &&
             (pageSizeKB == 0 || target->memSizeKB % pageSizeKB != 0 || target->memSizeKB / pageSizeKB > UINT32_MAX))) {
            return -EINVAL;
        }
        remoteSeen[target->remoteNid] = true;
        totalRatio += target->ratio;
        payloadTargets[i] = (struct PersistRemoteTarget){
            .nid = target->remoteNid,
            .ratio = (uint8_t)target->ratio,
            .memSize = target->memSizeKB,
        };
    }
    return 0;
}

/*
 * SnapshotProcessPayload - 在持有 slot->attrLock 的前提下，把 attr 中需要持久化的字段快照到 payload
 *
 * Returns 0 on success, 1 if the attr is managed by grouped policy (not persisted), negative errno on failure.
 */
static int SnapshotProcessPayload(ProcessAttr *attr, struct ProcessPayload *tmp)
{
    if (attr->groupPolicy.enabled && attr->groupPolicy.groupCount > 0) {
        return 1;
    }
    tmp->pid = attr->pid;
    tmp->scanType = attr->scanType;
    tmp->type = attr->type;
    const ProcessTargetConfig *effectiveConfig = attr->pendingTargetConfigValid ? &attr->pendingTargetConfig :
                                                                                  &attr->targetConfig;
    tmp->srcNid = effectiveConfig->sourceLocalNid;
    tmp->state = attr->state == PROC_MIGRATE ? PROC_IDLE : attr->state;
    /* numaAttr.numaNodes is user-synthesized; persist the last read-back value.
     * 深拷贝：避免序列化期间 attr 位图被并发替换后悬空 */
    if (!NumaBitmapCopy(&tmp->numaNodes, &attr->numaAttr.numaNodes)) {
        SMAP_LOGGER_ERROR("Build pid %d numa bitmap failed.", attr->pid);
        return -ENOMEM;
    }
    tmp->scanTime = attr->scanTime;
    tmp->duration = attr->duration;
    int ret = WriteTargetConfigToPayload(tmp->migrateParam, &tmp->count, &tmp->migrateMode, effectiveConfig);
    if (ret) {
        SMAP_LOGGER_ERROR("Build pid %d effective target config payload failed: %d.", attr->pid, ret);
        return ret;
    }
    /* 与 effectiveConfig 选择一致：pending 生效时用 pending 的容量策略 */
    tmp->ignoreRemoteCapacity =
        (attr->pendingTargetConfigValid ? attr->pendingIgnoreRemoteCapacity : attr->ignoreRemoteCapacity) ? 1 : 0;
    return 0;
}

static int BuildAllProcessPayload(struct ProcessPayload **payload, int *len)
{
    struct ProcessManager *manager = GetProcessManager();
    struct ProcessPayload *p;
    int nrPayload = 0;
    int ret = 0;
    struct PidSlot *all[MAX_PID_SLOTS];
    size_t n = PidSlotCollectRefs(manager, all, MAX_PID_SLOTS);

    *payload = NULL;
    *len = 0;
    if (n == 0) {
        SMAP_LOGGER_INFO("Number of process payload is 0.");
        return 0;
    }

    /* 按槽位引用数分配上界；分组策略的进程在持锁快照时才能确定是否跳过 */
    p = calloc(n, sizeof(struct ProcessPayload));
    if (!p) {
        SMAP_LOGGER_ERROR("BuildAllProcessPayload calloc failed.");
        PidSlotReleaseRefs(all, n);
        return -ENOMEM;
    }

    for (size_t i = 0; i < n; i++) {
        /* 持 attrLock 快照：位图、pending/active 目标配置会被 IPC 线程与 worker 在锁内整体替换 */
        EnvMutexLock(&all[i]->attrLock);
        ProcessAttr *attr = PidSlotAttr(all[i]);
        ret = attr ? SnapshotProcessPayload(attr, &p[nrPayload]) : 1;
        EnvMutexUnlock(&all[i]->attrLock);
        if (ret < 0) {
            break;
        }
        if (ret == 0) {
            nrPayload++;
        }
        ret = 0;
    }
    PidSlotReleaseRefs(all, n);

    if (ret < 0 || nrPayload == 0) {
        FreeProcessPayloadBitmaps(p, (int)n);
        free(p);
        SMAP_LOGGER_DEBUG("BuildAllProcessPayload nrPayload %d, ret %d.", nrPayload, ret);
        return ret;
    }

    /* 进程身份在锁外读取（/proc 文件 I/O 不放在 attrLock 内）；读不到（进程已退出）记 0 */
    for (int i = 0; i < nrPayload; i++) {
        if (ConfigReadProcStartTime(p[i].pid, &p[i].startTime) != 0) {
            p[i].startTime = 0;
        }
    }

    SMAP_LOGGER_DEBUG("BuildAllProcessPayload nrPayload %d.", nrPayload);
    *payload = p;
    *len = nrPayload;
    return 0;
}

/*
 * ChangeProcessConfig - replace the process config of @img with a payload snapshot built by the caller
 *
 * @payload/@nrPayload: snapshot from BuildAllProcessPayload, owned by the caller
 */
static int ChangeProcessConfig(struct SmapConfigImage *img, struct ProcessPayload *payload, int nrPayload)
{
    size_t prefixLen = CONFIG_HEADER_LEN + CalcNumaConfigLen();
    size_t newConfigLen = CalcConfigLen(CalcProcessPayloadBytes(payload, nrPayload));
    char *buf;
    int ret;

    SMAP_LOGGER_DEBUG("New config length with %d processes: %zu.", nrPayload, newConfigLen);
    if (img->len < prefixLen || newConfigLen > UINT32_MAX) {
        SMAP_LOGGER_ERROR("Invalid smap config length, old %zu, new %zu.", img->len, newConfigLen);
        return -EINVAL;
    }
    buf = calloc(1, newConfigLen);
    if (buf == NULL) {
        SMAP_LOGGER_ERROR("Alloc smap config image %zu failed.", newConfigLen);
        return -ENOMEM;
    }
    /* 头部与 NUMA 配置沿用原映像，进程配置整体重建 */
    if (memcpy_s(buf, newConfigLen, img->data, prefixLen) != EOK) {
        free(buf);
        return -EINVAL;
    }
    ret = WriteProcessConfig(JumpToProcessConfig(buf), payload, nrPayload);
    if (ret) {
        SMAP_LOGGER_ERROR("Write process config failed: %d.", ret);
        free(buf);
        return ret;
    }
    WriteHeader(buf, GetRunMode(), newConfigLen);

    FreeConfigImage(img);
    img->data = buf;
    img->len = newConfigLen;
    return 0;
}

static int ParseConfig(const struct SmapConfigImage *img)
{
    int ret;
    char *numaBase;
    char *processBase;
    struct SmapConfigHeader header;
    size_t totalLen = img->len;

    SMAP_LOGGER_DEBUG("Enter ParseConfig.");
    ReadHeader(img->data, &header);
    if (!IsRunModeValid(header.runMode)) {
        SMAP_LOGGER_ERROR("Detected invalid run mode: %d.", header.runMode);
        return -EINVAL;
    }
    SMAP_LOGGER_INFO("Detected run mode: %d.", header.runMode);
    RecoverRunMode(header.runMode);

    // Parse NumaPayload
    numaBase = JumpToNumaConfig(img->data);
    if (!IsNumaConfigValid(numaBase, totalLen)) {
        SMAP_LOGGER_ERROR("Detected invalid numa config.");
        return -EINVAL;
    }
    ret = RecoverNumaConfig(numaBase);
    if (ret) {
        SMAP_LOGGER_ERROR("Parse numa config failed: %d.", ret);
        return ret;
    }

    // Parse ProcessConfig
    if (!HasProcessConfig(totalLen)) {
        SMAP_LOGGER_DEBUG("Exit ParseConfig.");
        return 0;
    }
    struct ConfigRecoverCtx ctx = {
        .recHeaderLen = ConfigRecordHeaderLen(header.ver),
        .fileMtime = img->mtime,
    };
    processBase = JumpToProcessConfig(img->data);
    if (!IsProcessConfigValid(processBase, totalLen, ctx.recHeaderLen)) {
        SMAP_LOGGER_ERROR("Detected invalid process config.");
        return -EINVAL;
    }
    ret = RecoverProcessConfig(processBase, &ctx);
    if (ret) {
        SMAP_LOGGER_ERROR("Parse process config failed: %d.", ret);
        return ret;
    }

    SMAP_LOGGER_DEBUG("Exit ParseConfig.");
    return 0;
}

static void ChangeRunMode(struct SmapConfigImage *img, RunMode runMode)
{
    SMAP_LOGGER_DEBUG("Enter ChangeRunMode.");
    WriteRunMode(img->data, runMode);
    SMAP_LOGGER_DEBUG("Exit ChangeRunMode.");
}

static int ChangeOneNumaConfig(struct SmapConfigImage *img, struct NumaPayload *payload)
{
    char *numaBase;

    SMAP_LOGGER_DEBUG("Enter ChangeOneNumaConfig.");
    numaBase = JumpToNumaConfig(img->data);
    if (!IsNumaConfigValid(numaBase, img->len)) {
        SMAP_LOGGER_ERROR("Detected invalid numa config.");
        return -EINVAL;
    }

    WriteOneNumaConfig(numaBase, payload);
    SMAP_LOGGER_DEBUG("Exit ChangeOneNumaConfig.");
    return 0;
}

/* IsConfigUnusable - 文件存在但内容不可用（损坏或版本不支持），需要备份后重建 */
static inline bool IsConfigUnusable(int ret)
{
    return ret == -EINVAL || ret == -EPROTONOSUPPORT;
}

/*
 * BackupConfig - keep an unusable smap config as "<path>.bak" instead of deleting it
 *
 * The previous backup is overwritten. The caller recreates the config afterwards, so the managed processes recorded
 * in the backup are not recovered automatically; the backup allows manual inspection or downgrade.
 */
static void BackupConfig(const char *path, int reason)
{
    char bakPath[SMAP_CONFIG_PATH_MAX];

    if (snprintf_s(bakPath, sizeof(bakPath), sizeof(bakPath) - 1, "%s%s", path, SMAP_CONFIG_BAK_SUFFIX) < 0) {
        SMAP_LOGGER_ERROR("Build smap config backup path failed.");
        return;
    }
    if (rename(path, bakPath) == NORMAL_ERR) {
        if (errno != ENOENT) {
            SMAP_LOGGER_ERROR("Backup unusable smap config (%d) failed: %d.", reason, -errno);
        }
        return;
    }
    SMAP_LOGGER_WARNING("Smap config is unusable (%d), saved as %s and recreated; processes recorded in it are not "
                        "recovered.",
                        reason, bakPath);
}

/*
 * LoadOrInitConfigImage - load the current smap config, or build one from the in-memory state
 *
 * A missing or corrupted file is rebuilt from memory (the daemon state is authoritative once recovered); other
 * errors are returned so that a transient failure never overwrites a valid file.
 */
static int LoadOrInitConfigImage(struct SmapConfigImage *img)
{
    int ret = LoadConfigImage(SmapConfigPath(), img);
    if (ret == 0) {
        return 0;
    }
    if (ret != -ENOENT && !IsConfigUnusable(ret)) {
        SMAP_LOGGER_ERROR("Load smap config failed: %d.", ret);
        return ret;
    }
    if (IsConfigUnusable(ret)) {
        BackupConfig(SmapConfigPath(), ret);
    }
    ret = BuildInitConfigImage(img);
    if (ret) {
        SMAP_LOGGER_ERROR("Init smap config error: %d.", ret);
    }
    return ret;
}

/*
 * LoadLegacyConfigImage - load the config left at the legacy location by an older release (upgrade path)
 *
 * Returns 0 if a trusted, usable legacy config was loaded, -ENOENT otherwise (an unusable or untrusted legacy file is
 * left untouched and ignored).
 */
static int LoadLegacyConfigImage(const char *path, struct SmapConfigImage *img)
{
    const char *legacyPath = SmapConfigLegacyPath();
    if (strcmp(path, legacyPath) == 0) {
        return -ENOENT;
    }
    int ret = LoadConfigImage(legacyPath, img);
    if (ret == 0) {
        SMAP_LOGGER_INFO("Migrate smap config from legacy %s to %s.", legacyPath, path);
        return 0;
    }
    if (ret != -ENOENT) {
        SMAP_LOGGER_WARNING("Ignore legacy smap config %s: %d.", legacyPath, ret);
    }
    return -ENOENT;
}

/* FinishLegacyMigration - persist the migrated config at the new location, then drop the legacy file */
static void FinishLegacyMigration(const char *path, const struct SmapConfigImage *img)
{
    int ret = StoreConfigImage(path, img);
    if (ret) {
        /* 保留旧文件；后续 SyncAllProcessConfig 会按内存状态在新位置重建 */
        SMAP_LOGGER_ERROR("Store migrated smap config to %s failed: %d, keep legacy file.", path, ret);
        return;
    }
    if (unlink(SmapConfigLegacyPath()) == NORMAL_ERR && errno != ENOENT) {
        SMAP_LOGGER_WARNING("Remove legacy smap config failed: %d.", -errno);
    }
}

static int RecoverFromConfigLocked(void)
{
    struct SmapConfigImage img = { 0 };
    const char *path = SmapConfigPath();
    bool fromLegacy = false;
    int ret = LoadConfigImage(path, &img);
    if (ret == -ENOENT) {
        ret = LoadLegacyConfigImage(path, &img);
        fromLegacy = ret == 0;
    }
    if (ret == 0) {
        ret = ParseConfig(&img);
        if (ret == 0) {
            if (fromLegacy) {
                FinishLegacyMigration(path, &img);
            }
            FreeConfigImage(&img);
            SMAP_LOGGER_INFO("Read config done.");
            return 0;
        }
        FreeConfigImage(&img);
        // Keep the old config as a backup and create a new one if parse is failed
        SMAP_LOGGER_ERROR("Parse config error: %d.", ret);
        if (!fromLegacy) {
            BackupConfig(path, ret);
        }
    } else if (IsConfigUnusable(ret)) {
        BackupConfig(path, ret);
    } else if (ret != -ENOENT) {
        SMAP_LOGGER_ERROR("Read config error: %d.", ret);
        return ret;
    }

    ret = BuildInitConfigImage(&img);
    if (ret == 0) {
        ret = StoreConfigImage(path, &img);
        FreeConfigImage(&img);
    }
    if (ret) {
        SMAP_LOGGER_ERROR("Init smap config error: %d.", ret);
    } else {
        SMAP_LOGGER_INFO("Init smap config done.");
    }
    return ret;
}

static int SyncRunModeLocked(RunMode runMode)
{
    struct SmapConfigImage img = { 0 };

    if (!IsRunModeValid(runMode)) {
        SMAP_LOGGER_ERROR("SyncRunMode runMode %d is invalid.", runMode);
        return -EINVAL;
    }

    SMAP_LOGGER_INFO("Sync run mode to %d.", runMode);
    int ret = LoadOrInitConfigImage(&img);
    if (ret) {
        return ret;
    }
    ChangeRunMode(&img, runMode);
    ret = StoreConfigImage(SmapConfigPath(), &img);
    FreeConfigImage(&img);
    if (ret) {
        SMAP_LOGGER_ERROR("Change run mode failed: %d.", ret);
    } else {
        SMAP_LOGGER_INFO("Change run mode done.");
    }
    return ret;
}

static int SyncOneNumaConfigLocked(int local, int remote, size_t size)
{
    struct SmapConfigImage img = { 0 };
    int nrLocalNuma = GetNrLocalNuma();
    struct NumaPayload config = { local, remote, size };

    if (local >= nrLocalNuma) {
        SMAP_LOGGER_ERROR("SyncOneNumaConfig local %d is invalid.", local);
        return -EINVAL;
    }
    if (remote < nrLocalNuma || remote >= SMAP_MAX_NUMNODES) {
        SMAP_LOGGER_ERROR("SyncOneNumaConfig remote %d is invalid.", remote);
        return -EINVAL;
    }

    SMAP_LOGGER_INFO("Synchronize numa config %d-%d to %zu.", local, remote, size);
    int ret = LoadOrInitConfigImage(&img);
    if (ret) {
        return ret;
    }
    ret = ChangeOneNumaConfig(&img, &config);
    if (ret == 0) {
        ret = StoreConfigImage(SmapConfigPath(), &img);
    }
    FreeConfigImage(&img);
    if (ret) {
        SMAP_LOGGER_ERROR("Change one numa config failed: %d.", ret);
    } else {
        SMAP_LOGGER_INFO("Change one numa config done.");
    }
    return ret;
}

static int SyncAllProcessConfigLocked(struct ProcessPayload *payload, int nrPayload)
{
    struct SmapConfigImage img = { 0 };

    SMAP_LOGGER_INFO("Synchronize process config begin.");
    int ret = LoadOrInitConfigImage(&img);
    if (ret) {
        return ret;
    }
    ret = ChangeProcessConfig(&img, payload, nrPayload);
    if (ret == 0) {
        ret = StoreConfigImage(SmapConfigPath(), &img);
    }
    FreeConfigImage(&img);
    if (ret) {
        SMAP_LOGGER_ERROR("Change process config failed: %d.", ret);
    } else {
        SMAP_LOGGER_INFO("Change process config done.");
    }
    return ret;
}

int RecoverFromConfig(void)
{
    pthread_mutex_lock(&g_smapConfigLock);
    int ret = RecoverFromConfigLocked();
    pthread_mutex_unlock(&g_smapConfigLock);
    return ret;
}

int SyncRunMode(RunMode runMode)
{
    pthread_mutex_lock(&g_smapConfigLock);
    int ret = SyncRunModeLocked(runMode);
    pthread_mutex_unlock(&g_smapConfigLock);
    return ret;
}

int SyncOneNumaConfig(int local, int remote, size_t size)
{
    pthread_mutex_lock(&g_smapConfigLock);
    int ret = SyncOneNumaConfigLocked(local, remote, size);
    pthread_mutex_unlock(&g_smapConfigLock);
    return ret;
}

int SyncAllProcessConfig(void)
{
    struct ProcessPayload *payload = NULL;
    int nrPayload = 0;
    /* 先领序号再快照：序号越大，快照开始得越晚，反映的内存状态越新 */
    uint64_t seq = __atomic_add_fetch(&g_smapConfigSyncSeq, 1, __ATOMIC_SEQ_CST);

    /* 快照在配置锁外进行（只取 attrLock），避免引入 g_smapConfigLock -> attrLock 锁序 */
    int ret = BuildAllProcessPayload(&payload, &nrPayload);
    if (ret) {
        SMAP_LOGGER_ERROR("Build all process payload failed: %d.", ret);
        return ret;
    }

    pthread_mutex_lock(&g_smapConfigLock);
    if (seq < g_smapConfigWrittenSeq) {
        /* 更新的快照已经落盘，旧快照不得覆盖（否则可能把已移除的 pid 写回） */
        SMAP_LOGGER_INFO("Skip stale process config snapshot %llu < %llu.", (unsigned long long)seq,
                         (unsigned long long)g_smapConfigWrittenSeq);
        ret = 0;
    } else {
        ret = SyncAllProcessConfigLocked(payload, nrPayload);
        if (ret == 0) {
            g_smapConfigWrittenSeq = seq;
        }
    }
    pthread_mutex_unlock(&g_smapConfigLock);

    FreeProcessPayloadBitmaps(payload, nrPayload);
    free(payload);
    return ret;
}
