/*
 * smap is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <sys/syscall.h>

#include "smap_user_log.h"
#include "manage.h"
#include "securec.h"
#include "oom_migrate.h"

#define MAX_MOVE_PAGES_BATCH 512 /* 单次 move_pages(2) 批量上限 */
#define NUMA_MAPS_LINE_LEN 1024 /* numa_maps 单行缓冲（与 MAX_LINE_LENGTH 等宽），超长行整行丢弃 */

/*
 * move_pages(2) 的 mpol flags（内核 uapi 取值，避免依赖 <numaif.h>）。
 * MPOL_MF_MOVE 只迁移 mapcount == 1 的页（仅本进程映射），MPOL_MF_MOVE_ALL 连
 * 多进程共享页一并迁移。匿名私有段只用 MOVE，避免把与其他进程共享的页拖到远端；
 * MOVE_ALL 仅用于大页模式下的 hugetlb 段（VM 客户机内存，可能与 vhost-user 等
 * 后端共享映射，不带 MOVE_ALL 时逐页 -EACCES）。
 */
#ifndef MPOL_MF_MOVE
#define MPOL_MF_MOVE (1 << 1)
#endif
#ifndef MPOL_MF_MOVE_ALL
#define MPOL_MF_MOVE_ALL (1 << 2)
#endif
#define URGENT_MOVE_PAGES_FLAGS_ANON (MPOL_MF_MOVE)
#define URGENT_MOVE_PAGES_FLAGS_HUGETLB (MPOL_MF_MOVE | MPOL_MF_MOVE_ALL)

/*
 * numa_maps 流式游标：按段（VMA）产出"当前确实位于本地节点"的页地址。
 * numa_maps 只给出段起始地址与各节点页数，段内哪些页在本地未知，因此对候选段
 * 用 move_pages(nodes=NULL) 逐批查询每页所在节点，只把落在 [0, nrLocalNuma)
 * 的页交给迁移；段尾由下一行的起始地址界定（预读一行）。
 */
typedef struct {
    char line[NUMA_MAPS_LINE_LEN]; /* 当前段所在行 */
    char next[NUMA_MAPS_LINE_LEN]; /* 预读的下一行 */
    bool hasNext; /* next[] 持有一条完整行 */
    bool eof;
    bool hasSeg; /* 当前段尚未枚举完 */
    unsigned long segAddr; /* 下一个待查询地址 */
    unsigned long segEnd; /* 段尾（不含）；0 表示末段，尾部未知 */
    uint64_t segLocal; /* numa_maps 报告的该段本地页数 */
    uint64_t segFound; /* 已找到的本地页数 */
    int segFlags; /* 该段迁移使用的 move_pages flags */
    uint64_t *probeAddrs; /* MAX_MOVE_PAGES_BATCH 个查询地址 */
    int *probeStatus; /* MAX_MOVE_PAGES_BATCH 个查询结果 */
} NumaScanCursor;

/* move_pages(2) 薄封装（mockable seam）：UT 经 run_dt.sh strip static 后拦截此处，不触达真实 syscall。
 * nodes == NULL 时为查询模式：status[i] 返回页当前所在 nid 或负错误码。 */
static long SmapMovePages(int pid, unsigned long count, const void **pages, const int *nodes, int *status, int flags)
{
    return syscall(__NR_move_pages, pid, count, pages, nodes, status, flags);
}

/*
 * 读取一整行 numa_maps 到 buf。行长超过缓冲区时（文件路径可达 PATH_MAX）fgets
 * 会把一行拆成多段，后续段以路径剩余部分开头，若按行解析会把路径中的十六进制
 * 字符误当成段起始地址；因此丢弃整条超长行（含其所有后续段）。返回 false 表示 EOF。
 */
