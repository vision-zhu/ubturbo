# RMRS 架构设计

## 定位与边界

RMRS 是 UBTurbo 通用插件，不是独立守护进程。插件初始化时读取配置、恢复必要状态并向 UBTurbo IPC
server 注册 handler。当前反初始化实现会停止 SMAP helper 和 UCache detector，但没有显式注销 IPC
服务；在补齐注销逻辑前，不应把运行期热卸载视为安全能力。

RMRS 负责“迁多少、迁哪些进程、何时迁回或回滚”的业务决策。实际冷热扫描、页选择和迁移动作由
SMAP 完成。RMRS 不负责跨节点认证、PID 授权或远端内存分配。

## 组件

| 组件 | 职责 |
| --- | --- |
| CallbackManager | 注册 IPC 服务，解析请求并组装响应 |
| intranode strategy | 迁出、迁回、回滚及节点内策略 |
| RmrsSmapHelper | 将 RMRS 决策转换为 SMAP 接口调用 |
| message/common | JSON 消息、返回码、配置和公共校验 |
| UCache path | 可选的页面缓存迁移与瓶颈检测 |

## 决策与执行

![RMRS 决策与执行流程](images/rmrs-workflow.svg)

典型迁出事务：

1. 上层资源管理器发送目标迁移量、候选 PID 和远端 NUMA 信息。
2. handler 校验消息结构、数量和字段范围。
3. 策略模块结合候选进程及可迁移容量生成分配结果。
4. RMRS 返回策略，或根据请求调用 SMAP 同步/异步迁出。
5. 上层业务提交事务；失败时调用 BorrowRollback 服务撤销借用。

典型归还事务先判断远端 NUMA 上相关进程是否可以迁回，再调用 SMAP 迁回。部分 PID 已退出时，
RMRS 会清理相应配置，但调用方仍应根据返回码处理部分成功。

## IPC 服务

当前插件注册的服务包括：

- `BorrowRollbackRecvHandler`
- `MigrateStrategyRecvHandler`
- `MigrateBackRecvHandler`
- `MigrateExecuteRecvHandler`
- `HeartBeatRecvHandler`
- `PidNumaInfoCollectRecvHandler`
- `NumaMemInfoCollectRecvHandler`
- `UCacheMigrateStrategyRecvHandler`
- `UCacheMigrateStopRecvHandler`
- `UpdateUCacheRatioRecvHandler`

这些字符串是 IPC 路由名。文档中的 `UBTurboRMRSAgent*` 是客户端封装接口，两者不能混用。

## 多 NUMA 与页面类型

- 4K 普通进程和 2M 静态大页虚机必须使用与 SMAP 启动一致的页面类型。
- 一个 PID 可以配置多个远端 NUMA；普通迁出由 SMAP 根据亲和性、页面驻留和迁移记录管理本地 NUMA。
- 远端容量由多个 PID 共享，配置成功不保证页面立即达到目标比例。
- 调用方不得假设一次 IPC 成功等价于所有页面迁移完成。

## 故障与恢复

- JSON、数量或范围非法：拒绝请求，不调用 SMAP。
- SMAP 返回失败或超时：保留明确错误码，由上层选择重试或回滚。
- 插件初始化或 handler 注册失败：RMRS 初始化失败；当前插件管理器记录错误并跳过 RMRS，主服务仍
  可能继续启动，必须检查日志和服务注册状态。
- 插件卸载：当前反初始化未显式注销 handler，部署流程应先停止 UBTurbo，不应在服务运行时热卸载
  RMRS。
- 重启恢复：只恢复代码明确持久化的状态；上层必须重新核对业务事务和实际页面位置。

## 安全约束

RMRS 信任上层提供的 PID 和 NUMA 信息。部署系统必须完成身份认证、授权、配额、重放防护和审计；
日志不得记录完整请求、敏感地址或租户数据。
