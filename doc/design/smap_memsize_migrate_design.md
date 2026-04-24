# SMAP进程按迁移量迁移功能设计文档

## 文档版本
| 版本 | 日期 | 作者 | 描述 |
| --- | --- | --- | --- |
| v1.0 | 2026-04-22 | dszhu | 初稿 |

## 1. 背景

### 1.1 业务背景
腾讯POC场景下，客户存在Redis进程需要精确控制迁移内存量的需求。当前系统仅支持按比例迁移内存到远端NUMA，无法精确指定迁移的具体内存大小。例如，客户需要将Redis进程的500MiB内存精确迁移到指定的远端NUMA节点，而非基于进程总内存的某个百分比。

### 1.2 技术背景
当前UBTurbo SMAP模块已实现两种迁移模式：
- **按比例迁移（MIG_RATIO_MODE）**：基于进程内存总量按百分比迁移，适用于水线场景（WATERLINE_MODE）
- **按内存大小迁移（MIG_MEMSIZE_MODE）**：基于指定内存大小迁移，适用于内存池化场景（MEM_POOL_MODE）

现有`ubturbo_smap_migrate_out`和`ubturbo_smap_migrate_out_sync`接口已支持通过`migrateMode`字段区分迁移模式，并通过`memSize`字段指定迁移内存大小（单位KB）。

### 1.3 问题分析
当前按内存大小迁移功能仅支持虚拟化2M大页场景，且主要面向内存池化业务场景。对于通算场景下4K页的Redis进程，缺乏以下能力：
1. 精确按内存大小迁移的支持
2. 迁移进度实时反馈
3. 迁移失败的精细化处理

## 2. 目标

### 2.1 功能目标
1. **扩展迁移场景支持**：将按内存大小迁移功能从虚拟化2M大页场景扩展至通算4K页场景，支持Redis等通算进程
2. **复用现有接口**：复用`ubturbo_smap_start`初始化流程及`ubturbo_smap_migrate_out`迁移配置接口，降低开发成本
3. **精确迁移控制**：支持按MiB为单位指定迁移内存量，确保迁移量精确可控
4. **迁移进度反馈**：提供迁移完成状态的查询能力

### 2.2 性能目标
| 指标 | 目标值 |
| --- | --- |
| 迁移延迟 | 异步迁移模式下，单次迁移周期内完成 |
| 接口响应时间 | 接口调用返回时间 < 100ms |
| 迁移成功率 | 正常情况下迁移成功率 > 95% |

### 2.3 可靠性目标
- 迁移失败时返回明确错误码，便于上层组件进行错误处理
- 支持迁移过程中进程异常退出后的状态恢复

## 3. 非目标

1. **不支持实时迁移进度百分比查询**：本次设计不提供迁移进度百分比接口，仅提供迁移完成状态
2. **不支持迁移过程中动态调整迁移量**：一旦配置迁移量，不支持运行时动态修改
3. **不支持跨代际迁移**：仅支持同代际（HCCS或UB）内的远端NUMA迁移
4. **不支持并发迁移配置**：同一进程不支持同时配置多个远端NUMA的迁移

## 4. 术语

| 术语 | 定义 |
| --- | --- |
| SMAP | Smart Memory Access Pattern，UBTurbo内存冷热流动管理模块 |
| L1 NUMA | 本地NUMA节点，即处理器直接访问的近端内存节点 |
| L2 NUMA | 远端NUMA节点，通过HCCS或UB互联的远端内存节点 |
| 水线场景 | WATERLINE_MODE，基于内存借用水线的动态迁移场景 |
| 内存池化场景 | MEM_POOL_MODE，基于固定内存大小的静态迁移场景 |
| MIG_RATIO_MODE | 按比例迁移模式，基于进程内存百分比迁移 |
| MIG_MEMSIZE_MODE | 按内存大小迁移模式，基于指定内存大小迁移 |
| 冷页 | 访存频次低于阈值的内存页，适合迁移到远端NUMA |
| 热页 | 访存频次高于阈值的内存页，适合保留在本地NUMA |

## 5. 接口设计

### 5.1 接口概述

本功能复用现有接口体系，通过扩展参数支持实现新功能：