static bool ReadNumaMapsLine(FILE *fp, char *buf, size_t size)
{
    bool dropping = false;
    for (;;) {
        char *ret = fgets(buf, (int)size, fp);
        if (ret == NULL) {
            return false;
        }
        /* 真实 fgets 返回 buf 本址；UT mock 下返回预设行串（可能不写缓冲），统一拷入 buf。 */
        if (ret != buf && strcpy_s(buf, size, ret) != EOK) {
            buf[0] = '\0';
            dropping = true; /* 超长（不可能来自真实 fgets），按截断处理 */
            continue;
        }
        size_t len = strnlen(buf, size);
        bool complete = (len > 0 && buf[len - 1] == '\n') || len + 1 < size;
        if (dropping) {
            dropping = !complete; /* 丢弃超长行的剩余段，直到读到行尾 */
            continue;
        }
        if (!complete) {
            dropping = true;
            continue;
        }
        return true;
    }
}

/*
 * 进程当前是否已有页驻留在 nid 上（读 numa_maps，任一段 N<nid> 计数 > 0 即为真）。
 * walkPage 是扫描周期的快照，迁出刚完成时 L2 计数可能仍为 0，不能单独用来判定
 * "已迁移过"；以 numa_maps 的实时驻留为准。读取失败按"无驻留"处理（不额外拦截）。
 */
static bool PidHasPagesOnNode(pid_t pid, int nid)
{
    char pattern[NUMA_MAPS_MAX_PATTERN_LEN];
    if (snprintf_s(pattern, sizeof(pattern), sizeof(pattern) - 1, " N%d=", nid) < 0) {
        return false;
    }
    FILE *fp = OpenNumaMaps(pid);
    if (fp == NULL) {
        return false;
    }
    char line[NUMA_MAPS_LINE_LEN];
    bool found = false;
    while (!found && ReadNumaMapsLine(fp, line, sizeof(line))) {
        const char *sub = strstr(line, pattern);
        if (sub == NULL) {
            continue;
        }
        const char *value = sub + strlen(pattern);
        char *end = NULL;
        errno = 0;
        unsigned long long c = strtoull(value, &end, 10);
        found = (value != end && errno == 0 && c > 0);
    }
    (void)fclose(fp);
    return found;
}

/* 累加本地页数 = Σ N<i>=<count>，i ∈ [0, nrLocalNuma)。 */
static uint64_t ParseNumaMapLocalCount(const char *line, int nrLocalNuma)
{
    uint64_t total = 0;
    for (int i = 0; i < nrLocalNuma; i++) {
        char pattern[NUMA_MAPS_MAX_PATTERN_LEN];
        if (snprintf_s(pattern, sizeof(pattern), sizeof(pattern) - 1, " N%d=", i) < 0) {
            continue;
        }
        const char *sub = strstr(line, pattern);
        if (sub == NULL) {
            continue;
        }
        const char *value = sub + strlen(pattern);
        char *end = NULL;
        errno = 0;
        uint64_t c = strtoull(value, &end, 10);
        if (value == end || errno == ERANGE || c == UINT64_MAX) {
            continue; /* 无转换或溢出，跳过该 node 字段 */
        }
        total += c;
    }
    return total;
}

/*
 * 判定一行 numa_maps 是否为紧急迁出候选段，并取段起始地址、本地页数与迁移 flags。
 * - 大页模式：只取 hugetlb 段（kernelpagesize_kB=2048），用 MOVE_ALL；
 * - 4K 模式：只取匿名私有段（有 anon=、无 file=、非 huge）。文件页（libc/共享库/
 *   page cache/shm）可能被系统中任意进程共享，不在受管范围内，不得搬到远端；
 *   hugetlb 段的 N 计数以大页为单位，不能按 4K 步长枚举。
 */
