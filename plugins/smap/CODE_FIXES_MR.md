# plugins/smap 代码问题修复说明

本 MR 修复了在 `plugins/smap` 目录下发现的以下问题：

## 1. 运算符优先级错误 (smap_interface.c)

**文件**: `src/user/smap_interface.c`  
**位置**: 约第 577 行，`AddProcessNumaBitMap` 中

**问题**: 条件 `nodeBitmap[i] & LOCAL_NUMA_BIT_MAP_MASK == 0` 因运算符优先级被解析为  
`nodeBitmap[i] & (LOCAL_NUMA_BIT_MAP_MASK == 0)`。由于 `==` 优先于 `&`，  
`LOCAL_NUMA_BIT_MAP_MASK == 0` 恒为 0，导致条件恒为假，逻辑错误。

**修复**: 改为 `(nodeBitmap[i] & LOCAL_NUMA_BIT_MAP_MASK) == 0`，正确判断“无本地 NUMA”时设置全部本地 NUMA。

---

## 2. DoMigration 失败路径内存泄漏 (migration.c)

**文件**: `src/user/strategy/migration.c`  
**函数**: `DoMigration`

**问题**: 当 `malloc(sizeof(*tmpAddr) * mMsg->cnt)` 失败时直接返回 `-ENOMEM`，  
未释放已分配的 `mMsg->migList[i].addr`。`PostMigration` 只释放 `mMsg->migList` 数组本身，  
不释放各元素的 `addr`，导致泄漏。

**修复**: 在 `tmpAddr` 分配失败时，先循环释放 `mMsg->migList[i].addr`（并置 NULL），再返回 `-ENOMEM`。

---

## 3. 策略错误路径内存泄漏 (separate_strategy.c)

**文件**: `src/user/strategy/separate_strategy.c`

**问题 1** - `BaseStrategyInner`: 当 `mlist[from][to].addr = calloc(...)` 成功但  
`!process->scanAttr.actcData[from]` 时直接返回 `-EINVAL`，未释放刚分配的 `addr`。

**问题 2** - `BuildMlistAddr`: 同样在 `addr` 分配成功后若 `!process->scanAttr.actcData[from]`  
返回 `-EINVAL`，未释放 `mlist[from][to].addr`。

**修复**: 在上述两处返回 `-EINVAL` 前增加 `free(mlist[from][to].addr)` 并置 `addr = NULL`。

---

## 其他检查结论（本次未改）

- **manage.c / smap_config.c / access_ioctl.c** 等处的 payload/alloc 在正常与错误路径均有对应 free，未发现新泄漏。
- **period_config.c** 中 `ReadConfigByPath` 在 `break` 出循环后仍会执行 `fclose(fp)`，无文件句柄泄漏。
- **oom_migrate.c** 中 `mList->addr` / `mMsg->migList` 的分配与释放路径对应正确。
- 用户态未发现不安全的 `strcpy`/`sprintf` 等，已使用 `snprintf_s`/`sscanf_s` 等安全接口。
- `OpenNumaMaps` 使用 `popen`，调用方使用 `pclose` 关闭，无泄漏。

---

## 测试建议

- 运行 plugins/smap 相关单测（如 test/user 下用例）。
- 压力/长时间运行场景下用 valgrind 或 ASan 检查是否还有 alloc/free 不匹配。