| 接口 | 功能 | 使用方式 |
| --- | --- | --- |
| `ubturbo_smap_start` | 初始化SMAP模块 | 初始化时设置pageType=0（4K页模式） |
| `ubturbo_smap_run_mode_set` | 设置运行模式 | 设置为MEM_POOL_MODE（内存池化场景） |
| `ubturbo_smap_migrate_out` | 配置进程迁移 | 设置migrateMode=MIG_MEMSIZE_MODE，指定memSize |
| `ubturbo_smap_remove` | 移除进程管理 | 迁移完成后移除进程 |

### 5.2 接口调用流程

```
┌─────────────────────────────────────────────────────────────┐
│                    Redis进程迁移流程                          │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│ Step 1: 初始化SMAP                                            │
│ ubturbo_smap_start(pageType=0, extlog)                       │
│ pageType=0 表示4K页模式                                       │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│ Step 2: 设置运行模式                                          │
│ ubturbo_smap_run_mode_set(MEM_POOL_MODE)                     │
│ 必须设置为内存池化场景才能使用按大小迁移                       │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│ Step 3: 配置Redis进程迁移                                     │
│ ubturbo_smap_migrate_out(&msg, pidType=0)                    │
│ msg.payload.inner.migrateMode = MIG_MEMSIZE_MODE             │
│ msg.payload.inner.memSize = 512 * 1024  // 512MiB = 524288KB │
│ msg.payload.inner.destNid = 远端NUMA ID                       │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│ Step 4: 迁移执行（异步）                                      │
│ SMAP后台线程周期性扫描并迁移冷页                              │
│ 迁移量达到memSize后停止迁移                                   │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│ Step 5: 查询迁移状态                                          │
│ ubturbo_smap_process_config_query(nid, result, inLen, outLen)│
│ 查询进程状态是否为PROC_IDLE（迁移完成）                       │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│ Step 6: 移除进程管理                                          │
│ ubturbo_smap_remove(&msg, pidType=0)                         │
│ 完成迁移后根据业务需要移除进程                                │
└─────────────────────────────────────────────────────────────┘
```

### 5.3 接口参数详解

#### 5.3.1 ubturbo_smap_start参数

| 参数 | 类型 | 有效值 | 描述 |
| --- | --- | --- | --- |
| pageType | uint32_t | 0 | 4K页模式，适用于Redis等通算进程 |
| extlog | Logfunc | 函数指针或NULL | 日志回调函数 |

#### 5.3.2 ubturbo_smap_run_mode_set参数

| 参数 | 类型 | 有效值 | 描述 |
| --- | --- | --- | --- |
| runMode | int | 1 (MEM_POOL_MODE) | 内存池化场景，必须设置为此模式 |

#### 5.3.3 ubturbo_smap_migrate_out参数（MigrateOutMsg）

```c
struct MigrateOutMsg {
    int count;                      // 配置进程数量，有效值[1, 40]
    struct MigrateOutPayload payload[MAX_NR_MIGOUT];
};

struct MigrateOutPayload {
    int srcNid;                     // 源NUMA，-1表示不指定
    pid_t pid;                      // Redis进程PID
    int count;                      // 远端NUMA配置数量，有效值[1, REMOTE_NUMA_NUM]
    struct MigrateOutPayloadInner inner[REMOTE_NUMA_NUM];
};

struct MigrateOutPayloadInner {
    int destNid;                    // 目标远端NUMA ID
    int ratio;                      // 比例模式下使用，大小模式下忽略
    uint64_t memSize;               // 迁移内存大小，单位KB
    MigrateMode migrateMode;        // 迁移模式：MIG_MEMSIZE_MODE(1)
};
```

#### 5.3.4 关键参数说明

| 参数 | 单位 | 约束 |
| --- | --- | --- |
| memSize | KB | 必须为2MB的整数倍（2048KB），因为内核迁移以2MB页为单位 |
| destNid | NUMA ID | 必须为有效的远端NUMA（>=nrLocalNuma且<nrLocalNuma+REMOTE_NUMA_NUM） |
| pid | 进程ID | 进程必须存在且不在SMAP黑名单中 |

## 6. 参数校验规则

### 6.1 初始化阶段校验