static bool ParseNumaMapSegment(const char *line, int nrLocalNuma, bool huge, unsigned long *segStart,
                                uint64_t *localCount, int *flags)
{
    bool hugeLine = IsNumaMapLineHuge((char *)line);
    if (huge) {
        if (!hugeLine) {
            return false;
        }
        *flags = URGENT_MOVE_PAGES_FLAGS_HUGETLB;
    } else {
        if (hugeLine || strstr(line, " huge") != NULL || strstr(line, " file=") != NULL ||
            strstr(line, " anon=") == NULL) {
            return false;
        }
        *flags = URGENT_MOVE_PAGES_FLAGS_ANON;
    }
    if (sscanf_s(line, "%lx", segStart) != 1) {
        return false; /* 解析段起始地址失败 */
    }
    *localCount = ParseNumaMapLocalCount(line, nrLocalNuma);
    return true;
}

/* 推进到下一个有本地页的候选段；返回 false 表示 numa_maps 已读完。 */
static bool NextCandidateSegment(FILE *fp, int nrLocalNuma, bool huge, NumaScanCursor *cur)
{
    for (;;) {
        if (!cur->hasNext) {
            if (cur->eof || !ReadNumaMapsLine(fp, cur->next, sizeof(cur->next))) {
                cur->eof = true;
                return false;
            }
        }
        if (strcpy_s(cur->line, sizeof(cur->line), cur->next) != EOK) {
            cur->hasNext = false;
            continue;
        }
        cur->hasNext = false;
        /* 预读下一行，以其起始地址作为当前段的上界 */
        unsigned long segEnd = 0;
        if (!cur->eof && ReadNumaMapsLine(fp, cur->next, sizeof(cur->next))) {
            cur->hasNext = true;
            if (sscanf_s(cur->next, "%lx", &segEnd) != 1) {
                segEnd = 0;
            }
        } else {
            cur->eof = true;
        }

        unsigned long segStart = 0;
        uint64_t localCount = 0;
        int flags = 0;
        if (!ParseNumaMapSegment(cur->line, nrLocalNuma, huge, &segStart, &localCount, &flags)) {
            continue;
        }
        if (localCount == 0 || (segEnd != 0 && segEnd <= segStart)) {
            continue; /* 该段无本地页（不误碰纯远端段）或边界异常 */
        }
        cur->hasSeg = true;
        cur->segAddr = segStart;
        cur->segEnd = segEnd;
        cur->segLocal = localCount;
        cur->segFound = 0;
        cur->segFlags = flags;
        return true;
    }
}

/*
 * 在当前段内查询一批页的所在节点，把位于本地节点的页追加到 addrs[*cnt..cap)。
 * 段内本地页已找齐、到达段尾或越过末段的映射区（-EFAULT）时结束本段。
 * 返回 0 或系统级错误（-EACCES/-EPERM）。
 */
static int ProbeSegmentLocalPages(pid_t pid, int nrLocalNuma, uint64_t pageSize, NumaScanCursor *cur, uint64_t *addrs,
                                  int *cnt, uint64_t cap)
{
    uint64_t probeCnt = MAX_MOVE_PAGES_BATCH;
    if (cur->segEnd != 0) {
        uint64_t remain = (cur->segEnd - cur->segAddr) / pageSize;
        probeCnt = remain < probeCnt ? remain : probeCnt;
    }
    if (probeCnt == 0 || cur->segFound >= cur->segLocal) {
        cur->hasSeg = false;
        return 0;
    }
    for (uint64_t i = 0; i < probeCnt; i++) {
        cur->probeAddrs[i] = (uint64_t)(cur->segAddr + i * pageSize);
    }
    long rc = SmapMovePages(pid, (unsigned long)probeCnt, (const void **)cur->probeAddrs, NULL, cur->probeStatus, 0);
    if (rc < 0) {
        int err = errno;
        if (err == EACCES || err == EPERM) {
            return -err;
        }
        cur->hasSeg = false; /* 查询失败：放弃本段，继续后续段 */
        return 0;
    }

    bool allFault = true;
    uint64_t consumed = 0;
    for (; consumed < probeCnt; consumed++) {
        int s = cur->probeStatus[consumed];
        allFault = allFault && s == -EFAULT;
        if (s < 0 || s >= nrLocalNuma) {
            continue; /* 空洞/不可访问/已在远端的页不是候选 */
        }
        if ((uint64_t)*cnt >= cap) {
            break; /* 输出已满：本页留待下一批重新查询 */
        }
        addrs[(*cnt)++] = cur->probeAddrs[consumed];
        cur->segFound++;
    }
    cur->segAddr += consumed * pageSize;
    if (cur->segFound >= cur->segLocal || (cur->segEnd == 0 && allFault) ||
        (cur->segEnd != 0 && cur->segAddr >= cur->segEnd)) {
        cur->hasSeg = false;
    }
    return 0;
}

