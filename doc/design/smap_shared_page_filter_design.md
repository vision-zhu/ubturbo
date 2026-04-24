# SMAP迁出内存共享页过滤功能设计文档

## 文档版本
| 版本 | 日期 | 作者 | 描述 |
| --- | --- | --- | --- |
| v1.0 | 2026-04-22 | dszhu | 初稿 |

## 1. 背景

### 1.1 业务背景
在内存冷热流动场景中，进程的内存页面可能包括私有页和共享页两种类型。共享页是指通过`mmap(MAP_SHARED)`或类似机制创建的、被多个进程共享访问的内存页面。

**可靠性风险**：如果将共享页迁移到远端NUMA，可能引发以下问题：
1. **数据一致性问题**：共享页被多个进程访问，迁移后可能导致其他进程无法正确访问该页面
2. **性能下降**：其他共享该页面的进程需要跨NUMA访问，增加访问延迟
3. **系统稳定性问题**：某些共享页（如KVM共享内存）迁移可能导致虚拟机异常或系统崩溃
4. **内存映射失效**：共享内存的物理地址变更可能导致映射关系错乱

### 1.2 技术背景
当前UBTurbo SMAP模块已实现部分共享页过滤机制：
- **虚拟化场景**：通过`ParseMmapType`识别VM的共享内存模式（`MMAP_SHARED`）
- **白名单机制**：`ActcData.isWhiteListPage`字段标记不应迁移的页面
- **共享内存检测**：通过解析libvirt XML识别`memAccess='shared'`类型

### 1.3 问题分析
当前实现存在以下不足：
1. **仅支持虚拟化场景**：共享页检测仅针对QEMU进程，4K通算进程未覆盖
2. **白名单依赖内核**：白名单页面由内核扫描模块标记，用户态无法主动添加
3. **共享页识别不完整**：未覆盖所有共享内存场景（如shm、tmpfs等）

## 2. 目标

### 2.1 功能目标
1. **扩展共享页识别范围**：支持识别4K进程和2M虚拟机的共享页
2. **迁移过滤机制**：确保共享页不被迁移到远端NUMA
3. **多种识别方式**：支持通过`/proc/[pid]/maps`、`numa_maps`等识别共享页
4. **白名单自动管理**：在进程纳管时自动识别共享页并添加到白名单，无需用户干预

### 2.2 性能目标
| 指标 | 目标值 |
| --- | --- |
| 共享页识别延迟 | < 50ms（单进程） |
| 迁移过滤开销 | 对正常迁移无额外性能损耗 |
| 内存开销 | 白名单位图占用 < 1MB/进程 |

### 2.3 可靠性目标
- 共享页迁移率为0%（确保不迁移）
- 迁移后数据一致性100%保持
- 系统稳定性不受影响

## 3. 非目标

1. **不支持动态共享页检测**：不实现运行时动态检测新创建的共享页
2. **不支持跨进程共享页协调**：不实现多进程共享页的协调迁移
3. **不支持用户自定义白名单配置文件**：不通过配置文件持久化白名单

## 4. 术语

| 术语 | 定义 |
| --- | --- |
| 共享页 | 通过`mmap(MAP_SHARED)`创建的、多进程共享访问的内存页面 |
| 私有页 | 进程独占使用的内存页面，可安全迁移 |
| 白名单页面 | 标记为不可迁移的页面，包括共享页和特殊页面 |
| MAP_SHARED | mmap的共享映射模式，多个进程可共享同一物理内存 |
| MAP_PRIVATE | mmap的私有映射模式，进程独占使用（COW） |
| shm | System V共享内存机制 |
| tmpfs | 内存文件系统，文件内容映射为共享内存 |
| KSM | Kernel Samepage Merging，内核相同页合并机制 |

## 5. 共享页类型分析

### 5.1 共享页来源分类

| 来源 | 特征 | 检测方法 |
| --- | --- | --- |
| mmap(MAP_SHARED) | 进程间共享内存 | 解析`/proc/[pid]/maps`中`s`标志 |
| System V shm | `shmget/shmat`创建 | 解析`/proc/[pid]/maps`中shm段 |
| POSIX shm | `/dev/shm/`文件映射 | 解析`/proc/[pid]/maps`路径 |
| tmpfs映射 | tmpfs文件映射 | 解析`/proc/[pid]/maps`路径 |
| KVM共享内存 | QEMU `memAccess='shared'` | 解析libvirt XML |
| Hugepage共享 | 共享大页映射 | 解析`/proc/[pid]/numa_maps` |

### 5.2 共享页风险等级

| 风险等级 | 共享页类型 | 迁移后果 | 过滤必要性 |
| --- | --- | --- | --- |
| **高危** | KVM共享内存 | VM崩溃、数据损坏 | **必须过滤** |
| **高危** | System V shm | 进程通信失败 | **必须过滤** |
| **高危** | POSIX shm | 进程同步失败 | **必须过滤** |
| **中危** | mmap(MAP_SHARED) | 性能下降、一致性风险 | **必须过滤** |
| **低危** | tmpfs只读映射 | 性能下降 | **建议过滤** |