| 校验项 | 校验规则 | 错误码 |
| --- | --- | --- |
| pageType有效性 | pageType必须为0（4K页）或1（2M页） | -EINVAL |
| 页面类型匹配 | pageType必须与当前内核驱动配置匹配 | -EINVAL |
| 内核驱动存在 | /dev/smap_*设备文件必须存在 | -ENODEV |
| 重复初始化 | 同进程不允许重复初始化 | -EPERM |
| 跨进程初始化 | 已有其他进程初始化时不允许再次初始化 | -EPERM |

### 6.2 运行模式设置校验

| 校验项 | 校验规则 | 错误码 |
| --- | --- | --- |
| runMode有效性 | runMode必须为0（水线）或1（内存池化） | -EINVAL |
| 页面类型兼容 | 4K页场景不支持设置为内存池化模式（需扩展支持） | -EINVAL |
| SMAP运行状态 | SMAP必须已初始化 | -EPERM |

### 6.3 迁移配置阶段校验

| 校验项 | 校验规则 | 错误码 |
| --- | --- | --- |
| msg非空 | msg指针不能为NULL | -EINVAL |
| count范围 | count必须在[1, MAX_NR_MIGOUT]范围内 | -EINVAL |
| pid有效性 | pid必须为有效进程（进程存在） | -ESRCH（部分不存在） |
| pid重复 | 同一批次不允许重复PID | -EINVAL |
| pid黑名单 | 进程名不能在SMAP黑名单中 | -EINVAL |
| pid数量上限 | 配置后进程总数不能超过MAX_4K_PROCESSES_CNT(300) | -EINVAL |
| destNid有效性 | destNid必须是有效的远端NUMA | -EINVAL |
| destNid禁用状态 | destNid不能处于禁用状态 | -EINVAL |
| destNid唯一性 | 同一PID的多个远端NUMA不能重复 | -EINVAL |
| migrateMode有效性 | migrateMode必须为MIG_RATIO_MODE(0)或MIG_MEMSIZE_MODE(1) | -EINVAL |
| 运行模式兼容 | MEM_POOL_MODE下只能使用MIG_MEMSIZE_MODE | -EINVAL |
| memSize对齐 | memSize必须为KB_PER_2MB(2048)的整数倍 | -EINVAL |
| memSize非零 | memSize必须大于0 | -EINVAL |

### 6.4 校验代码参考

```c
// memSize对齐校验（来自smap_interface.c）
if (payload->inner[i].migrateMode == MIG_MEMSIZE_MODE && 
    payload->inner[i].memSize % KB_PER_2MB != 0) {
    SMAP_LOGGER_ERROR("[%d] pid: %d memSize %llu is not 2M aligned.", 
                      i, payload->pid, payload->inner[i].memSize);
    return false;
}

// 运行模式兼容校验
if (GetRunMode() == MEM_POOL_MODE && 
    payload->inner[i].migrateMode != MIG_MEMSIZE_MODE) {
    SMAP_LOGGER_ERROR("[%d] smap runMode is MEM_POOL_MODE, "
                      "not supported mode except MIG_MEMSIZE_MODE.", i);
    return false;
}
```

## 7. 关键数据结构

### 7.1 进程属性结构（ProcessAttr）

```c
struct ProcessAttribute {
    PidType type;                      // 进程类型：PROCESS_TYPE(0)/VM_TYPE(1)
    pid_t pid;                         // 进程PID
    enum ProcessState state;           // 进程状态
    uint32_t scanTime;                 // 扫描周期
    MigrateMode migrateMode;           // 迁移模式
    int initLocalMemRatio;             // 本地内存保留比例
    int remoteNumaCnt;                 // 远端NUMA数量
    bool isLowMem;                     // 目的端内存不足标志
    struct {
        int nid;                       // 远端NUMA ID
        uint64_t memSize;              // 迁移内存大小（KB）
    } migrateParam[REMOTE_NUMA_NUM];   // 迁移参数数组
    NumaAttribute numaAttr;            // NUMA属性
    StrategyAttribute strategyAttr;    // 策略属性
    ScanAttribute scanAttr;            // 扫描属性
};
```

### 7.2 策略属性结构（StrategyAttribute）