/*
 * 从已打开的 numa_maps 流中收集一批确实位于本地节点的页地址（≤ MAX_MOVE_PAGES_BATCH 且 ≤ maxPages）。
 * 一批只来自同一段，*flags 为该段的迁移 flags。返回 0（*outCnt=0 表示无更多候选）或系统级错误。
 */
static int CollectVaddrsBatch(FILE *fp, pid_t pid, int nrLocalNuma, uint64_t pageSize, uint64_t maxPages,
                              NumaScanCursor *cur, uint64_t *addrs, int *outCnt, int *flags)
{
    *outCnt = 0;
    int cnt = 0;
    uint64_t cap = (maxPages > MAX_MOVE_PAGES_BATCH) ? MAX_MOVE_PAGES_BATCH : maxPages;
    bool huge = IsHugeMode();

    while ((uint64_t)cnt < cap) {
        if (!cur->hasSeg) {
            if (cnt > 0) {
                break; /* 保持一批只含同一段，flags 一致 */
            }
            if (!NextCandidateSegment(fp, nrLocalNuma, huge, cur)) {
                break; /* EOF */
            }
        }
        *flags = cur->segFlags;
        int ret = ProbeSegmentLocalPages(pid, nrLocalNuma, pageSize, cur, addrs, &cnt, cap);
        if (ret != 0) {
            *outCnt = cnt;
            return ret;
        }
    }
    *outCnt = cnt;
    return 0;
}

/*
 * 批量迁移一个进程：本地(任意本地节点) -> destNid(L2)，边扫描 numa_maps 边按 MAX_MOVE_PAGES_BATCH 分批迁移。
 * 固定分配 MAX_MOVE_PAGES_BATCH 的地址/节点/状态数组（不按 pageBudget 整体分配），迁够 pageBudget 即停。
 * 候选页在迁移前已确认位于本地节点，成功判定：move_pages(2) 成功时 status[i] = 页面最终所在 nid，
 * 须 == destNid 才算迁达；失败时 status[i] 为负错误码（-ENOENT/-EACCES/-EIO 等），逐页记录首个 errno。
 */
