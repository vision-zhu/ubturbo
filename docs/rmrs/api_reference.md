# 🔗 RMRS API 参考

## 接口边界

RMRS 客户端接口声明在 `src/sdk/turbo_rmrs_interface.h`，位于 `turbo::rmrs` 命名空间。接口参数包含
STL 容器和 C++ 类，因此只面向与发布件 ABI 兼容的 C++ 调用方。

接口返回值首先表示 IPC 调用、handler 返回和响应反序列化是否成功。常见 IPC 错误码为 1–6，分别
对应通用错误、socket 创建失败、连接失败、服务不存在、handler 失败和响应无效。返回 `0` 不一定
代表业务目标已经完成：带 `resCode`、`result` 等业务字段的响应必须继续检查这些字段；迁出策略接口
即使未生成可执行方案也可能成功返回序列化结果，调用方还必须验证结果内容。

## 迁出策略

```cpp
uint32_t UBTurboRMRSAgentMigrateStrategy(
    const MigrateStrategyParamRMRS &param,
    MigrateStrategyResult &result);
```

输入包含候选 PID 及最大迁出比例、需要释放的本地内存量、PID 到远端 NUMA 的映射和超时 NUMA。
输出为每个 PID 的迁移大小、目的 NUMA 及建议等待时间。`borrowSize` 和
`VMMigrateOutParam.memSize` 的单位均为 KB；公共头文件的现有注释未完整标明这一点，调用方不得混用
字节、KB 和页面数。

## 迁出执行

```cpp
uint32_t UBTurboRMRSAgentMigrateExecute(
    const MigrateStrategyResult &result);
```

执行先前策略结果。调用方应保证结果未被其他事务修改，并在失败或部分成功时进入回滚流程。

## 迁回

```cpp
uint32_t UBTurboRMRSAgentMigrateBack(MigrateBackResult &result);
```

尝试将节点内相关远端内存迁回本地。除函数返回值外，还应读取 `result.result` 和 `result.numaIds`，判断
哪些 NUMA 可以或已经完成归还。

## 借用回滚

```cpp
uint32_t UBTurboRMRSAgentBorrowRollBack(
    std::map<std::string, std::set<BorrowIdInfo>> &borrowIdsPidsMap);
```

按借用标识、PID 和原始大小回滚先前事务。映射必须来自同一受信任业务事务，不能使用日志中复制的
标识重放请求。

## 信息采集

```cpp
uint32_t UBTurboRMRSAgentPidNumaInfoCollect(
    const PidNumaInfoCollectParam &param,
    PidNumaInfoCollectResult &result);

uint32_t UBTurboRMRSAgentNumaMemInfoCollect(
    const NumaMemInfoCollectParam &param,
    ResponseInfoSimpo &result);
```

进程结果包含本地 NUMA、近端/远端使用量和各 NUMA 元信息。采集结果具有时效性，不能作为后续
迁移时仍然有效的锁定快照。

## UCache 路径

```cpp
uint32_t UBTurboRMRSAgentUCacheMigrateStrategy(
    const UCacheMigrationStrategyParam &param,
    ResCode &result);
uint32_t UBTurboRMRSAgentUCacheMigrateStop(ResCode &result);
uint32_t UBTurboRMRSAgentUpdateUCacheRatio(
    const MigrationInfoParam &param,
    UCacheRatioRes &result);
```

只有 `rmrs.ucache.enable=true` 且 UCache 内核能力、设备和权限就绪时使用。接口返回成功后仍需检查
响应中的 `resCode`；比例必须在业务允许范围内，PID 列表必须经过授权。

## 主要数据结构

| 类型 | 关键字段 |
| --- | --- |
| `VMPresetParam` | `pid`、最大迁出 `ratio` |
| `MigrateStrategyParamRMRS` | PID 列表、`borrowSize`、远端映射、超时 NUMA |
| `VMMigrateOutParam` | `pid`、`memSize`、`desNumaId` |
| `MigrateStrategyResult` | 迁出列表、`waitingTime`（ms） |
| `PidInfo` | 本地 NUMA、近端/远端用量、NUMA 元信息 |
| `UCacheMigrationStrategyParam` | 本地/远端 NUMA、PID、UCache 比例 |

精确字段、类型和默认初始化值以同版本 `turbo_rmrs_interface.h` 为准。升级客户端或服务端时必须成对
验证 C++ ABI 和 JSON 序列化兼容性。
