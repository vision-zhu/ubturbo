# SMAP 开发者指南

## 代码布局

| 目录 | 说明 | 风格 |
| --- | --- | --- |
| `src/user/` | `libsmap.so`、管理和策略 | C11，4 空格、120 列 |
| `src/drivers/` | 访问跟踪驱动 | Linux 内核风格 |
| `src/tiering/` | 页面迁移驱动 | Linux 内核风格 |
| `src/ucache/` | 页面缓存迁移驱动 | Linux 内核风格 |
| `test/` | 用户态与内核桩测试 | GoogleTest/mockcpp |

用户态和内核态使用不同的 clang-format/clang-tidy 配置，不能用用户态规则批量格式化内核代码。

## 构建

```bash
./build.sh -t debug
./build.sh -t release
./build.sh -t clean
```

构建内核模块：

```bash
make -C src/drivers KERNEL_VERSION=openeuler -j"$(nproc)"
cp -f src/drivers/Module.symvers src/tiering/depends/
make -C src/tiering KERNEL_VERSION=openeuler -j"$(nproc)"
make -C src/ucache -j"$(nproc)"
```

## 公共接口分组

### 生命周期

- `ubturbo_smap_start`
- `ubturbo_smap_stop`
- `ubturbo_smap_is_running`

启动时确定 `pageType` 和外部日志回调。后续请求必须匹配该实例状态。停止时应先阻止新增任务，再结束
线程、设备和策略状态。

### 迁出、迁回与移除

- `ubturbo_smap_migrate_out`
- `ubturbo_smap_migrate_out_grouped`
- `ubturbo_smap_migrate_out_sync`
- `ubturbo_smap_urgent_migrate_out`
- `ubturbo_smap_migrate_back`
- `ubturbo_smap_remove`

同步接口的 `maxWaitTime` 是等待上限，不是迁移完成的性能保证。异步配置成功只代表请求被接受。

### 跟踪与查询

- `ubturbo_smap_process_tracking_add/remove`
- `ubturbo_smap_process_migrate_enable`
- `ubturbo_smap_freq_query`
- `ubturbo_smap_process_config_query`
- `ubturbo_smap_remote_numa_freq_query`

数组接口必须同时校验指针、元素数量、每个元素的范围和输出容量。

### NUMA 管理

- `ubturbo_smap_remote_numa_info_set`
- `ubturbo_smap_node_enable`
- `ubturbo_smap_remote_numa_migrate`
- `ubturbo_smap_same_remote_numa_migrate`
- `ubturbo_smap_pid_remote_numa_migrate`

调用方负责提供已授权的 NUMA 与容量信息；SMAP 只处理执行条件。

完整结构体、参数和返回值见 [API 参考](api_reference.md)。

## 修改接口

1. 先修改 `src/user/smap_interface.h`，明确 ABI、上限和所有权。
2. 同步 UBTurbo 的 SMAP 函数指针、动态符号加载和 IPC handler。
3. 同步用户态实现、内核 ioctl 结构及兼容转换。
4. 增加空指针、零长度、最大数量、非法 NUMA、页面类型冲突和部分失败测试。
5. 更新 API、架构、配置及发布说明。

公开结构体布局用于动态库和内核消息时，不能只改字段而不考虑旧调用方、对齐和长度校验。

## 并发与清理

- 修改全局策略或进程状态前确认所需互斥和原子语义。
- 内核扫描工作队列、迁移任务和用户态控制线程的停止顺序必须明确。
- 错误路径释放已打开设备、已分配缓冲区、已注册线程和未完成任务。
- 不在持锁期间执行无界等待或高开销日志。
- PID 退出和 NUMA 下线必须视为正常竞态，而不是仅在调试构建处理。

## 测试

```bash
cd test
sh run_dt.sh
```

仅运行已构建测试：

```bash
./build/smap_dt --gtest_filter='SuiteName.CaseName'
```

测试脚本会对源码生成测试副本并收集覆盖率。不要把测试生成目录中的改动提交到源码目录。除正常路径外，
至少覆盖生命周期重复调用、设备失败、配置边界、PID 退出、容量不足、超时和并发停止。

## 日志和安全

外部日志回调可能由多个线程调用，其实现必须线程安全且不能反向阻塞 SMAP。日志只记录阶段、统计量和
错误码，不记录页面内容、完整物理地址列表、凭证或租户敏感信息。