```c
typedef struct {
    double initRemoteMemRatio[LOCAL_NUMA_NUM][REMOTE_NUMA_NUM]; // 接口设置的内存比例
    uint64_t memSize[LOCAL_NUMA_NUM][REMOTE_NUMA_NUM];          // 迁移内存大小（KB）
    uint32_t allocRemoteNrPages[LOCAL_NUMA_NUM][REMOTE_NUMA_NUM]; // 远端page数量
    uint32_t nrMigratePages[MAX_NODES][MAX_NODES];              // 迁移页面数
    MigrateDirection dir[MAX_NODES];                             // 迁移方向
} StrategyAttribute;
```

### 7.3 进程状态枚举（ProcessState）

```c
enum ProcessState {
    PROC_IDLE,      // 空闲状态，迁移完成或未开始
    PROC_MIGRATE,   // 冷热迁移进行中
    PROC_BACK,      // 迁回状态
    PROC_MOVE,      // 逃生状态（远端NUMA迁移）
};
```

### 7.4 迁移模式枚举（MigrateMode）

```c
typedef enum {
    MIG_RATIO_MODE = 0,     // 按比例迁移模式
    MIG_MEMSIZE_MODE,       // 按内存大小迁移模式
} MigrateMode;
```

### 7.5 运行模式枚举（RunMode）

```c
typedef enum {
    WATERLINE_MODE = 0,     // 水线场景（按比例）
    MEM_POOL_MODE,          // 内存池化场景（按大小）
    MAX_RUN_MODE,
} RunMode;
```

### 7.6 配置持久化结构

配置信息持久化存储于`/dev/shm/smap_config`文件，用于进程异常退出后状态恢复：

```c
struct ProcessPayload {
    pid_t pid;
    uint8_t scanType;
    uint8_t type;
    uint8_t state;
    uint8_t migrateMode;         // 迁移模式
    uint32_t numaNodes;
    uint32_t scanTime;
    int count;
    struct {
        int nid;
        uint8_t ratio;
        uint64_t memSize;        // 迁移内存大小
    } migrateParam[REMOTE_NUMA_NUM];
};
```

## 8. DFX设计

### 8.1 日志设计

#### 8.1.1 日志级别

| 级别 | 用途 | 示例场景 |
| --- | --- | --- |
| INFO | 正常流程日志 | 迁移配置成功、迁移开始、迁移完成 |
| WARNING | 警告信息 | 进程不存在但继续处理其他进程 |
| ERROR | 错误信息 | 参数校验失败、迁移失败、内存申请失败 |
| DEBUG | 调试信息 | 详细参数值、内部状态变化 |

#### 8.1.2 关键日志点

```c
// 配置迁移日志
SMAP_LOGGER_INFO("mig out msg num:[%d] pid:%d, destNid:%d, ratio:%d, "
                 "memSize:%llu, migMode:%d.", j, pid, destNid, ratio, memSize, migrateMode);

// 迁移完成日志
SMAP_LOGGER_INFO("Process %d migration completed, migrated %llu KB.", pid, actualMigratedSize);

// 迁移失败日志
SMAP_LOGGER_ERROR("Migration failed for pid %d, error: %d.", pid, errno);
```

### 8.2 状态查询

#### 8.2.1 进程配置查询

通过`ubturbo_smap_process_config_query`接口查询指定远端NUMA上的进程配置：

```c
int ubturbo_smap_process_config_query(int nid, struct OldProcessPayload *result, 
                                       int inLen, int *outLen);

// 返回结果包含：
// - pid: 进程ID
// - state: 进程状态（PROC_IDLE表示迁移完成）
// - memSize: 配置的迁移大小
// - migrateMode: 迁移模式
```

#### 8.2.2 运行状态查询

通过`ubturbo_smap_is_running`查询SMAP模块运行状态：

```c
bool ubturbo_smap_is_running(void);
// 返回true表示SMAP正在运行，false表示已停止
```

### 8.3 错误码定义

| 错误码 | 宏定义 | 含义 | 处理建议 |
| --- | --- | --- | --- |
| -1 | -EPERM | SMAP未初始化或已停止 | 先调用ubturbo_smap_start初始化 |
| -3 | -ESRCH | 进程不存在 | 检查进程是否存活 |
| -6 | -ENXIO | 远端NUMA不匹配 | 检查进程当前远端NUMA配置 |
| -9 | -EBADF | 内核调用失败 | 检查内核驱动状态 |
| -11 | -EAGAIN | 超时 | 等待后重试 |
| -12 | -ENOMEM | 内存申请失败 | 检查系统内存状态 |
| -16 | -EBUSY | 迁移超时 | 增加等待时间或检查远端内存 |
| -19 | -ENODEV | 内核驱动未安装 | 安装SMAP内核驱动 |
| -22 | -EINVAL | 参数错误 | 检查参数有效性 |