### 5.3 maps文件共享页识别

`/proc/[pid]/maps`文件格式示例：
```
7f8a00000000-7f8a00200000 rw-s 00000000 00:05 1234  /dev/shm/my_shared_mem
```

关键标志解读：
- `s`：共享映射（MAP_SHARED）
- `p`：私有映射（MAP_PRIVATE，默认）
- `/dev/shm/`：POSIX共享内存
- `[shm]`：System V共享内存

## 6. 现有机制分析

### 6.1 现有数据结构

```c
// ActcData - 访存数据结构，包含白名单标记
typedef struct {
    uint64_t addr;
    actc_t freq;
    uint8_t prior;
    bool isWhiteListPage;  // 白名单页面标记
} ActcData;

// ProcessMemBitmap - 进程内存位图，包含白名单位图
struct ProcessMemBitmap {
    pid_t pid;
    size_t nrPages[MAX_NODES];
    size_t len[MAX_NODES];
    unsigned long *data[MAX_NODES];
    unsigned long *whiteListBm[MAX_NODES];  // 白名单位图
    ...
};

// MmapType - 内存映射类型枚举
typedef enum { 
    MMAP_PRIVATE,   // 私有映射
    MMAP_SHARED,    // 共享映射
    NR_MMAP_TYPE 
} MmapType;

// ProcessAttribute - 进程属性，包含映射类型
struct ProcessAttribute {
    ...
    VMPidAttribute vmPidAttr;
    ...
};

struct VMPidAttribute {
    int domainId;        // 虚机domain ID
    MmapType mmapType;  // 内存映射模式
};
```

### 6.2 现有过滤实现

#### 6.2.1 白名单位图解析（manage.c）

```c
// 从内核获取白名单位图
static int ParseWhiteListBitmap(struct ProcessMemBitmap *pmb, char *buf, size_t *offset)
{
    for (int nid = 0; nid < MAX_NODES; nid++) {
        if (pmb->len[nid] == 0) {
            continue;
        }
        ret = memcpy_s(pmb->whiteListBm[nid], bitmapSize * pmb->len[nid], 
                       buf + tmpOffset, bitmapSize * pmb->len[nid]);
        ...
    }
}

// 填充白名单标记到ActcData
static int FillActcByBitmap(ProcessAttr *attr, int nid, struct ProcessMemBitmap *pmb, ...)
{
    unsigned long *whiteListBitmap = pmb->whiteListBm[nid];
    ...
    if (TestBit(acidx, whiteListBitmap)) {
        actc[actcLen].isWhiteListPage = true;  // 标记为白名单页面
    }
}
```

#### 6.2.2 迁移过滤实现（separate_strategy.c）

```c
// 排序时将白名单页面排到最后
static int ActcFreqAscFunc(const void *actc1, const void *actc2)
{
    ActcData *a1 = (ActcData *)actc1, *a2 = (ActcData *)actc2;
    if (a1->isWhiteListPage != a2->isWhiteListPage) {
        return a1->isWhiteListPage ? 1 : -1;  // 白名单排后面
    }
    // 然后按频率排序...
}

// 收集页面时跳过白名单页面
static void CollectPages(const SelectionMode mode, ..., ActcData *currentData, ...)
{
    for (size_t i = offset; i < actcLen && collected_count < nrMig; ++i) {
        ...
        if (shouldTake && !currentData[i].isWhiteListPage) {  // 跳过白名单
            currMlist->addr[collected_count++] = currentData[i].addr;
            ...
        }
    }
}

// 构建迁移列表时统计bucket时跳过白名单
static int BuildSelectKMlistAddr(ProcessAttr *process, ...)
{
    ...
    for (uint64_t i = offset; i < n; ++i) {
        if (currentData[i].isWhiteListPage) {
            continue;  // 跳过白名单页面
        }
        buckets[freq]++;
    }
}
```

#### 6.2.3 虚机共享内存识别（manage.c）

```c
// 解析VM XML识别共享内存模式
static int ParseMmapType(int domainId, MmapType *mmapType)
{
    xml = GetXMLByDomainId(domainId);
    if (strstr(xml, "memAccess='shared'") || 
        strstr(xml, "access mode='shared'")) {
        *mmapType = MMAP_SHARED;
    }
    return 0;
}

// 迁移时根据mmapType决策
static int PreMigration(...)
{
    ...
    isForcedSingleThread = isForcedSingleThread || current->vmPidAttr.mmapType;
    ...
}
```

### 6.3 现有机制不足

