# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 项目概述

UBTurbo 是华为开源的**节点内资源管理框架**，提供配置读取、插件加载、日志打印、IPC通信能力，集成 SMAP 实现多级内存调度。主要用于虚拟化场景下的 NUMA 架构内存分层管理（冷热页识别、页迁移、内存资源调度）。

核心语言：C++17（框架及 RMRS/UCache 插件）、C11（SMAP 内核插件和 UBDMA 内核模块）。目标平台：openEuler Linux (aarch64)。

## 构建与测试

### 构建

```bash
# 初始化子模块（首次）
git submodule update --init --recursive
# Windows 环境需先转换换行符
dos2unix build.sh

# Release 构建（默认）
sh build.sh

# Debug 构建
sh build.sh -D

# 清理后构建
sh build.sh -c && sh build.sh

# 指定并行任务数
sh build.sh -j 8

# Sanitizer 构建
sh build.sh --asan   # 或 --lsan / --tsan / --ubsan

# 源码编译第三方依赖
sh build.sh -S
```

构建产物输出到 `dist/release/`（Release）或 `dist/debug/`（Debug）：
- `bin/ub_turbo_exec` — 主守护进程
- `lib/libubturbo_client.so` — IPC客户端SDK
- `lib/librmrs_ubturbo_plugin.so` — RMRS插件

### 单元测试

```bash
# 构建+运行所有UT
sh build.sh ut

# 仅构建测试（不运行）
sh build.sh ut --skip-run-tests

# 覆盖率报告
sh build.sh -C
```

测试产出两个可执行文件：`ubturbo_ut`（核心框架UT）和 `rmrs_ut`（RMRS插件UT）。

`test/run_ut.sh` 在运行前会用 sed 移除 `src/smap/*.cpp` 中的 `static` 关键字以支持 mockcpp 打桩，运行后覆盖率报告生成在 `gcovr_report/`。

### 内核模块构建

SMAP 和 UBDMA 是独立内核模块，使用各自的 Makefile/Kbuild 构建，**不在主 CMake 构建中**：

```bash
# SMAP drivers 模块（产出3个.ko）
cd plugins/smap/src/drivers && make

# SMAP tiering 模块（产出smap_tiering.ko）
cd plugins/smap/src/tiering && make
# 禁用critical检查: make CRITICAL=0

# SMAP user 库（独立CMake构建，产出libsmap.so）
cd plugins/smap && mkdir build && cd build && cmake .. && make

# UBDMA 内核模块
cd plugins/ubdma/src && make
```

所有内核模块使用标准 Kbuild：`$(MAKE) -C $(KERNELDIR) M=$(PWD) modules`，`KERNELDIR` 默认 `/lib/modules/$(uname -r)/build`。
tiering 依赖 drivers 的 `Module.symvers`（通过 `KBUILD_EXTRA_SYMBOLS`）。
drivers 编译时传递 `ccflags-y += -DACPI_NOT_READY`。

SMAP 有独立 CMakeLists.txt（project SMAP v6.0, C++11），产出在 `output/smap/`。

### 格式化与静态分析

```bash
clang-format -style=file -i <file>    # 使用 .clang-format 配置
clang-tidy -p=cmake-build-release -header-filter=.* --fix <file>  # Release 代码
clang-tidy -p=cmake-build-debug --fix <file>                       # 测试代码
```

Pre-commit 钩子（`.pre-commit-config.yaml`）：源码变更触发完整 Release 构建；测试代码变更触发测试构建；自动执行 clang-format 和 clang-tidy。SMAP/UBDMA 测试目录排除在 clang-format/clang-tidy 检查之外。

## 架构

### 模块生命周期

`TurboMain`（Meyer's 单例）按顺序管理五个模块（均继承 `TurboModule` 接口，实现 `Init/Start/Stop/UnInit`）：

