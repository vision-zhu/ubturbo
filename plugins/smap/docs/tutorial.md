# SMAP 集成教程

## 最小生命周期

以下流程用于说明调用顺序，不能直接使用示例 PID 或 NUMA 值操作生产工作负载。

```c
#include "smap_interface.h"

static void AppLog(int level, const char *message, const char *module)
{
    /* 将消息接入应用日志；不要记录敏感地址或页面内容。 */
}

int main(void)
{
    const uint32_t page_type = 0; /* 4K process pages */
    if (ubturbo_smap_start(page_type, AppLog) != 0) {
        return 1;
    }

    /* Set remote capacity, add tracking, and configure migration here. */

    if (ubturbo_smap_stop() != 0) {
        return 1;
    }
    return 0;
}
```

## 配置迁出

1. 由可信资源管理器确认 PID、页面类型和允许使用的远端 NUMA。
2. 使用 `ubturbo_smap_remote_numa_info_set` 设置远端可用容量。
3. 填充 `MigrateOutMsg`，确保 `count` 不超过接口上限。
4. 调用 `ubturbo_smap_migrate_out` 设置异步目标，或使用
   `ubturbo_smap_migrate_out_sync` 等待本次迁出任务。
5. 查询实际状态，不把配置成功等同于所有页面已完成迁移。

普通迁出支持同一 PID 配置多个远端 NUMA。每次请求是该 PID 普通迁出目标的完整更新；未继续提供的
目标会被清除，`count == 0` 表示清空普通迁出目标。普通路径中的 `srcNid` 仅为 ABI 兼容保留。

## 进程跟踪

`ubturbo_smap_process_tracking_add` 用于显式添加统计扫描：

- `pidArr`、`scanTime`、`duration` 的有效元素数量均为 `len`。
- `scanTime` 单位为 ms，取 50–2000 范围内 50 的倍数；`duration` 单位为秒，取值 1–300。
- PID 退出后调用方仍应执行移除和状态清理。

使用 `ubturbo_smap_process_tracking_remove` 移除跟踪，使用
`ubturbo_smap_process_migrate_enable` 单独启停进程迁移。

## 迁回与移除

迁回用于将远端页面迁至本地，移除用于停止 SMAP 管理并清理配置。两者语义不同：迁回成功后仍可能
保留管理状态；移除也不能保证所有历史页面已经迁回。业务结束时应按 API 返回值和实际状态决定顺序。

## 验证

- 检查接口返回值和同步调用是否超时。
- 使用频率与进程配置查询接口确认 SMAP 状态。
- 使用操作系统 NUMA 统计进行交叉验证。
- 查看用户态和内核日志，但不要输出完整页面地址。

所有结构体字段、上限和错误语义见 [API 参考](api_reference.md)。