1. **内核白名单来源不透明**：`whiteListBm`由内核扫描模块填充，用户态无法控制
2. **虚机共享页处理不完善**：仅识别共享模式，未完全过滤共享页
3. **4K进程无共享页识别**：`ParseMmapType`仅针对虚机场景
4. **排序逻辑不够严格**：白名单页面仍可能被选中迁移

## 7. 功能设计

### 7.1 设计方案概述

```
┌─────────────────────────────────────────────────────────────────┐
│                      共享页过滤功能架构                           │
│                    （内部自动执行，无新增接口）                     │
└─────────────────────────────────────────────────────────────────┘
                              │
        ┌─────────────────────┼─────────────────────┐
        │                     │                     │
        ▼                     ▼                     ▼
┌──────────────┐    ┌──────────────┐    ┌──────────────┐
│ 共享页识别    │    │ 白名单管理    │    │ 迁移过滤     │
│（内部函数）   │    │（内部函数）   │    │（自动执行）   │
│              │    │              │    │              │
│ - maps解析   │    │ - 位图维护   │    │ - 排序过滤   │
│ - numa_maps  │    │ - 自动添加   │    │ - 选择跳过   │
│ - XML解析    │    │ - 状态同步   │    │ - 结果验证   │
└──────────────┘    └──────────────┘    └──────────────┘
        │                     │                     │
        └─────────────────────┼─────────────────────┘
                              │
                              ▼
                    ┌──────────────┐
                    │  内核驱动     │
                    │              │
                    │ - 位图标记   │
                    │ - 扫描同步   │
                    └──────────────┘
```

### 7.2 共享页识别设计

#### 7.2.1 识别流程

```
┌─────────────────────────────────────────────────────────────────┐
│                      共享页识别流程                               │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│ Step 1: 进程类型判断                                             │
│ - VM_TYPE: 使用VM共享内存识别流程                                │
│ - PROCESS_TYPE: 使用4K进程共享页识别流程                         │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│ Step 2a: VM共享内存识别（2M大页场景）                            │
│ - 调用ParseMmapType解析libvirt XML                               │
│ - 检测memAccess='shared'或access mode='shared'                   │
│ - 设置process->vmPidAttr.mmapType = MMAP_SHARED                  │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│ Step 2b: 4K进程共享页识别                                        │
│ - 解析/proc/[pid]/maps                                          │
│ - 检测'map_shared'标志                                          │
│ - 检测/dev/shm/、[shm]等共享内存路径                             │
│ - 检测tmpfs文件映射                                              │
│ - 收集共享页地址范围                                             │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│ Step 3: 共享页地址收集                                           │
│ - 记录共享页的虚拟地址范围                                       │
│ - 转换为物理页编号                                               │
│ - 添加到白名单位图                                               │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│ Step 4: 白名单位图同步                                           │
│ - 更新ProcessMemBitmap.whiteListBm                              │
│ - 通过ioctl同步到内核驱动                                        │
│ - 设置ActcData.isWhiteListPage标记                               │
└─────────────────────────────────────────────────────────────────┘
```

#### 7.2.2 4K进程共享页识别（内部函数）

共享页识别在进程纳管时自动执行，不暴露对外接口：

```c
/**
 * @brief 识别4K进程的共享页（内部函数）
 *
 * @param pid 进程ID
 * @param sharedAddrRanges 共享页地址范围数组（输出）
 * @param maxRanges 最大地址范围数量
 * @param actualRanges 实际地址范围数量（输出）
 *
 * @return 0成功，非0失败
 * @note 此函数在ProcessAddManage内部调用，无需用户手动调用
 */
static int IdentifySharedPages4K(pid_t pid, struct AddrRange *sharedAddrRanges,
                          int maxRanges, int *actualRanges);

struct AddrRange {
    uint64_t start;  // 起始虚拟地址
    uint64_t end;    // 结束虚拟地址
    uint64_t flags;  // 映射标志
    char path[PATH_MAX];  // 映射路径
};
```

#### 7.2.3 maps解析实现