**启动顺序**：Config → Logger → SMAP → Plugin → IPC Server
**关闭顺序**（逆序）：IPC Server → Plugin → SMAP → Logger → Config

任何模块 `Init()` 或 `Start()` 返回非 `TURBO_OK(0)` 即终止启动；关闭始终遍历所有模块。

### IPC 通信流

外部进程 → `libubturbo_client.so`（SDK）→ IPC Client（Unix Domain Socket）→ IPC Server（Reactor模式，每连接独立线程）→ 插件注册的回调函数 → SMAP 内核接口执行

IPC Client API：`UBTurboFunctionCaller(functionName, params, result)` — 返回码：0=成功, 1=通用错误, 2=socket无效, 3=连接断开, 4=无匹配函数, 5=服务端函数错误, 6=输出范围异常

IPC Server API：`UBTurboRegIpcService(name, handler)` — 函数名最长128字符不含空格，不可重复注册，回调中不可嵌套调用。

### 插件机制

`TurboModulePlugin` 读取 `ubturbo_plugin_admission.conf`，通过 `dlopen` 加载启用的 `.so` 插件。插件必须实现：
- `TurboPluginInit(modCode)` — 启动时调用
- `TurboPluginDeInit()` — 关闭时调用
- 通过 `UBTurboRegIpcService()` 注册 IPC 回调

启用插件：在 `ubturbo_plugin_admission.conf` 中取消注释对应行，插件 .so 和配置文件需先放入 `/opt/ubturbo/lib` 和 `/opt/ubturbo/conf`。

### 核心模块职责

| 模块 | 命名空间 | 关键职责 |
|------|----------|----------|
| Config | `turbo::config` | 解析INI配置文件（`/opt/ubturbo/conf/`），提供类型化读取接口 |
| Logger | `turbo::log` | 异步环形缓冲区日志，每模块独立日志文件（最大200MB×10文件轮转） |
| IPC Server | `turbo::ipc::server` | UDS Reactor服务器，连接级独立线程 |
| IPC Client | `turbo::ipc::client` | UDS客户端SDK封装 |
| Plugin | `turbo::plugin` | dlopen/dlclose插件管理器 |
| SMAP | mixed | SMAP消息编解码，桥接框架与内核模块 |

### 插件模块

| 插件 | 语言 | 职责 |
|------|------|------|
| RMRS | C++17 | VM/容器内存迁移决策与执行（迁出/迁回策略），依赖SMAP执行 |
| SMAP | C(kernel)+C++(user) | 内核级冷热页识别与页迁移，1650芯片硬件增强热度检测 |
| UBDMA | C(kernel) | DMA引擎内核模块，段管理器+内存通知器 |
| UCache | C++17 | 页缓存管理，资源采集+策略执行+任务执行 |

## SMAP 内部架构（重点开发区域）

SMAP 插件由三个核心子目录组成，形成**内核数据采集 → 内核迁移执行 → 用户态策略决策**的完整链路：

```
drivers/ (内核: 页访问频率采集)
    ↓ ACTC频率数据 + 物理地址位图
tiering/ (内核: 页迁移执行引擎)
    ↓ ioctl接口 (SMAP_MIG_MIGRATE等)
user/   (用户态: 策略决策 + 进程管理 + 周期调度)
```

### drivers/ — 页访问频率采集（内核模块）

产出 **3个内核模块**：`smap_tracking_core`（跟踪总线基础设施）、`smap_access_tracking`（核心访问频率引擎）、`smap_histogram_tracking`（硬件SMAP驱动）。

**核心机制 — ACTC（Access Count）频率采集**：

两种扫描路径：
- **2M KVM扫描**（`accessed_bit.c: scan_accessed_bit_forward_vm`）：遍历KVM memslot，通过 `smap_kvm_pgtable_stage2_mkold`（`kvm_pgtable.c`）检查/清除stage-2 PTE的Access Flag位，若页面被访问则累加ACTC计数
- **4K host扫描**（`accessed_bit.c: scan_accessed_bit_forward_mm`）：`walk_page_range` + `check_pte_young` 检查/清除host PTE accessed位，64KB组优化（相邻页共享访问模式则直接标记）

