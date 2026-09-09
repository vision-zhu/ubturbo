# SMAP API 参考

## 基本约定

接口声明在 `src/user/smap_interface.h`，由 `libsmap.so` 提供。除特别说明外，返回 `0` 表示请求成功，
负 errno 或其他非零值表示失败。调用方必须使用与库同版本的头文件。

- `pageType=0`：4K 普通进程页面。
- `pageType=1`：2M 静态大页虚机。
- 所有 `msg`、数组和输出长度指针必须非空并满足头文件上限。
- “请求成功”不一定代表异步页面迁移已收敛到目标。

## 生命周期

```c
int ubturbo_smap_start(uint32_t pageType, Logfunc extlog);
int ubturbo_smap_stop(void);
bool ubturbo_smap_is_running(void);
```

`start` 初始化日志、配置、设备和工作线程。同一进程不得重复初始化；其他进程占用共享状态时可能返回
`-EACCES`。`extlog` 可以为空；非空回调必须线程安全且不能阻塞 SMAP。

`stop` 结束 SMAP 实例。停止前调用方应阻止新增操作。`is_running` 仅报告库内运行状态，不代表所有
内核模块和远端资源均健康。

## 迁出

```c
int ubturbo_smap_migrate_out(struct MigrateOutMsg *msg, int pageType);
int ubturbo_smap_migrate_out_grouped(struct GroupedMigrateOutMsg *msg,
                                     int pageType);
int ubturbo_smap_migrate_out_sync(struct MigrateOutMsg *msg, int pageType,
                                  uint64_t maxWaitTime);
void ubturbo_smap_urgent_migrate_out(uint64_t size);
```

### 普通迁出消息

`MigrateOutMsg.count` 是 PID 数量，不能超过 `MAX_NR_MIGOUT`。每个
`MigrateOutPayload` 包含 PID 和多个远端目标；`inner[].memSize` 的单位是 KB，迁移模式决定使用比例
还是大小。

- 同一 PID 可以配置多个远端 NUMA。
- 一次调用完整替换该 PID 的普通迁出目标；目标数量为 0 表示清空。
- 普通路径中的 `srcNid` 仅为 ABI 兼容保留，不参与本地 NUMA 选择。
- 容量不足时配置可被保存，但页面按可用容量逐步收敛。

分组迁出使用 `GroupedMigrateOutMsg` 描述 local 集合、目标集合、本地保留水线和远端容量，目前仅用于
接口声明支持的 2M 虚机场景。同步接口的 `maxWaitTime` 单位为 ms：`0` 表示不设等待上限，调用线程
可能长期阻塞；非零值必须在 10000–180000 ms 范围内。

紧急迁出的 `size` 单位为字节。该接口无返回值，不能写成通过返回码判断成功。它以缓解本地内存压力为优先，可能按
`numa_maps` 段收集页面；调用方需在压力解除后恢复常规管理并核对共享页归属。

## 迁回与移除

```c
int ubturbo_smap_migrate_back(struct MigrateBackMsg *msg);
int ubturbo_smap_remove(struct RemoveMsg *msg, int pageType);
```

`MigrateBackMsg` 通过 `taskID` 和不超过 `MAX_NR_MIGBACK` 的地址段描述迁回任务。移除消息以 PID 为
单位；payload 中 NUMA 数量为 0 表示整体删除，大于 0 表示删除指定远端 NUMA 的普通配置。

迁回与移除不是同义操作：迁回改变页面位置，移除改变 SMAP 管理状态。

## NUMA 容量与开关

```c
int ubturbo_smap_remote_numa_info_set(struct SetRemoteNumaInfoMsg *msg);
int ubturbo_smap_node_enable(struct EnableNodeMsg *msg);
int ubturbo_smap_run_mode_set(int runMode);
```

`SetRemoteNumaInfoMsg.size` 表示可使用容量，单位为 MiB。`srcNid=-1` 表示该远端目标可供每个本地
NUMA 使用。容量、拓扑和开关由可信资源管理器维护。

## 进程跟踪

```c
int ubturbo_smap_process_tracking_add(pid_t *pidArr, uint32_t *scanTime,
                                      uint32_t *duration, int len,
                                      int scanType);
int ubturbo_smap_process_tracking_remove(pid_t *pidArr, int len, int flag);
int ubturbo_smap_process_migrate_enable(pid_t *pidArr, int len, int enable,
                                        int flags);
```

三个输入数组的有效元素数均由 `len` 决定。`scanTime` 是扫描间隔，单位为 ms，范围为 50–2000 且
必须是 50 的倍数；`duration` 是统计持续时间，单位为秒，范围为 1–300。PID 退出、重复添加和停止
期间添加都应作为可预期错误处理。

## 查询

```c
int ubturbo_smap_freq_query(int pid, uint16_t *data, uint32_t lengthIn,
                            uint32_t *lengthOut, int dataSource);
int ubturbo_smap_process_config_query(int nid,
                                      struct OldProcessPayload *result,
                                      int inLen, int *outLen);
int ubturbo_smap_remote_numa_freq_query(uint16_t *numa, uint64_t *freq,
                                       uint16_t length);
```

`freq_query` 的 `dataSource` 可选普通管理数据或显式统计数据。输出数组由调用方提供；调用前设置容量，
返回后使用实际长度，不能在失败时读取数组。

`remote_numa_freq_query` 为每个输入 NUMA 写入对应频率，是现有头文件公开接口，旧文档曾遗漏。

## 远端到远端迁移

```c
int ubturbo_smap_remote_numa_migrate(struct MigrateNumaMsg *msg);
int ubturbo_smap_same_remote_numa_migrate(struct MigrateNumaMsg *msg);
int ubturbo_smap_pid_remote_numa_migrate(struct MigrateEscapeMsg *msg);
```

`MigrateNumaMsg` 包含源/目的 NUMA 及不超过 `MAX_NR_MIGNUMA` 的 `memid` 列表。PID 版本使用
`MigrateEscapeMsg` 描述进程、源/目的 NUMA、比例或大小。

`ubturbo_smap_same_remote_numa_migrate` 是当前公开头文件中的独立接口，旧文档曾遗漏；调用方不能用
普通远端迁移接口名替代它。

## 主要上限

| 常量 | 值/来源 | 作用 |
| --- | --- | --- |
| `MAX_NR_MIGRATE_ESCAPE` | 300 | PID 远端迁移 payload 上限 |
| `MAX_NR_MIGBACK` | 50 | 迁回 payload 上限 |
| `MAX_NR_MIGNUMA` | 50 | NUMA 迁移 memid 上限 |
| `MAX_MIGRATION_GROUP_NUM` | 8 | 每个 PID 的迁移分组上限 |
| `MIN_WAIT_TIME` | 10000 ms | 同步等待下限常量 |
| `MAX_WAIT_TIME` | 180000 ms | 同步等待上限常量 |
| `MIN_SCAN_TIME` | 50 ms | 扫描间隔下限常量 |
| `MAX_SCAN_TIME` | 2000 ms | 扫描间隔上限常量 |

其他数组上限由 `smap_env.h` 与 `smap_config.h` 提供，应使用宏而不是在调用方复制数字。

## 最小生命周期示例

```c
#include "smap_interface.h"

int main(void)
{
    int ret = ubturbo_smap_start(0, NULL);
    if (ret != 0) {
        return ret;
    }

    /* Configure authorized NUMA capacity and PIDs here. */

    return ubturbo_smap_stop();
}
```