```c
static int ParseMapsForSharedPages(pid_t pid, struct AddrRange *ranges, 
                                    int maxRanges, int *actualRanges)
{
    char mapsPath[64];
    snprintf(mapsPath, sizeof(mapsPath), "/proc/%d/maps", pid);
    
    FILE *fp = fopen(mapsPath, "r");
    if (!fp) {
        SMAP_LOGGER_ERROR("Failed to open %s.", mapsPath);
        return -EINVAL;
    }
    
    char line[1024];
    int count = 0;
    
    while (fgets(line, sizeof(line), fp) && count < maxRanges) {
        // 解析maps行格式
        // 格式: address perms offset dev inode pathname
        // 示例: 7f8a00000000-7f8a00200000 rw-s 00000000 00:05 1234 /dev/shm/test
        
        uint64_t start, end;
        char perms[5];
        char path[PATH_MAX] = {0};
        
        sscanf(line, "%llx-%llx %4s %*s %*s %*s %s", &start, &end, perms, path);
        
        // 判断是否为共享页
        bool isShared = false;
        
        // 1. 检测MAP_SHARED标志（perms包含's'）
        if (perms[3] == 's') {
            isShared = true;
            SMAP_LOGGER_INFO("Found MAP_SHARED mapping: %llx-%llx %s", start, end, path);
        }
        
        // 2. 检测共享内存路径
        if (strstr(path, "/dev/shm/") ||      // POSIX shm
            strstr(path, "[shm]") ||          // System V shm
            strncmp(path, "/run/shm/", 9) == 0) {
            isShared = true;
            SMAP_LOGGER_INFO("Found shared memory path: %llx-%llx %s", start, end, path);
        }
        
        // 3. 检测tmpfs映射（可选过滤）
        if (strstr(path, "tmpfs")) {
            isShared = true;
            SMAP_LOGGER_INFO("Found tmpfs mapping: %llx-%llx %s", start, end, path);
        }
        
        if (isShared) {
            ranges[count].start = start;
            ranges[count].end = end;
            ranges[count].flags = perms[3] == 's' ? MAP_SHARED : 0;
            strncpy(ranges[count].path, path, PATH_MAX - 1);
            count++;
        }
    }
    
    fclose(fp);
    *actualRanges = count;
    SMAP_LOGGER_INFO("Pid %d has %d shared memory ranges.", pid, count);
    return 0;
}
```

### 7.3 白名单管理设计

#### 7.3.1 白名单位图更新（内部函数）

白名单管理在进程纳管和扫描阶段自动执行，不暴露对外接口：

```c
/**
 * @brief 添加共享页到白名单（内部函数）
 *
 * @param pid 进程ID
 * @param sharedAddrRanges 共享页地址范围数组
 * @param numRanges 地址范围数量
 *
 * @return 0成功，非0失败
 * @note 此函数在IdentifySharedPages4K后自动调用
 */
static int AddSharedPagesToWhiteList(pid_t pid, struct AddrRange *sharedAddrRanges,
                               int numRanges);

/**
 * @brief 清除进程白名单（内部函数）
 *
 * @param pid 进程ID
 *
 * @return 0成功，非0失败
 * @note 进程移除管理时自动调用
 */
static int ClearProcessWhiteList(pid_t pid);
```

#### 7.3.2 白名单位图操作实现

```c
static int UpdateWhiteListBitmap(struct ProcessMemBitmap *pmb, 
                                  struct AddrRange *ranges, int numRanges)
{
    int nrLocalNuma = GetNrLocalNuma();
    uint64_t pageSize = GetPageSize();
    
    for (int i = 0; i < numRanges; i++) {
        uint64_t start = ranges[i].start;
        uint64_t end = ranges[i].end;
        
        // 将虚拟地址范围转换为页索引，并设置白名单位图
        for (uint64_t addr = start; addr < end; addr += pageSize) {
            // 需要通过pagemap或内核获取物理页信息
            // 此处假设内核扫描模块已提供页面NUMA分布
            
            // 为每个NUMA节点的白名单位图设置对应bit
            for (int nid = 0; nid < MAX_NODES; nid++) {
                if (pmb->whiteListBm[nid] && pmb->len[nid] > 0) {
                    size_t pageIndex = addr / pageSize;  // 简化示意
                    SetBit(pageIndex, pmb->whiteListBm[nid]);
                }
            }
        }
    }
    
    SMAP_LOGGER_INFO("Updated white list for %d shared page ranges.", numRanges);
    return 0;
}
```

### 7.4 迁移过滤设计

#### 7.4.1 过滤策略

共享页过滤采用"先标记后过滤"策略：
1. **标记阶段**：在扫描阶段识别共享页并标记到白名单位图
2. **排序阶段**：排序时将白名单页面排到末尾，确保优先选择非共享页
3. **选择阶段**：选择迁移页面时跳过所有白名单页面
4. **验证阶段**：迁移执行前再次验证目标页面不在白名单中

#### 7.4.2 排序过滤优化

```c
// 排序函数：严格将白名单页面排到最后
static int ActcFreqAscFuncWithWhiteList(const void *actc1, const void *actc2)
{
    ActcData *a1 = (ActcData *)actc1, *a2 = (ActcData *)actc2;
    
    // 白名单页面排到最后，绝对不参与排序选择
    if (a1->isWhiteListPage && !a2->isWhiteListPage) {
        return 1;  // a1排后面
    }
    if (!a1->isWhiteListPage && a2->isWhiteListPage) {
        return -1; // a2排后面
    }
    if (a1->isWhiteListPage && a2->isWhiteListPage) {
        return 0;  // 都是白名单，保持原顺序
    }
    
    // 非白名单页面按频率排序
    if (a1->freq < a2->freq) {
        return -1;
    } else if (a1->freq > a2->freq) {
        return 1;
    } else {
        return (int)a2->prior - (int)a1->prior;
    }
}
```