硬件辅助扫描（`hist_ops.c` + `ub_hist.c`，`enable_hist=1` 时启用）：内核线程驱动1650芯片BA寄存器，2M粗扫+4K精扫多粒度模式。

**PID状态机**（`access_pid.c`）：`AP_STATE_WALK → READ → FREQ → MIG`，严格顺序推进，spinlock保护状态转换。

**内存拓扑发现**：
- 本地NUMA：`access_acpi_mem.c` 解析ACPI SRAT表
- 远程NUMA：`access_iomem.c` 通过 `walk_iomem_res_desc` 发现远程RAM段

**字符设备接口**：
- `/dev/smap_nodeN`（每个NUMA节点）— tracking ioctl（enable/disable, mode, page_size）
- `/dev/smap_access_device` — PID跟踪、pagemap walk、ACTC数据读取、procfs频率输出

**导出符号供tiering/user使用**：`convert_pos_to_paddr_sorted`、`walk_pid_pagemap`、`remote_ram_list`、`set_reinit_pending_flag`、`get_ham_pages_freqs` 等

**模块参数**：
- `smap_scene`：0=HCCS, 1=UB_QEMU, 2=UB_QEMU_ADVANCED
- `enable_hist`：0=禁用(默认), 1=启用硬件SMAP
- `hist_4k_scan_mode_param`：0=多粒度滑动, 1=顺序循环滑动(默认, 可sysfs运行时修改)

### tiering/ — 页迁移执行引擎（内核模块）

产出内核模块 `smap_tiering`，通过 kprobe 解析内核符号（`migrate_pages`、`isolate_folio_to_list`、`putback_movable_pages`）以调用非导出内核API。

**三条迁移路径**：

1. **热度迁移**（ioctl `SMAP_MIG_MIGRATE`，magic `0xB9`）：
   - `mig_init.c: __ioctl_migrate` → `build_migrate_list` → `do_migrate`
   - `do_migrate` 按from节点降序处理（先远程→本地=提升，后本地→远程=降级）
   - `smap_migrate_pages.c: smap_migrate` → `isolate_and_migrate_folios` → `fp_migrate_pages`(kprobe)
   - 支持多线程迁移：`migrate_multi_threaded`（最大32线程）

2. **迁回迁移**（ioctl `SMAP_MIGRATE_BACK`，magic `0xBA`）：
   - `pid_ioctl.c: __ioctl_migrate_back` → 创建 `migrate_back_task` + subtask
   - workqueue `smap_migrate_back_wq` 周期性（1000ms间隔）处理 WAITING 任务
   - 2M页：`smap_handle_migrate_back_subtask`；4K页：`smap_handle_migrate_back_subtask_4k` + `find_page_task`(rmap) + `find_node_to_migrate_rr`(轮询)

3. **HAM加速迁移**（ioctl on `/dev/ham_migrate`）：
   - 6步流程：START → (MODIFY_PAGETABLE suspend) → MIGRATE_PAGES → CACHE_CLEAR → (MODIFY_PAGETABLE resume) → ROLLBACK(可选)
   - `ham_migration.c: handle_ham_migration`：页面按频率排序后批量迁移，最大1000次重试
   - `coherence_maintain.c`：ARM64 TLB刷新、页表cacheable/valid切换、HiSilicon SOC缓存维护

**rmap反向映射**（`rmap.c` + `ksm.c`）：`find_page_task` 查找页面的PID/NUMA归属，支持anon/file/ksm三种rmap遍历。用于4K迁回时确定目标本地NUMA节点。