### 8.4 性能指标

可通过以下方式监控迁移性能：

```bash
# 查看迁移统计信息
cat /sys/kernel/debug/smap/migrate_stats

# 查看进程状态
cat /sys/kernel/debug/smap/process_info

# 查看NUMA内存分布
numastat -p <pid>
```

## 9. 测试建议

### 9.1 单元测试

| 测试项 | 测试内容 | 预期结果 |
| --- | --- | --- |
| 参数校验测试 | 无效memSize（非2MB对齐） | 返回-EINVAL |
| 参数校验测试 | 无效destNid | 返回-EINVAL |
| 参数校验测试 | 无效migrateMode | 返回-EINVAL |
| 参数校验测试 | NULL msg指针 | 返回-EINVAL |
| 状态校验测试 | SMAP未初始化时调用 | 返回-EPERM |
| 边界值测试 | memSize=0 | 返回-EINVAL |
| 边界值测试 | memSize=最大值 | 正常处理 |

### 9.2 功能测试

| 测试场景 | 测试步骤 | 验证点 |
| --- | --- | --- |
| Redis进程迁移 | 1.启动Redis进程<br>2.初始化SMAP<br>3.配置迁移<br>4.验证迁移完成 | 迁移内存量与配置一致 |
| 多进程迁移 | 配置多个Redis进程同时迁移 | 各进程独立完成迁移 |
| 进程异常退出 | 迁移过程中杀死进程 | SMAP正确处理异常 |
| SMAP重启 | 迁移过程中重启SMAP | 状态正确恢复 |

### 9.3 性能测试

| 测试项 | 测试方法 | 验收标准 |
| --- | --- | --- |
| 接口响应时间 | 测量接口调用耗时 | < 100ms |
| 迁移延迟 | 测量迁移开始到完成时间 | < 单个迁移周期 |
| 内存开销 | 测量SMAP内存占用 | < 50MB |
| 并发性能 | 同时配置40个进程迁移 | 无阻塞、无异常 |

### 9.4 压力测试

| 测试场景 | 测试条件 | 验证点 |
| --- | --- | --- |
| 大量迁移 | 连续配置300个进程 | 系统稳定运行 |
| 长时间运行 | 迁移持续24小时 | 无内存泄漏、无崩溃 |
| 快速配置 | 快速反复配置移除 | 状态正确切换 |

### 9.5 测试代码示例

```c
// Redis进程按大小迁移测试示例
#include <stdio.h>
#include "smap_interface.h"

int test_redis_migrate_by_size(void)
{
    int ret;
    pid_t redis_pid = 12345;  // Redis进程PID
    uint64_t migrate_size = 512 * 1024;  // 512MiB = 524288KB
    int dest_nid = 4;  // 目标远端NUMA
    
    // Step 1: 初始化SMAP（4K页模式）
    ret = ubturbo_smap_start(0, NULL);
    if (ret != 0) {
        printf("SMAP start failed: %d\n", ret);
        return ret;
    }
    
    // Step 2: 设置内存池化模式
    ret = ubturbo_smap_run_mode_set(MEM_POOL_MODE);
    if (ret != 0) {
        printf("Set run mode failed: %d\n", ret);
        ubturbo_smap_stop();
        return ret;
    }
    
    // Step 3: 配置迁移
    struct MigrateOutMsg msg = {
        .count = 1,
        .payload[0] = {
            .srcNid = -1,
            .pid = redis_pid,
            .count = 1,
            .inner[0] = {
                .destNid = dest_nid,
                .ratio = 0,  // 大小模式下忽略
                .memSize = migrate_size,
                .migrateMode = MIG_MEMSIZE_MODE,
            },
        },
    };
    
    ret = ubturbo_smap_migrate_out(&msg, 0);  // pidType=0表示4K进程
    if (ret != 0) {
        printf("Migrate out failed: %d\n", ret);
        ubturbo_smap_stop();
        return ret;
    }
    
    printf("Migration configured successfully, size: %llu KB\n", migrate_size);
    
    // Step 4: 停止SMAP（可选）
    ubturbo_smap_stop();
    
    return 0;
}
```