#### 7.4.3 迁移执行前验证

```c
// 迁移执行前验证页面是否在白名单
static int VerifyMigratePagesNotInWhiteList(struct MigList *mlist, 
                                             unsigned long *whiteListBm)
{
    for (uint32_t i = 0; i < mlist->nr; i++) {
        uint64_t addr = mlist->addr[i];
        size_t pageIndex = addr / GetPageSize();
        
        if (TestBit(pageIndex, whiteListBm)) {
            SMAP_LOGGER_ERROR("Page addr %llx is in white list, skip migration!", addr);
            return -EINVAL;
        }
    }
    return 0;
}
```

## 8. 关键数据结构

### 8.1 共享页识别结果结构

```c
/**
 * @brief 共享页识别结果
 */
struct SharedPageInfo {
    pid_t pid;                      // 进程ID
    uint64_t totalSharedPages;      // 共享页总数
    uint64_t sharedPagesPerNode[MAX_NODES]; // 各NUMA共享页数
    struct AddrRange ranges[MAX_SHARED_RANGES]; // 地址范围
    int numRanges;                  // 地址范围数量
};

#define MAX_SHARED_RANGES 256  // 最大共享内存段数量
```

### 8.2 白名单统计结构（内部使用）

```c
/**
 * @brief 白名单统计信息（内部调试使用）
 * @note 用于内部日志和debugfs输出，不暴露对外接口
 */
struct WhiteListStats {
    uint64_t totalPages;            // 总页面数
    uint64_t whiteListPages;        // 白名单页面数
    uint64_t whiteListPagesPerNode[MAX_NODES]; // 各NUMA白名单页数
    uint64_t sharedPageCount;       // 共享页数量
    uint64_t filteredCount;         // 本次过滤的迁移页数
};
```

### 8.3 扩展ProcessAttribute结构

```c
struct ProcessAttribute {
    // 现有字段...
    
    // 新增共享页相关字段
    struct SharedPageInfo sharedPageInfo;  // 共享页信息
    bool sharedPagesIdentified;             // 共享页识别完成标记
    uint64_t whiteListPageCount;            // 白名单页面计数
};
```

## 9. 内部流程扩展

本功能不新增对外接口，通过扩展现有内部流程实现共享页自动过滤。

### 9.1 进程纳管流程扩展

在现有`ubturbo_smap_migrate_out`调用流程中，`ProcessAddManage`内部自动执行共享页识别：

```c
// 内部流程扩展（manage.c）
int ProcessAddManage(ProcessParam *param, uint32_t *nodeBitmap)
{
    // 现有逻辑：进程状态初始化、NUMA属性设置等...

    // 新增：自动识别共享页并添加到白名单
    if (param->type == PROCESS_TYPE) {  // 4K进程
        struct AddrRange sharedRanges[MAX_SHARED_RANGES];
        int actualRanges = 0;

        int ret = IdentifySharedPages4K(param->pid, sharedRanges,
                                        MAX_SHARED_RANGES, &actualRanges);
        if (ret == 0 && actualRanges > 0) {
            // 自动添加共享页到白名单
            AddSharedPagesToWhiteList(param->pid, sharedRanges, actualRanges);
            SMAP_LOGGER_INFO("Pid %d: identified %d shared ranges, added to white list.",
                             param->pid, actualRanges);
        } else if (ret != 0) {
            SMAP_LOGGER_WARNING("Identify shared pages for pid %d failed: %d.",
                                param->pid, ret);
            // 不阻止进程添加，但记录警告
        }
    } else if (param->type == VM_TYPE) {  // 2M虚拟机
        // 复用现有的ParseMmapType识别VM共享内存模式
        MmapType mmapType;
        if (ParseMmapType(param->vmPidAttr.domainId, &mmapType) == 0) {
            param->vmPidAttr.mmapType = mmapType;
            if (mmapType == MMAP_SHARED) {
                SMAP_LOGGER_INFO("VM domain %d uses shared memory mode, will filter shared pages.",
                                 param->vmPidAttr.domainId);
            }
        }
    }

    // 现有逻辑：进程添加到管理列表...
}
```

### 9.2 进程移除流程扩展

进程移除管理时自动清理白名单：

```c
// 内部流程扩展（manage.c）
int ProcessRemoveManage(pid_t pid)
{
    // 现有逻辑...

    // 新增：清除白名单
    ClearProcessWhiteList(pid);

    // 现有逻辑...
}
```

### 9.3 迁移流程自动过滤

迁移时自动跳过白名单页面，无需用户干预：