**内存拓扑**：与drivers共享概念但独立实现 — `acpi_mem.c`(本地SRAT)、`iomem.c`(远程OBMM设备 `/sys/devices/obmm/obmm_shmdev*`)、`memid_range` 支持memid→PA范围查找。

**字符设备**：
- `/dev/smap_mig_device`（flock独占）— 迁移ioctl
- `/dev/smap_device` — 迁回ioctl
- `/dev/ham_migrate` — HAM ioctl

**模块参数**：`node_modes[22]`(每节点数据模式)、`smap_pgsize`(0=4K/1=2M)、`smap_mode`(0=bare/1=VM/2=process)、`smap_scene`(0=HCCS/1=UB_QEMU)、`qemu_name`

**关键常量**：`NR_BATCHED_MIGRATION=512`、`MAX_MIGRATE_BACK_TASK_COUNT=100`、`HAM_MAX_TASK=32`、`MAX_MIGRATE_NUMA_RETRY_TIME=10`

### user/ — 用户态策略决策与进程管理

C语言实现，产出共享库 `smap`，链接 pthread、user_log、boundscheck。

**核心数据模型**：`ProcessManager`（g_processManager单例）管理 `ProcessAttribute` 链表：
- `ProcessAttribute`：PID + 类型(PROCESS_TYPE/VM_TYPE) + 状态(PROC_IDLE/MIGRATE/BACK/MOVE) + numaNodes位图(L1+L2编码在uint32_t) + strategyAttr + scanAttr(ACTC数据) + sceneInfo + groupPolicy + vmPidAttr(domainId/mmapType)
- NUMA位图编码（`numa_nodes.h`）：bits 0-3 = local(L1), bits 4-21 = remote(L2)，MAX_NODES=22

**公共API**（`smap_interface.h/.c`，约2700行）：
- `ubturbo_smap_start(pageType, extlog)` — 初始化ProcessManager、打开设备、恢复配置、创建线程
- `ubturbo_smap_migrate_out(msg, pidType)` — 设置进程迁移目标
- `ubturbo_smap_migrate_out_grouped(msg, pidType)` — 设置分组迁移策略
- `ubturbo_smap_migrate_back(msg)` — 指定地址范围迁回
- `ubturbo_smap_remove(msg, pidType)` — 移除进程管理
- `ubturbo_smap_urgent_migrate_out(size)` — OOM紧急迁移
- `ubturbo_smap_freq_query(pid, data, lengthIn, lengthOut, dataSource)` — 查询页频率数据

**周期调度循环**（`strategy/migration.c: ScanMigrateWork`）：
禁用tracking → 重读配置 → 检查/移除无效进程 → 迁移准备 → 场景更新 → 比率配置 → 执行迁移 → 恢复扫描次数 → 刷新远程RAM → 启用tracking

**策略分发**（`strategy/strategy.c: RunStrategy`）：
- `GroupedMigrationStrategy` — 分组VM策略
- `SeparateStrategy` — 2M单NUMA VM
- `SeparateStrategy4K` — 4K进程
- `SeparateStrategyMultiNumaVm` — 2M多NUMA VM

**分组交换策略**（`strategy/grouped_strategy.c`）：
- 多阶段流水线：`SyncGroupedTargetUsedPages`(刷新目标配额) → `RunSwapStage`(交换冷本地页与热远程页)
- 门槛条件：`swapCandidateRounds≥2`(连续2轮达标)、`IsGroupLocalSteady`(所有本地NUMA保留页高于水位线)、`UpdateGroupSwapTotalPagesStable`(2轮总页数不变)
- 共享远程NUMA目标：多组共享同一远程NUMA时按配额比例分配
- `IsSwapBeneficial` 判断：remoteFreq > localFreq × weight + slowThreshold + minGain, 且 remoteFreq > minRemoteFreq

**场景检测**（`advanced-strategy/scene.c`）：3种场景 LIGHT_STABLE/HEAVY_STABLE/UNSTABLE，滑动窗口分析热页数量变化，`ConfigRatios` 调整每VM远程内存比率（盈余/ deficit平衡）。

