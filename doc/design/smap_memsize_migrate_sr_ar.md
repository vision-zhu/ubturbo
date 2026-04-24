# SR: SMAP进程按迁移量迁移功能

## SR描述

**SR编号**: SR-SMAP-MEMSIZE-MIGRATE

**SR名称**: SMAP进程按迁移量迁移

**SR描述**: 扩展SMAP按内存大小迁移功能（MIG_MEMSIZE_MODE）支持通算4K页场景，使Redis等通算进程可以精确指定迁移内存量（如512MiB）到指定远端NUMA节点。复用现有ubturbo_smap_start和ubturbo_smap_migrate_out接口，通过memSize参数（单位KB）控制迁移量，迁移达到目标大小后自动停止。

**需求来源**: 腾讯POC Redis进程精确控制迁移量需求

**优先级**: P1

**验收标准**:
- 支持4K页进程按MiB单位精确指定迁移量
- memSize必须2MB对齐（KB_PER_2MB=2048）
- 迁移量达到memSize后自动停止
- 接口响应时间 < 100ms
- 迁移成功率 > 95%
- 配置持久化支持状态恢复

---

## AR拆分

### AR: 按量迁移完整实现

| 属性 | 内容 |
| --- | --- |
| **AR编号** | AR-SMAP-MEMSIZE-MIGRATE |
| **AR名称** | 按量迁移参数校验、配置管理与执行控制完整实现 |
| **AR描述** | 扩展4K页场景支持MEM_POOL_MODE运行模式，实现memSize参数校验（2MB对齐），配置管理记录migrateMode和memSize并持久化，迁移策略计算按memSize计算迁移页数，执行时累计迁移量并判断是否达到目标后停止 |
| **依赖AR** | 无 |
| **实现方式** | 1. ubturbo_smap_run_mode_set扩展支持4K页MEM_POOL_MODE<br>2. CheckMigOutPayloadItems校验memSize % KB_PER_2MB == 0<br>3. ProcessAddManage记录migrateMode和memSize到ProcessAttribute.migrateParam<br>4. 配置持久化ProcessPayload新增migrateMode和memSize字段<br>5. CalculateMigrateStrategy：nrMigratePages = memSize / KB_PER_2MB<br>6. 迁移执行累计nrMigratePages，达到目标后停止并设置state=PROC_IDLE<br>7. DFX日志记录迁移完成和实际迁移量 |
| **关键数据结构** | MigrateOutPayloadInner.memSize、ProcessAttribute.migrateParam[].memSize、StrategyAttribute.memSize、StrategyAttribute.nrMigratePages、ProcessPayload.migrateParam[].memSize |
| **验收标准** | 1. memSize非2MB对齐返回-EINVAL<br>2. MEM_POOL_MODE下仅支持MIG_MEMSIZE_MODE<br>3. 配置持久化正确，状态恢复正确<br>4. 迁移页数计算正确：memSize / KB_PER_2MB<br>5. 迁移量达到memSize后自动停止，状态切换PROC_IDLE<br>6. 实际迁移量误差 < 2MB<br>7. 接口响应时间 < 100ms |
| **涉及文件** | src/user/smap_interface.c、src/user/manage/manage.c、src/user/manage/smap_config.c、src/user/strategy/separate_strategy.c、src/user/strategy/migration.c |

---

## AR工作量估算

| AR | 估算工作量 | 说明 |
| --- | --- | --- |
| AR-SMAP-MEMSIZE-MIGRATE | 2人日 | 参数校验、配置管理、策略计算、执行控制，约350行代码 |

---

## 非功能性需求

| 类型 | 需求 |
| --- | --- |
| **性能** | 接口响应时间 < 100ms |
| **性能** | 迁移延迟 < 单次迁移周期 |
| **性能** | 迁移成功率 > 95% |
| **可靠性** | memSize必须2MB对齐，否则返回-EINVAL |
| **可靠性** | 迁移失败返回明确错误码 |
| **可靠性** | 配置持久化支持进程异常退出后状态恢复 |
| **可维护性** | 复用现有接口ubturbo_smap_start、ubturbo_smap_migrate_out |
| **可维护性** | 复用现有数据结构ProcessAttribute、StrategyAttribute |

---

## 测试覆盖

| 测试类型 | 测试场景 |
| --- | --- |
| 单元测试 | memSize非2MB对齐返回-EINVAL |
| 单元测试 | memSize=0返回-EINVAL |
| 单元测试 | MEM_POOL_MODE下使用MIG_RATIO_MODE返回-EINVAL |
| 单元测试 | 配置持久化正确、状态恢复正确 |
| 功能测试 | Redis进程迁移512MiB，实际迁移量误差<2MB |
| 功能测试 | 多进程同时按量迁移 |
| 功能测试 | 迁移完成状态切换为PROC_IDLE |
| 可靠性测试 | 迁移中进程退出，SMAP正确处理 |
| 性能测试 | 接口响应时间<100ms，并发40进程 |
| 压力测试 | 连续配置300进程，长时间运行24小时 |