## 10. 风险与限制

### 10.1 功能限制

| 限制项 | 限制内容 | 原因 |
| --- | --- | --- |
| 页面粒度 | memSize必须为2MB整数倍 | 内核以2MB页为单位迁移 |
| 场景限制 | 4K页场景需扩展支持MEM_POOL_MODE | 当前实现仅支持2M页场景 |
| NUMA范围 | destNid必须是有效的远端NUMA | 系统架构限制 |
| 进程数量 | 最大支持300个4K进程 | 内存管理能力限制 |
| 并发限制 | 不支持同进程多远端NUMA并发配置 | 数据结构限制 |

### 10.2 性能风险

| 风险项 | 风险描述 | 缓解措施 |
| --- | --- | --- |
| 迁移延迟 | 大量迁移可能增加延迟 | 控制单次配置进程数量 |
| 内存压力 | 迁移过程中需要临时内存 | 预留足够远端内存 |
| 进程影响 | 迁移期间可能影响进程性能 | 选择低负载时段迁移 |

### 10.3 可靠性风险

| 风险项 | 风险描述 | 缓解措施 |
| --- | --- | --- |
| 进程退出 | 迁移中进程异常退出 | SMAP自动清理，恢复配置 |
| 远端内存不足 | 远端NUMA内存不足 | 提前检查内存可用量 |
| 内核异常 | 内核迁移失败 | 返回错误码，支持重试 |
| 配置丢失 | SMAP异常退出 | 配置持久化到/dev/shm/smap_config |

### 10.4 兼容性风险

| 风险项 | 风险描述 | 缓解措施 |
| --- | --- | --- |
| 版本兼容 | 新功能依赖特定内核版本 | 明确版本依赖要求 |
| 场景切换 | 水线与内存池化场景切换需清理 | 删除配置文件后重新初始化 |
| 接口变更 | 参数语义变化 | 文档明确说明 |

## 11. 依赖与影响

### 11.1 内部依赖

| 依赖模块 | 依赖内容 |
| --- | --- |
| SMAP内核驱动 | 提供内存迁移底层能力 |
| ProcessManager | 进程管理状态维护 |
| StrategyAttribute | 迁移策略计算 |
| ScanAttribute | 冷热扫描数据 |

### 11.2 外部依赖

| 依赖项 | 依赖内容 |
| --- | --- |
| 内核版本 | 要求特定内核版本支持 |
| 系统内存 | 远端NUMA需有足够可用内存 |
| 进程状态 | 进程需处于稳定运行状态 |

### 11.3 对其他模块的影响

| 影响模块 | 影响内容 |
| --- | --- |
| 内存管理 | 远端NUMA内存使用量变化 |
| 进程调度 | 进程内存访问性能可能变化 |
| 监控系统 | 需支持新的监控指标 |

## 12. 附录

### 12.1 MiB与KB转换关系

| MiB | KB |
| --- | --- |
| 1 MiB | 1024 KB |
| 2 MiB | 2048 KB（最小迁移单位） |
| 100 MiB | 102400 KB |
| 512 MiB | 524288 KB |
| 1 GiB | 1048576 KB |

### 12.2 常见配置示例

#### 12.2.1 迁移256MiB到NUMA4

```c
struct MigrateOutPayloadInner inner = {
    .destNid = 4,
    .ratio = 0,
    .memSize = 256 * 1024,  // 256MiB = 262144KB
    .migrateMode = MIG_MEMSIZE_MODE,
};
```

#### 12.2.2 迁移1GiB到NUMA5

```c
struct MigrateOutPayloadInner inner = {
    .destNid = 5,
    .ratio = 0,
    .memSize = 1024 * 1024,  // 1GiB = 1048576KB
    .migrateMode = MIG_MEMSIZE_MODE,
};
```

### 12.3 参考资料

- UBTurbo开发者指南：`doc/Developer_Guide.md`
- SMAP接口头文件：`src/user/smap_interface.h`
- SMAP配置管理：`src/user/manage/smap_config.h`
- 进程管理：`src/user/manage/manage.h`