**配置持久化**（`manage/smap_config.c`）：`/dev/shm/smap_config` 共享内存文件，mmap结构化二进制布局（Header + NumaPayload + ProcessPayload），支持崩溃恢复 `RecoverFromConfig()`。

**策略配置文件**（`strategy/strategy_config.c`，`/opt/ubturbo/conf/smap/period.config`）：

| 配置键 | 默认值 | 说明 |
|--------|--------|------|
| `smap.scan.period` | 200ms | 页扫描间隔 |
| `smap.migrate.period` | 2000ms | 迁移周期间隔 |
| `smap.remote.freq.percentile` | 99 | 远程频率百分位 |
| `smap.slow.threshold` | 2 | 慢页频率阈值倍数 |
| `smap.freq.wt` | 0 | 频率权重(0=自动) |
| `smap.group.swap.ratio` | 1 | 交换容量比率 |
| `smap.group.swap.min.remote.freq` | 0 | 交换最低远程频率 |
| `smap.group.swap.min.freq.gain` | 0 | 交换最低频率增益 |
| `smap.group.swap.local.watermark.ratio` | 95% | 交换本地水位线比率 |
| `smap.migrate.mode` | 1 | 迁移模式(0=水位线/1=默认) |
| `smap.adaptive.ratio.enable` | true | 自适应比率调整 |

**内核通信桥接**：用户态通过 ioctl 操作3个字符设备：
- `/dev/smap_mig_device` — `SMAP_MIG_MIGRATE`(迁移)、`SMAP_MIG_MIGRATE_NUMA`(NUMA间迁移)、`SMAP_MIG_PID_REMOTE_NUMA`(PID远程迁移)、`SMAP_SET_UB_DMA_AVAIL`(DMA可用性)
- `/dev/smap_device` — `SMAP_MIGRATE_BACK`(迁回)
- `/dev/smap_access_device` — `SMAP_ACCESS_ADD_PID/REMOVE_PID/WALK_PAGEMAP/GET_TRACKING/GET_NR_LOCAL_NUMA/REFRESH_REMOTE_RAM`

**ioctl协议合约**：drivers/tiering/user 各自独立定义 ioctl 魔数和消息结构体，需保持布局一致（magic `0xB9`/`0xBA`/`0xBB`），这是隐式协议而非共享头文件。

### SMAP 三层交互总结

```
user/ 策略层                            drivers/ 采集层                    tiering/ 执行层
┌─────────────────┐                    ┌──────────────────┐              ┌──────────────────┐
│ ScanMigrateWork  │──周期循环──→       │ work_func         │             │                  │
│ (周期调度)       │                    │ (delayed_work)    │             │                  │
│                  │                    │                    │             │                  │
│ RunStrategy      │──频率数据──→       │ AccessRead         │             │                  │
│ (策略分发)       │  ←──ACTC数据──     │ (GET_TRACKING)    │             │                  │
│                  │                    │                    │             │                  │
│ DoMigration      │──迁移指令──→       │                    │──ioctl──→  │ do_migrate        │
│ (迁移执行)       │                    │                    │             │ → smap_migrate    │
│                  │                    │                    │             │ → fp_migrate_pages│
│                  │                    │                    │             │                  │
│ 迁回请求         │──ioctl──→          │                    │──ioctl──→  │ migrate_back      │
│                  │                    │                    │             │ → rmap+轮询       │
└─────────────────┘                    └──────────────────┘              └──────────────────┘
```

### 公共头文件（`include/`）

- `turbo_ipc_client.h` — 外部IPC客户端API入口
- `turbo_ipc_server.h` — IPC服务注册API
- `turbo_conf.h` — 配置读取API（GetUInt32/GetFloat/GetStr/GetBool/GetUInt64）
- `turbo_logger.h` — 日志宏（`UBTURBO_LOG_INFO` 等，格式：`UBTURBO_LOG_INFO(moduleName, moduleId) << args`）
- `turbo_def.h` — 核心类型定义（`TurboByteBuffer`、`IpcHandlerFunc`）

