/*
 * smap is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#ifndef __CONFIG_FILE_H__
#define __CONFIG_FILE_H__

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "manage.h"

#define NORMAL_ERR (-1)

/*
 * 配置放在 daemon 用户私有目录（tmpfs，跨 daemon 重启保留、重启机器清空，与原 /dev/shm 语义一致），
 * 目录由 tmpfiles.d（smap.spec 安装 ubturbo-smap-tmpfiles.conf）按 0700 ubturbo:ubturbo 创建。
 * 私有目录不可用时退回旧位置 /dev/shm，并对文件做属主/权限/符号链接校验。
 */
#define SMAP_CONFIG_DIR "/run/smap"
#define SMAP_CONFIG_DIR_MODE (0700)
#define SMAP_CONFIG_PATH SMAP_CONFIG_DIR "/smap_config"
#define SMAP_CONFIG_LEGACY_PATH "/dev/shm/smap_config"
#define SMAP_CONFIG_MODE (0600)
#define SMAP_CONFIG_PATH_MAX 256
/* 不可用（损坏/版本不支持）的旧配置改名保留的后缀 */
#define SMAP_CONFIG_BAK_SUFFIX ".bak"

/*
 * Layout of config is as follows:
 * | VER | HEADER_LEN | TOTAL_LEN | = 2B + 2B + 4B
 * | NUMA_PAYLOAD_LEN | NUMA_PAYLOAD | = 4B + 4B * nrLocalNuma * SMAP_MAX_NUMNODES
 * | PROCESS_PAYLOAD_LEN | PROCESS_PAYLOAD | = 4B + Σ 进程记录字节数（V3/V4）
 *
 * V2 起进程记录为定长；V3 起改为「定长头 + 尾部柔性区」，节点位图不再按
 * 编译期 NUMA_NODES_BIT_WORDS 定宽，字数由写入侧运行期 cap 折算。
 * V4 在进程记录定长头末尾追加 startTime（进程身份），其余布局与 V3 相同；读取侧兼容 V3。
 */
#define SMAP_CONFIG_VER_V1 1
#define SMAP_CONFIG_VER_V2 2
#define SMAP_CONFIG_VER_V3 3
#define SMAP_CONFIG_VER_V4 4
#define SMAP_CONFIG_VER SMAP_CONFIG_VER_V4
#define CONFIG_HEADER_LEN sizeof(struct SmapConfigHeader)
#define PAYLOAD_HEADER_LEN sizeof(struct PayloadHeader)
#define CONFIG_NUMA_LEN sizeof(struct NumaPayload)
#define CONFIG_PROC_HEADER_LEN sizeof(struct ProcessPayloadHeader)
/* V3 记录定长头不含 startTime */
#define CONFIG_PROC_HEADER_LEN_V3 offsetof(struct ProcessPayloadHeader, startTime)

struct SmapConfigHeader {
    uint8_t ver;
    uint8_t runMode;
    uint16_t headerLen;
    uint32_t totalLen;
};

struct PayloadHeader {
    uint32_t len;
};

struct NumaPayload {
    int16_t local; // L1 NUMA ID，0-255
    int16_t remote; // L2 NUMA ID，0-255
    uint32_t size; // Available size, unit is MB
};

struct OldProcessPayload {
    pid_t pid;
    uint8_t ratio; // remote ratio set by upstream component
    uint8_t scanType;
    uint8_t type;
    uint8_t state;
    uint8_t migrateMode;
    int16_t l1Node[4];
    int16_t l2Node[4];
    uint32_t scanTime;
    uint64_t memSize;
};

struct PersistRemoteTarget {
    int nid;
    uint8_t ratio; // remote ratio set by upstream component
    uint64_t memSize;
};

/*
 * V4 记录定长头（40B，8 字节对齐；V3 为不含 startTime 的前 32B）。尾部柔性区依次为：
 *   unsigned long numaNodes[nrWords]                          // 变长节点位图
 *   struct PersistRemoteTarget migrateParam[count]            // 活跃 Pair 目标
 * 单条记录字节数 = 定长头长度 + nrWords * 8 + count * sizeof(struct PersistRemoteTarget)。
 */
struct ProcessPayloadHeader {
    pid_t pid;
    uint8_t scanType;
    uint8_t type;
    uint8_t state;
    uint8_t migrateMode;
    int srcNid; // 迁出源本地 NUMA，NUMA_NO_NODE 表示未指定
    uint32_t scanTime;
    uint32_t duration;
    uint32_t nrWords; // 尾部 numaNodes 字数（按写入侧运行期 cap 折算）
    int count; // 尾部 migrateParam 条数
    uint8_t ignoreRemoteCapacity; // sync 旁路容量标记
    /* 对齐填充：定长头保持 8 字节对齐，尾部柔性区（unsigned long 位图）起始地址仍 8 字节对齐 */
    uint8_t reserved[3];
    /* V4：/proc/<pid>/stat 第 22 字段 starttime（开机以来的 clock tick），0 表示写入时未知。
     * 恢复时与当前同 pid 进程比对，防止 pid 复用后接管无关进程 */
    uint64_t startTime;
};

/* 进程内构建载荷（含变长位图句柄）；序列化后落盘为 V3 记录 */
struct ProcessPayload {
    pid_t pid;
    uint8_t scanType;
    uint8_t type;
    uint8_t state;
    uint8_t migrateMode;
    int srcNid;
    uint32_t scanTime;
    uint32_t duration;
    NumaBitmap numaNodes;
    int count;
    struct PersistRemoteTarget migrateParam[SMAP_MAX_NUMNODES];
    uint8_t ignoreRemoteCapacity; /* 新增：sync 旁路容量标记 */
    uint64_t startTime; /* 进程启动时间（clock tick），0 表示未知 */
};

/*
 * 内存中的完整配置映像。所有读改写都先在映像上完成，再整体原子替换文件：
 * 不再 mmap 配置文件（短文件/并发截断/tmpfs 空间不足都会在访存时 SIGBUS）。
 */
struct SmapConfigImage {
    char *data;
    size_t len; /* == header.totalLen */
    time_t mtime; /* 读入时文件的最后修改时间，新建映像为 0 */
};

/* 解析进程记录所需的上下文 */
struct ConfigRecoverCtx {
    size_t recHeaderLen; /* 进程记录定长头长度：V3 为 CONFIG_PROC_HEADER_LEN_V3，V4 为 CONFIG_PROC_HEADER_LEN */
    time_t fileMtime; /* 配置文件最后写入时间，用于无 startTime 记录的 pid 复用判定；0 表示未知 */
};

int RecoverFromConfig(void);
int SyncRunMode(RunMode runMode);
int SyncOneNumaConfig(int local, int remote, size_t size);
int SyncAllProcessConfig(void);

#endif /* __CONFIG_FILE_H__ */
