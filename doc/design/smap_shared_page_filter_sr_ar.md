# SR: SMAP迁出内存共享页过滤功能

## SR描述

**SR编号**: SR-SMAP-SHARED-PAGE-FILTER

**SR名称**: SMAP迁出内存时过滤共享页

**SR描述**: 在内存冷热流动场景中，进程的共享内存页面（如mmap(MAP_SHARED)、POSIX shm、System V shm、tmpfs等）迁移到远端NUMA可能导致数据一致性、系统稳定性等可靠性问题。本需求要求在迁出内存时自动识别并过滤共享页，确保共享页不被迁移到远端NUMA。

**需求来源**: 腾讯POC可靠性需求

**优先级**: P1

**验收标准**:
- 共享页迁移率 = 0%（确保不迁移）
- 迁移后数据一致性100%保持
- 系统稳定性不受影响
- 用户无需任何额外接口调用

---

## AR拆分

### AR: 共享页过滤完整实现

| 属性 | 内容 |
| --- | --- |
| **AR编号** | AR-SMAP-SHARED-PAGE-FILTER |
| **AR名称** | 共享页识别、白名单管理与迁移过滤完整实现 |
| **AR描述** | 在进程纳管时自动识别共享内存页面（4K进程的mmap(MAP_SHARED)、POSIX shm、System V shm、tmpfs，2M虚拟机的共享内存模式），将共享页添加到白名单位图whiteListBm并填充isWhiteListPage标记。迁移策略计算和执行时自动过滤白名单页面，确保共享页不被迁移。提供日志记录和debugfs状态查询能力 |
| **依赖AR** | 无 |
| **实现方式** | 1. ProcessAddManage内部调用IdentifySharedPages4K解析/proc/[pid]/maps，调用ParseMmapType识别VM共享模式<br>2. AddSharedPagesToWhiteList将共享页地址设置到whiteListBm位图<br>3. FillActcByBitmap填充isWhiteListPage标记<br>4. 复用separate_strategy.c中ActcFreqAscFunc排序、CollectPages选择，自动跳过isWhiteListPage=true的页面<br>5. ClearProcessWhiteList在进程移除时清除白名单<br>6. 日志和debugfs节点提供DFX支持 |
| **关键数据结构** | AddrRange、ProcessMemBitmap.whiteListBm、ActcData.isWhiteListPage、MmapType、WhiteListStats |
| **验收标准** | 1. 正确识别所有共享内存类型<br>2. 白名单位图正确设置，isWhiteListPage标记正确填充<br>3. 共享页迁移率 = 0%<br>4. 迁移后数据一致性100%保持<br>5. 进程移除时白名单自动清除<br>6. 识别延迟 < 50ms，位图内存占用 < 1MB/进程<br>7. DFX日志和debugfs节点正常工作 |
| **涉及文件** | src/user/manage/manage.c、src/user/manage/manage.h、src/user/strategy/separate_strategy.c、内核驱动 |

---

## AR工作量估算

| AR | 估算工作量 | 说明 |
| --- | --- | --- |
| AR-SMAP-SHARED-PAGE-FILTER | 3人日 | maps解析、白名单管理、迁移过滤验证、DFX，约500行代码 |

---

## 非功能性需求

| 类型 | 需求 |
| --- | --- |
| **性能** | 共享页识别延迟 < 50ms（单进程） |
| **性能** | 迁移过滤开销对正常迁移无额外损耗 |
| **性能** | 白名单位图内存占用 < 1MB/进程 |
| **可靠性** | 共享页迁移率 = 0%（确保不迁移） |
| **可靠性** | 迁移后数据一致性100%保持 |
| **可靠性** | 识别失败不阻断进程纳管流程 |
| **可维护性** | 无新增对外接口，用户使用方式不变 |
| **可维护性** | 复用现有数据结构和过滤机制 |

---

## 测试覆盖

| 测试类型 | 测试场景 |
| --- | --- |
| 单元测试 | maps解析：MAP_SHARED标志、shm路径、tmpfs路径 |
| 单元测试 | 边界值：256段共享内存、空maps、无效pid |
| 功能测试 | 进程纳管后白名单正确、移除后白名单清除 |
| 功能测试 | 共享页不迁移、私有页正常迁移 |
| 可靠性测试 | 迁移后共享内存数据一致 |
| 功能测试 | debugfs节点查询正确、日志输出完整 |
| 性能测试 | 识别延迟、过滤开销、内存占用 |