```c
// 现有流程（separate_strategy.c），无需修改
// 排序时将白名单页面排到最后
static int ActcFreqAscFunc(const void *actc1, const void *actc2)
{
    ActcData *a1 = (ActcData *)actc1, *a2 = (ActcData *)actc2;
    if (a1->isWhiteListPage != a2->isWhiteListPage) {
        return a1->isWhiteListPage ? 1 : -1;  // 白名单排后面
    }
    // 然后按频率排序...
}

// 收集页面时跳过白名单页面
static void CollectPages(const SelectionMode mode, ..., ActcData *currentData, ...)
{
    for (size_t i = offset; i < actcLen && collected_count < nrMig; ++i) {
        ...
        if (shouldTake && !currentData[i].isWhiteListPage) {  // 跳过白名单
            currMlist->addr[collected_count++] = currentData[i].addr;
        }
    }
}
```

### 9.4 用户使用方式

用户无需任何额外操作，现有接口调用流程不变：

```
┌─────────────────────────────────────────────────────────────────┐
│                   用户调用流程（无变化）                           │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│ Step 1: 初始化SMAP                                              │
│ ubturbo_smap_start(pageType, extlog)                           │
│ └───────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│ Step 2: 配置进程迁移                                            │
│ ubturbo_smap_migrate_out(&msg, pidType)                        │
│                                                                 │
│ 【内部自动执行】                                                │
│ ├─ ProcessAddManage内部调用IdentifySharedPages4K               │
│ ├─ 自动识别共享页（mmap/shm/tmpfs等）                           │
│ └─ 自动添加到白名单whiteListBm                                  │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│ Step 3: 迁移执行（异步）                                        │
│ SMAP后台线程周期性扫描并迁移冷页                                │
│                                                                 │
│ 【内部自动过滤】                                                │
│ ├─ 排序时白名单页面排末尾                                       │
│ ├─ 选择时跳过白名单页面                                         │
│ └─ 仅迁移私有冷页                                               │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│ Step 4: 移除进程管理                                            │
│ ubturbo_smap_remove(&msg, pidType)                             │
│                                                                 │
│ 【内部自动清理】                                                │
│ └─ ClearProcessWhiteList清除白名单                              │
└─────────────────────────────────────────────────────────────────┘
```

## 10. 内部校验规则

### 10.1 共享页识别校验

| 校验项 | 校验规则 | 处理方式 |
| --- | --- | --- |
| pid有效性 | pid必须存在且可访问 | 警告日志，继续处理 |
| maps可访问 | `/proc/[pid]/maps`必须可读 | 警告日志，跳过识别 |
| 地址范围有效性 | start < end，且对齐 | 警告日志，跳过该段 |
| 内存段数量限制 | 不超过MAX_SHARED_RANGES(256) | 超出部分忽略 |

### 10.2 白名单操作校验

| 校验项 | 校验规则 | 处理方式 |
| --- | --- | --- |
| 进程已纳管 | pid必须在SMAP管理列表中 | 静默返回 |
| 白名单位图空间 | 位图空间足够容纳新页面 | 动态扩展或截断 |
| 页面地址有效 | 地址必须是进程有效页面 | 无效地址忽略 |

### 10.3 迁移过滤校验

| 校验项 | 校验规则 | 处理方式 |
| --- | --- | --- |
| 白名单页面被选中 | 检测到白名单页面在迁移列表 | 自动跳过并记录日志 |
| 共享页比例过高 | 共享页>50%总页面 | 警告日志，继续执行 |
| 全部为共享页 | 100%为共享页 | INFO日志，迁移量为0 |

## 11. DFX设计

### 11.1 日志设计

#### 11.1.1 共享页识别日志

```c
// 发现共享页
SMAP_LOGGER_INFO("Pid %d: Found shared page at %llx-%llx, type=%s, path=%s",
                 pid, start, end, type_str, path);

// 共享页统计
SMAP_LOGGER_INFO("Pid %d shared page summary: total=%llu, shm=%llu, mmap=%llu, tmpfs=%llu",
                 pid, total, shm_count, mmap_count, tmpfs_count);

// 白名单更新
SMAP_LOGGER_INFO("Pid %d white list updated: added %llu pages, total white list pages=%llu",
                 pid, added, total_white);
```

#### 11.1.2 迁移过滤日志

```c
// 迁移过滤统计
SMAP_LOGGER_INFO("Pid %d migration filtered: requested=%llu, filtered=%llu, actual=%llu",
                 pid, requested, filtered_shared, actual_migrate);

// 白名单页面跳过
SMAP_LOGGER_DEBUG("Skipping white list page %llx for pid %d during migration",
                   addr, pid);
```

### 11.2 状态查询（debugfs）

共享页和白名单状态通过debugfs查看：

