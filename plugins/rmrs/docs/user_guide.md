# RMRS 用户指南

## 前提条件

- UBTurbo、RMRS 插件和匹配版本的 SMAP 已安装。
- SMAP 内核模块已加载，页面类型与目标工作负载一致。
- `plugin_rmrs.conf` 可读，插件共享库位于 UBTurbo 库目录。
- PID、远端 NUMA 和容量由可信资源管理系统提供。

## 配置

`/opt/ubturbo/conf/plugin_rmrs.conf`：

```ini
turbo.plugin.name=rmrs
turbo.plugin.pkg=librmrs_ubturbo_plugin.so
rmrs.ucache.enable=false
```

`rmrs.ucache.enable=true` 会启用 RMRS 的 UCache 路径；只有在相应内核能力、设备节点和权限均已准备
时才能开启。

在准入文件中启用：

```ini
rmrs=777
```

修改后重启服务：

```bash
sudo systemctl restart ubturbo
systemctl status ubturbo --no-pager
```

## 调用顺序

1. 确认 UBTurbo 与 RMRS 心跳/服务可用。
2. 收集并下发进程和 NUMA 信息。
3. 调用迁出策略接口取得决策。
4. 在业务事务中调用迁出执行接口。
5. 业务失败时调用借用回滚；资源归还时调用迁回接口。

请求、响应和错误码见 [API 参考](api_reference.md)。

## 验证

- UBTurbo 日志中没有 RMRS 配置、加载或服务注册错误。
- 请求返回码为成功，响应 JSON 字段完整。
- SMAP 查询和操作系统 NUMA 统计与预期方向一致。
- 上层事务状态与实际迁移结果一致，不只依赖 IPC 返回码。

## 常见问题

| 问题 | 检查项 |
| --- | --- |
| 插件未加载 | 准入项、模块码、共享库、`plugin_rmrs.conf` |
| 策略请求失败 | JSON 格式、数组长度、PID 列表、容量和 NUMA 范围 |
| 迁移超时 | SMAP 状态、可迁页面、远端容量、系统负载 |
| 回滚不完整 | PID 是否退出、先前迁移是否部分成功、SMAP 配置记录 |
| UCache 路径失败 | 配置开关、设备节点、内核模块和访问权限 |

不要通过放宽设备权限、绕过配置校验或改用任意 PID 来规避错误。