## 代码风格

### clang-format 配置要点

- 基于 Google 风格，4空格缩进，120字符行宽限制
- 函数定义使用 Allman 大括号风格（换行），其他使用 K&R（同行）
- 指针右对齐：`Type *name`
- 命名空间不缩进，自动添加闭合注释
- Include 分类排序：系统头文件 < rack_def.h < rack_*.h < 其他

### clang-tidy 配置要点

- 白名单策略：先禁用所有(`-*`)，再选择性启用
- `bugprone-use-after-move` 作为错误处理（WarningsAsErrors）
- 函数体最大50条语句
- 命名约定：变量/成员 camelBack；全局变量 `g_` 前缀 camelBack；类/结构体/枚举 CamelCase；函数 CamelCase；全局常量 UPPER_CASE；枚举常量/宏 UPPER_CASE
- 禁止头文件中 `using namespace`，禁止 C 风格转换，禁止 `reinterpret_cast`

### .editorconfig 规则

- C/C++：4空格缩进
- **SMAP 内核代码**（`drivers/`、`tiering/`）：Tab缩进，8空格宽度，Linux内核风格
- **SMAP 用户态代码**（`user/`）：4空格缩进，C语言风格
- SMAP `drivers/` 和 `tiering/` 的测试目录排除在 clang-format/clang-tidy 检查之外

### SMAP 内核代码风格注意

`drivers/` 和 `tiering/` 遵循 **Linux内核编码风格**（非项目主 clang-format 规则）：
- Tab缩进（8空格宽度），最长80字符行宽
- 变量命名：snake_case（与用户态/框架层的 CamelCase 不同）
- 结构体用 `struct xxx` 而非 typedef
- EXPORT_SYMBOL / EXPORT_SYMBOL_GPL 导出符号
- 内核模块参数用 `module_param` 系列宏
- ioctl 使用 `_IOW/_IOR/_IO` 宏定义命令码

### 安全编译标志

Release：`-fstack-protector-strong -pie -Wl,-z,noexecstack -Wl,-z,relro -Wl,-z,now -D_FORTIFY_SOURCE=2`
Debug：`-fstack-protector-strong -fPIE -O0 -ggdb3`

## 测试框架

Google Test (gtest) + mockcpp (v2.7)。

测试目录：`test/testcase/`，按模块组织（config/log/ipc/plugin/utils/smap/rmrs）。
SMAP 有独立测试：`plugins/smap/test/`（使用 gtest + mockcpp + 内核桩驱动）。

`TurboByteBuffer` 最大数据长度 1,048,576 字节。

## RPM打包

`ubturbo.spec` 定义 RPM 包构建（CPack），版本 1.1.1-1，安装前缀 `/opt/ubturbo`，依赖：make, gcc, cmake, ninja-build, libboundscheck, rapidjson, libvirt-devel。安装后以 systemd 服务运行（`ubturbo.service`），以 ubturbo 用户身份，内存限制 30G。

## 依赖管理

使用 Git 子模块，无包管理器：
- `3rdparty/libboundscheck` — openEuler 安全边界检查库
- `3rdparty/rapidjson` — JSON解析
- `test/3rdparty/mockcpp` — C++ mock框架 (v2.7)
- `test/3rdparty/googletest` — Google Test

## 关键约束

- 内存地址安全性：源端和目标端内存地址安全级别必须匹配
- 用户权限：用户必须加入 UBTurbo 组并具备节点内存资源管理权限
- PID管理：UBTurbo 无法验证 PID 有效性（由集群资源管理中心管理）
- IPC 回调不可嵌套调用、不可重复注册同名函数