```bash
# 查看进程共享页识别状态
cat /sys/kernel/debug/smap/process_shared_info

# 输出示例
Pid: 12345
Shared Pages: 512
White List Pages: 512
Shared Ranges: 3
  Range 0: 7f8a00000000-7f8a00200000 (/dev/shm/my_shm)
  Range 1: 7f8b00000000-7f8b00100000 ([shm])
  Range 2: 7f8c00000000-7f8c00010000 (tmpfs)

# 查看白名单统计
cat /sys/kernel/debug/smap/whitelist_stats

# 输出示例
Total Pages: 10240
White List Pages: 512
Filtered Migrations: 256
Shared Page Filter Rate: 100.00%
```

### 11.3 内部错误处理

| 错误码 | 含义 | 内部处理方式 |
| --- | --- | --- |
| -ESRCH | 进程不存在 | 警告日志，跳过共享页识别 |
| -EACCES | 无法访问进程maps | 警告日志，跳过共享页识别 |
| -EINVAL | 无效地址范围 | 警告日志，跳过该地址段 |
| -ENOMEM | 内存申请失败 | 警告日志，跳过白名单更新 |

以上错误均为内部处理，不影响进程纳管和迁移流程。

### 11.4 性能指标

```bash
# 查看共享页识别性能
cat /sys/kernel/debug/smap/shared_identify_perf

# 输出示例
Avg Identify Time: 25ms
Max Identify Time: 50ms
White List Bitmap Size: 256KB
Filter Overhead: 0.1ms/migration
```

## 12. 测试建议

### 12.1 单元测试

| 测试项 | 测试内容 | 预期结果 |
| --- | --- | --- |
| maps解析测试 | 解析包含MAP_SHARED的maps文件 | 正确识别共享页 |
| shm识别测试 | 解析`/dev/shm/`路径映射 | 正确识别POSIX shm |
| System V shm测试 | 解析`[shm]`段 | 正确识别System V shm |
| 白名单位图操作 | 设置/清除/查询位图 | 位图状态正确 |
| 排序过滤测试 | 包含白名单页面的排序 | 白名单页面排末尾 |
| 迁移过滤测试 | 选择迁移页面时包含白名单 | 白名单页面被跳过 |

### 12.2 功能测试

| 测试场景 | 测试步骤 | 验证点 |
| --- | --- | --- |
| POSIX shm迁移过滤 | 1.创建POSIX shm<br>2.配置迁移<br>3.验证不迁移 | shm页面保留本地 |
| mmap(MAP_SHARED)过滤 | 1.mmap共享内存<br>2.配置迁移<br>3.验证不迁移 | 共享页不迁移 |
| KVM共享内存过滤 | 1.启动共享内存VM<br>2.配置迁移<br>3.验证VM正常 | VM运行正常 |
| 混合页面迁移 | 1.进程有私有页+共享页<br>2.配置迁移<br>3.验证私有页迁移 | 仅私有页迁移 |

### 12.3 可靠性测试

| 测试场景 | 测试条件 | 验证点 |
| --- | --- | --- |
| 共享页数据一致性 | 迁移前后访问共享内存 | 数据一致 |
| 多进程共享页 | 多进程共享同一内存 | 各进程访问正常 |
| VM稳定性 | 共享内存VM迁移过滤 | VM无崩溃无异常 |
| 迁移回滚 | 迁移中途取消 | 状态正确恢复 |

### 12.4 性能测试

| 测试项 | 测试方法 | 验收标准 |
| --- | --- | --- |
| 共享页识别延迟 | 测量单进程识别耗时 | < 50ms |
| 白名单位图开销 | 测量位图内存占用 | < 1MB/进程 |
| 迁移过滤开销 | 对比有无过滤的迁移时间 | 无明显差异 |
| 大量共享页测试 | 进程50%共享页迁移 | 正常完成 |

### 12.5 测试代码示例

```c
// POSIX共享内存迁移过滤测试
// 本功能自动生效，无需额外接口调用
#include <stdio.h>
#include <sys/mman.h>
#include <fcntl.h>
#include "smap_interface.h"

int test_posix_shm_filter(void)
{
    int ret;
    pid_t pid = getpid();

    // 1. 创建POSIX共享内存
    int shm_fd = shm_open("/test_shm", O_CREAT | O_RDWR, 0666);
    ftruncate(shm_fd, 2 * 1024 * 1024);  // 2MB

    void *shm_addr = mmap(NULL, 2 * 1024 * 1024,
                          PROT_READ | PROT_WRITE,
                          MAP_SHARED, shm_fd, 0);

    // 写入测试数据
    sprintf((char *)shm_addr, "Test data for shared memory");

    // 2. 初始化SMAP
    ret = ubturbo_smap_start(0, NULL);
    if (ret != 0) {
        printf("SMAP start failed: %d\n", ret);
        return ret;
    }

    // 3. 配置迁移（共享页识别在ProcessAddManage中自动执行）
    struct MigrateOutMsg msg = {
        .count = 1,
        .payload[0] = {
            .pid = pid,
            .count = 1,
            .inner[0] = {
                .destNid = 4,
                .ratio = 50,
                .migrateMode = MIG_RATIO_MODE,
            },
        },
    };

    ret = ubturbo_smap_migrate_out(&msg, 0);
    if (ret != 0) {
        printf("Migrate out failed: %d\n", ret);
        ubturbo_smap_stop();
        return ret;
    }

    printf("Migration configured. Shared pages will be filtered automatically.\n");

    // 4. 等待迁移周期完成（实际场景中需要等待）
    sleep(5);

    // 5. 验证共享内存数据一致性
    printf("Shared memory content: %s\n", (char *)shm_addr);
    if (strcmp((char *)shm_addr, "Test data for shared memory") == 0) {
        printf("Shared memory data is consistent!\n");
    }

    // 6. 移除进程管理（白名单自动清除）
    struct MigrateOutMsg remove_msg = {
        .count = 1,
        .payload[0] = {.pid = pid},
    };
    ubturbo_smap_remove(&remove_msg, 0);

    // 清理
    munmap(shm_addr, 2 * 1024 * 1024);
    shm_unlink("/test_shm");
    ubturbo_smap_stop();

    return 0;
}
```