static int MigratePidFromToL2(pid_t pid, int nrLocalNuma, int destNid, uint64_t pageSize, uint64_t *pageBudget)
{
    FILE *fp = OpenNumaMaps(pid);
    if (fp == NULL) {
        SMAP_LOGGER_ERROR("Open pid %d numa_maps failed.", pid);
        return -ENODEV;
    }

    /* 固定大小批数组，边扫描边迁移；不按 pageBudget 整体分配，避免极大值触发 TB 级 malloc。 */
    uint64_t *addrs = malloc(sizeof(uint64_t) * MAX_MOVE_PAGES_BATCH);
    int *nodes = malloc(sizeof(int) * MAX_MOVE_PAGES_BATCH);
    int *status = malloc(sizeof(int) * MAX_MOVE_PAGES_BATCH);
    NumaScanCursor *cur = calloc(1, sizeof(*cur));
    uint64_t *probeAddrs = malloc(sizeof(uint64_t) * MAX_MOVE_PAGES_BATCH);
    int *probeStatus = malloc(sizeof(int) * MAX_MOVE_PAGES_BATCH);
    if (addrs == NULL || nodes == NULL || status == NULL || cur == NULL || probeAddrs == NULL || probeStatus == NULL) {
        free(addrs);
        free(nodes);
        free(status);
        free(cur);
        free(probeAddrs);
        free(probeStatus);
        (void)fclose(fp);
        SMAP_LOGGER_ERROR("malloc batch arrays failed, pid %d.", pid);
        return -ENOMEM;
    }
    for (int i = 0; i < MAX_MOVE_PAGES_BATCH; i++) {
        nodes[i] = destNid; /* 全部目标 = L2 */
    }
    cur->probeAddrs = probeAddrs;
    cur->probeStatus = probeStatus;

    uint64_t movedCnt = 0;
    uint64_t failedCnt = 0;
    uint64_t attemptedCnt = 0; /* 实际收集到的候选页数，用于区分"无候选(非错误)"与"全失败(错误)" */
    int lastErr = 0; /* 保存首个失败 errno（全局 errno 或逐页 -status），避免被后续日志/free 覆写 */
    int ret = 0;
    while (*pageBudget > 0) {
        int cnt = 0;
        int flags = URGENT_MOVE_PAGES_FLAGS_ANON;
        int r = CollectVaddrsBatch(fp, pid, nrLocalNuma, pageSize, *pageBudget, cur, addrs, &cnt, &flags);
        if (r != 0) {
            lastErr = -r;
            ret = r;
            break;
        }
        if (cnt == 0) {
            break; /* numa_maps 扫完，无更多候选 */
        }
        attemptedCnt += cnt;
        /* 复用 addrs 段作为页指针数组：LP64 下 uint64_t 与 void* 等宽，逐项即页虚拟地址。 */
        const void **pages = (const void **)addrs;
        SMAP_LOGGER_DEBUG("move_pages in: pid=%d batch=%d destNid=%d nrLocal=%d flags=0x%x", pid, cnt, destNid,
                          nrLocalNuma, flags);
        long rc = SmapMovePages(pid, (unsigned long)cnt, pages, nodes, status, flags);
        if (rc < 0) {
            /* 全局失败（EACCES/EFAULT/EPERM/ENOMEM）：内核不填 status[]，整批计失败，不读未初始化内存。
               EACCES/EPERM 多为系统级权限问题，后续批同样会失败，终止本 pid。 */
            lastErr = errno;
            failedCnt += cnt;
            if (lastErr == EACCES || lastErr == EPERM) {
                ret = -lastErr;
                break;
            }
            continue;
        }
        /* 成功判定：status[i] == destNid 才代表迁达目标（候选页迁移前均在本地）。逐页失败保留首个 errno。 */
        uint64_t batchMoved = 0;
        for (int i = 0; i < cnt; i++) {
            int s = status[i];
            if (s == destNid) {
                batchMoved++;
            } else {
                failedCnt++;
                if (s < 0 && lastErr == 0) {
                    lastErr = -s; /* 逐页失败 errno（如 ENOENT/EACCES/EIO），保留首个 */
                }
            }
        }
        movedCnt += batchMoved;
        *pageBudget -= (batchMoved > *pageBudget) ? *pageBudget : batchMoved; /* 扣减预算 */
    }
    if (fclose(fp)) {
        SMAP_LOGGER_WARNING("Close numa_maps failed, pid=%d.", pid);
    }
    free(addrs);
    free(nodes);
    free(status);
    free(cur);
    free(probeAddrs);
    free(probeStatus);

    SMAP_LOGGER_INFO("pid %d local(nrLocal=%d)->L2=%d move_pages moved=%llu failed=%llu.", pid, nrLocalNuma, destNid,
                     (unsigned long long)movedCnt, (unsigned long long)failedCnt);

    if (ret != 0) {
        return ret;
    }
    if (attemptedCnt == 0) {
        return 0; /* 无候选页，非错误 */
    }
    if (movedCnt == 0) {
        /* 有候选但无一页迁达（逐页全失败或全局失败）：必须返回负错误，不能返回 0 误导调用方。 */
        if (lastErr == 0) {
            lastErr = EIO; /* 无可用 errno 时保守报 EIO（如远端节点故障逐页 -EIO） */
        }
        return -lastErr;
    }
    return 0;
}