## 13. 风险与限制

### 13.1 功能限制

| 限制项 | 限制内容 | 原因 |
| --- | --- | --- |
| 识别时机 | 仅在进程添加时识别 | 动态识别开销大 |
| 地址精度 | 按页面粒度识别 | 子页面级别不支持 |
| 跨进程协调 | 不支持多进程共享页协调 | 数据结构限制 |
| 动态共享页 | 不检测运行时新创建的共享页 | 需要持续监控 |

### 13.2 可靠性风险

| 风险项 | 风险描述 | 缓解措施 |
| --- | --- | --- |
| 识别遗漏 | 部分共享页未被识别 | 多种识别方式互补 |
| 内核同步失败 | 白名单未同步到内核 | 增加重试机制 |
| 迁移误选 | 白名单页面被错误选中 | 迁移前二次验证 |
| 性能影响 | 共享页比例过高影响迁移效果 | 提供统计报告 |

### 13.3 兼容性风险

| 风险项 | 风险描述 | 缓解措施 |
| --- | --- | --- |
| maps格式变化 | 不同内核版本maps格式可能不同 | 兼容性解析 |
| 新共享内存类型 | 新的共享内存机制可能出现 | 模块化识别接口 |
| 进程权限 | 低权限进程无法读取自身maps | 使用内核扫描 |

## 14. 依赖与影响

### 14.1 内部依赖

| 依赖模块 | 依赖内容 |
| --- | --- |
| ProcessManager | 进程管理状态维护 |
| ScanAttribute | 扫描数据和白名单标记 |
| StrategyAttribute | 迁移策略计算 |
| 内核扫描模块 | 白名单位图生成 |

### 14.2 外部依赖

| 依赖项 | 依赖内容 |
| --- | --- |
| `/proc/[pid]/maps` | 进程内存映射信息 |
| `/proc/[pid]/numa_maps` | NUMA内存分布 |
| libvirt XML | VM共享内存配置 |
| 内核白名单接口 | ioctl同步机制 |

### 14.3 对其他模块的影响

| 影响模块 | 影响内容 |
| --- | --- |
| 迁移策略 | 需考虑白名单页面比例 |
| 统计模块 | 需记录过滤统计 |
| 日志模块 | 需支持共享页相关日志 |
| 监控系统 | 需支持共享页监控指标 |

## 15. 附录

### 15.1 /proc/[pid]/maps格式说明

```
地址范围           权限  偏移    设备   inode   路径
7f8a00000000-7f8a00200000 rw-s 00000000 00:05 1234 /dev/shm/test
7f8a00200000-7f8a00400000 rw-p 00000000 00:00 0    [heap]
7f8b00000000-7f8b00100000 rw-s 00000000 00:00 0    [shm]

权限字段说明：
r - 可读
w - 可写
x - 可执行
s - 共享映射(MAP_SHARED)
p - 私有映射(MAP_PRIVATE)
```

### 15.2 共享内存类型特征汇总

| 类型 | maps标志 | 路径特征 | 识别方法 |
| --- | --- | --- | --- |
| mmap(MAP_SHARED) | `s` | 任意 | 权限字段检测 |
| POSIX shm | `s` | `/dev/shm/` | 路径匹配 |
| System V shm | `s` | `[shm]` | 路径匹配 |
| tmpfs | `s` | `tmpfs`关键字 | 路径匹配 |
| KVM shared | N/A | N/A | libvirt XML |

### 15.3 参考资料

- UBTurbo开发者指南：`doc/Developer_Guide.md`
- SMAP接口头文件：`src/user/smap_interface.h`
- 进程管理：`src/user/manage/manage.h`
- 迁移策略：`src/user/strategy/separate_strategy.c`
- Linux proc文件系统文档：`man 5 proc`