typedef struct {
    pid_t pid;
    int l2;
} OomPidSnap;

/* 主入口：锁内只取快照，锁外做 numa_maps 读与 move_pages，避免 OOM 卡扫描线程。 */
void FindPidMigrateSize(uint64_t size)
{
    struct ProcessManager *manager = GetProcessManager();
    if (manager == NULL) {
        SMAP_LOGGER_ERROR("process manager is null.");
        return;
    }

    uint64_t pageSize = manager->tracking.pageSize;
    if (pageSize == 0) {
        SMAP_LOGGER_ERROR("pageSize is 0, smap not initialized?");
        return;
    }
    uint64_t pageBudget = size / pageSize;
    if (pageBudget == 0) {
        SMAP_LOGGER_INFO("size %llu too small (< pageSize %llu), skip.", (unsigned long long)size,
                         (unsigned long long)pageSize);
        return;
    }

    int nrLocalNuma = (int)manager->nrLocalNuma; /* 初始化后不变，锁外读取安全 */

    /* 持锁只取快照，numa_maps 读与 move_pages 在锁外执行，避免 OOM 卡扫描线程。 */
    OomPidSnap snap[MAX_4K_PROCESSES_CNT];
    int n = 0;
    struct PidSlot *all[MAX_PID_SLOTS];
    size_t cnt = PidSlotCollectRefs(manager, all, MAX_PID_SLOTS);
    for (size_t k = 0; k < cnt && n < MAX_4K_PROCESSES_CNT; k++) {
        ProcessAttr *cur = all[k]->attr;
        int l2 = GetAttrL2(cur);
        if (l2 == NUMA_NO_NODE) {
            continue; /* 未配置远端目标，跳过 */
        }
        /*
         * actc 快照周期结束即释放，判重改用常驻的 walkPage 元数据：
         * 远端(L2) 已有驻留页时避免重复介入。
         */
        if (cur->state == PROC_MIGRATE || cur->state == PROC_MOVE || cur->walkPage.nrPages[l2] > 0) {
            continue;
        }
        snap[n].pid = cur->pid;
        snap[n].l2 = l2;
        n++;
    }
    PidSlotReleaseRefs(all, cnt);

    for (int i = 0; i < n && pageBudget > 0; i++) {
        if (PidHasPagesOnNode(snap[i].pid, snap[i].l2)) {
            /* 已有页驻留在 L2：视为已迁移过的进程，不重复介入（walkPage 快照可能尚未反映） */
            SMAP_LOGGER_INFO("pid %d already has pages on L2 %d, skip urgent migrate.", snap[i].pid, snap[i].l2);
            continue;
        }
        int ret = MigratePidFromToL2(snap[i].pid, nrLocalNuma, snap[i].l2, pageSize, &pageBudget);
        if (ret != 0) {
            if (ret == -EACCES || ret == -EPERM) {
                /* 系统级权限/能力故障：后续 pid 同样会失败，终止本轮避免无谓 churn。 */
                SMAP_LOGGER_ERROR("systemic migrate failure (ret=%d) at pid %d, abort urgent pass.", ret, snap[i].pid);
                break;
            }
            SMAP_LOGGER_ERROR("migrate pid %d failed: %d, try next.", snap[i].pid, ret);
        }
    }
    SMAP_LOGGER_INFO("urgent migrate done, remaining budget %llu pages.", (unsigned long long)pageBudget);
}
