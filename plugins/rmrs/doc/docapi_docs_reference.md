# UBTurboRMRSAgentMigrateStrategy:内存迁出策略

## 库 LIBRARY

UBTurbo客户端库 (libubturbo_client.so)

## 摘要 SYNOPSIS

```cpp
#include "turbo_rmrs_interface.h"

uint32_t UBTurboRMRSAgentMigrateStrategy(const MigrateStrategyParam &migrateStrategyParam, MigrateStrategyResult &migrateStrategyResult);
```

## 描述 DESCRIPTION

根据输入需要迁出本地内存大小和每个虚拟机对应最大迁出比例，确定虚拟机迁出的具体比例和对应远端NUMA。

## 参数 Parameters

| name                  | IN/OUT | description                                                  |
| --------------------- | ------ | ------------------------------------------------------------ |
| MigrateStrategyParam  | IN     | struct MigrateStrategyParam {<br/>    std::vector<VMPresetParam> vmInfoList;                                     // 虚拟机列表及最大迁出比<br/>    std::uint64_t borrowSize;                                                  // 需要匀出本地内存大小<br/>    std::vector<RemoteNumaSocketInfo> remoteNumaInfo;                          // 远端numa借出信息<br/>    std::unordered_map<std::string, std::vector<MemNodeDataNew>> nodeTopology; // 获取全量拓扑信息<br/>    std::string nodeId;                                                        // 从节点Id<br/>};<br/>struct VMPresetParam {<br/>    pid_t pid;      // vm对应pid<br/>    uint16_t ratio; // 迁出最大比例<br/>};<br/>struct RemoteNumaSocketInfo {<br/>    int16_t borrowRemoteNuma{-1}; // 借入numa, remote 借用时有效，否则为-1<br/>    std::string lentNode{};       // 借出节点<br/>    uint16_t lentSocketId{0};     // 借出内存socketId<br/>};<br/>struct NumaData {<br/>    std::string numaId{};<br/>    bool operator==(const NumaData &numaData) const<br/>    {<br/>        return numaId == numaData.numaId;<br/>    }<br/>};<br/>struct CpuData {<br/>    std::string CpuId{};<br/>    bool operator==(const CpuData &cpuData) const<br/>    {<br/>        return CpuId == cpuData.CpuId;<br/>    }<br/>};<br/>struct SocketData {<br/>    std::string socketId{};<br/>    std::vector<NumaData> numas{};<br/>    std::vector<CpuData> cpus{};<br/>    bool operator==(const SocketData &socketData) const<br/>    {<br/>        return socketId == socketData.socketId && numas == socketData.numas && cpus == socketData.cpus;<br/>    }<br/>};<br/>struct MemNodeDataNew {<br/>    std::string nodeId{};      // 节点名<br/>    SocketData socket{};       // socket 数据<br/>    std::string hostname{};    // 主机名<br/>    bool isRegisterRm = false; // 该节点是否有可连接的 RM，非 OS 固定为 false<br/>}; |
| MigrateStrategyResult | OUT    | struct VMMigrateOutParam {<br/>    pid_t pid;<br/>    uint64_t memSize;   // 迁出预设大小<br/>    uint16_t desNumaId; // 迁移远端numa<br/>};<br/>struct MigrateStrategyResult {<br/>    std::vector<VMMigrateOutParam> vmInfoList;<br/>    uint64_t waitingTime; // 单位ms<br/>}; |

## 返回值 RETURN VALUE

返回 `0` 表示成功，返回其他值表示失败，请见`错误 ERRORS`

## 错误 ERRORS

返回值1：通用错误码

返回值2：socket创建失败

返回值3：与server通信失败

返回值4：未找到对应函数

返回值5：函数执行错误

返回值6：函数返回值不合法


## 约束 CONSTRAINTS

暂无

## 附注 NOTES

暂无

## 样例 EXAMPLES

以下程序完成内存迁出策略调用。

```c
#include <iostream>
#include "turbo_rmrs_interface.h"

const char *const LIBUBTURBO_CLIENT_PATH = "/usr/lib64/libubturbo_client.so";
void *ubturboClientHandle = nullptr;
UBTurboRMRSAgentMigrateStrategy UBTurboRMRSAgentMigrateStrategy = nullptr;

int main()
{
    ubturboClientHandle = dlopen(LIBUBTURBO_CLIENT_PATH, RTLD_LAZY);
	UBTurboRMRSAgentMigrateStrategy = reinterpret_cast<UBTurboRMRSAgentMigrateStrategy>(dlsym(ubturboClientHandle, "UBTurboRMRSAgentMigrateStrategy"));
	
    MigrateStrategyParam migrateStrategyParam;
    /*
    填充对应参数
    */
    MigrateStrategyResult migrateStrategyResult;
    auto ret = UBTurboRMRSAgentMigrateStrategy(migrateStrategyParam, migrateStrategyResult);
    return 0;
}
```

# UBTurboRMRSAgentMigrateExecute:内存迁出执行

## 库 LIBRARY

UBTurbo客户端库 (libubturbo_client.so)

## 摘要 SYNOPSIS

```cpp
uint32_t UBTurboRMRSAgentMigrateExecute(const MigrateStrategyResult &migrateStrategyResult);
```

## 描述 DESCRIPTION

内存迁出执行。

## 参数 Parameters

| name                  | IN/OUT | description                                                  |
| --------------------- | ------ | ------------------------------------------------------------ |
| MigrateStrategyResult | IN     | struct VMMigrateOutParam {<br/>    pid_t pid;<br/>    uint64_t memSize;   // 迁出预设比例<br/>    uint16_t desNumaId; // 迁移远端numa<br/>};<br/>struct MigrateStrategyResult {<br/>    std::vector<VMMigrateOutParam> vmInfoList;<br/>    uint64_t waitingTime; // 单位ms, 范围10s-3min<br/>}; |

## 返回值 RETURN VALUE

返回 `0` 表示成功，返回其他值表示失败，请见`错误 ERRORS`

## 错误 ERRORS

返回值1：通用错误码
返回值2：socket创建失败
返回值3：与server通信失败
返回值4：未找到对应函数
返回值5：函数执行错误
返回值6：函数返回值不合法

## 约束 CONSTRAINTS

暂无

## 附注 NOTES

暂无

## 样例 EXAMPLES

以下程序完成虚拟机内存迁出功能。

```c
#include <iostream>
#include "turbo_rmrs_interface.h"

const char *const LIBUBTURBO_CLIENT_PATH = "/usr/lib64/libubturbo_client.so";
void *ubturboClientHandle = nullptr;
UBTurboRMRSAgentMigrateExecute UBTurboRMRSAgentMigrateExecute = nullptr;

int main()
{
    ubturboClientHandle = dlopen(LIBUBTURBO_CLIENT_PATH, RTLD_LAZY);
	UBTurboRMRSAgentMigrateExecute = reinterpret_cast<UBTurboRMRSAgentMigrateExecute>(dlsym(ubturboClientHandle, "UBTurboRMRSAgentMigrateExecute"));
	
    MigrateStrategyResult migrateStrategyResult;
    /*
    填充对应参数
    */
    ret = UBTurboRMRSAgentMigrateExecute(migrateStrategyResult);
    return 0;
}
```

# UBTurboRMRSAgentMigrateBack:内存归还

## 库 LIBRARY

UBTurbo客户端库 (libubturbo_client.so)

## 摘要 SYNOPSIS

```cpp
#include "turbo_rmrs_interface.h"

uint32_t UBTurboRMRSAgentMigrateBack(MigrateBackResult &migrateBackResult);
```

## 描述 DESCRIPTION

检查并迁回当前节点上可迁回的远端虚拟机，计算可归还的远端NUMA列表。

## 参数 Parameters

| name              | IN/OUT | description                                                  |
| ----------------- | ------ | ------------------------------------------------------------ |
| MigrateBackResult | IN     | class MigrateBackResult {<br/>public:<br/>    uint32_t result{};                   <br/>    std::vector<uint16_t> NumaIds{};     // 决策结果，表示哪些远端numaID可以归还<br/>}; |

## 返回值 RETURN VALUE

返回 `0` 表示成功，返回其他值表示失败，请见`错误 ERRORS`

## 错误 ERRORS

返回值1：通用错误码

返回值2：socket创建失败

返回值3：与server通信失败

返回值4：未找到对应函数

返回值5：函数执行错误

返回值6：函数返回值不合法

## 约束 CONSTRAINTS

暂无

## 附注 NOTES

暂无

## 样例 EXAMPLES

以下程序完成内存归还部分功能（将虚拟机迁回本地numa，计算可归还的远端NUMA列表）。

```c
#include <iostream>
#include "turbo_rmrs_interface.h"

const char *const LIBUBTURBO_CLIENT_PATH = "/usr/lib64/libubturbo_client.so";
void *ubturboClientHandle = nullptr;
UBTurboRMRSAgentMigrateBack UBTurboRMRSAgentMigrateBack = nullptr;

int main()
{
    ubturboClientHandle = dlopen(LIBUBTURBO_CLIENT_PATH, RTLD_LAZY);
	UBTurboRMRSAgentMigrateBack = reinterpret_cast<UBTurboRMRSAgentMigrateBack>(dlsym(ubturboClientHandle, "UBTurboRMRSAgentMigrateBack"));
	
    MigrateBackResult migrateBackResult;
    /*
    填充对应参数
    */
    auto ret = UBTurboRMRSAgentMigrateBack(migrateBackResult);
    return 0;
}
```

# UBTurboRMRSAgentBorrowRollBack:内存借用回滚

## 库 LIBRARY

UBTurbo客户端库 (libubturbo_client.so)

## 摘要 SYNOPSIS

```cpp
uint32_t UBTurboRMRSAgentBorrowRollBack(std::map<std::string, std::set<BorrowIdInfo>> &borrowIdsPidsMap);
```

## 描述 DESCRIPTION

内存借用回滚的迁回部分的回调函数。

## 参数 Parameters

| name             | IN/OUT | description                                                  |
| ---------------- | ------ | ------------------------------------------------------------ |
| borrowIdsPidsMap | IN     | std::map<std::string, std::set<BorrowIdInfo>><br/>（key为borrowId）<br/>struct BorrowIdInfo {<br/>    pid_t pid;<br/>    uint64_t oriSize;    // 对应pid之前占用远端内存大小<br/>}; |

## 返回值 RETURN VALUE

返回 `0` 表示成功，返回其他值表示失败，请见`错误 ERRORS`

## 错误 ERRORS

返回值1：通用错误码

返回值2：socket创建失败

返回值3：与server通信失败

返回值4：未找到对应函数

返回值5：函数执行错误

返回值6：函数返回值不合法

## 约束 CONSTRAINTS

暂无

## 附注 NOTES

暂无

## 样例 EXAMPLES

以下程序完成内存借用回滚的迁回部分。

```c
#include <iostream>
#include "turbo_rmrs_interface.h"

const char *const LIBUBTURBO_CLIENT_PATH = "/usr/lib64/libubturbo_client.so";
void *ubturboClientHandle = nullptr;
UBTurboRMRSAgentBorrowRollBack UBTurboRMRSAgentBorrowRollBack = nullptr;

int main()
{
    ubturboClientHandle = dlopen(LIBUBTURBO_CLIENT_PATH, RTLD_LAZY);
	UBTurboRMRSAgentBorrowRollBack = reinterpret_cast<UBTurboRMRSAgentBorrowRollBack>(dlsym(ubturboClientHandle, "UBTurboRMRSAgentBorrowRollBack"));
	
    std::map<std::string, std::set<BorrowIdInfo>> curBorrowIdsPidsMap;
    /*
    填充对应参数
    */
    auto ret = UBTurboRMRSAgentBorrowRollBack(curBorrowIdsPidsMap);
    return 0;
}
```

# PidNumaInfoCollectRecvHandler:容器进程内存采集

## 库 LIBRARY

UBTurbo客户端库 (libubturbo_client.so)

## 摘要 SYNOPSIS

```cpp
uint32_t UBTurboRMRSAgentPidNumaInfoCollect(const PidNumaInfoCollectParam &pidNumaInfoCollectParam, PidNumaInfoCollectResult &pidNumaInfoCollectResult);
```

## 描述 DESCRIPTION

RMRS提供容器进程内存采集。

## 参数 Parameters

| name                     | IN/OUT | description                                                  |
| ------------------------ | ------ | ------------------------------------------------------------ |
| PidNumaInfoCollectParam  | IN     | class PidNumaInfoCollectParam : public Serializer { <br/>public: <br/>PidNumaInfoCollectParam() {} <br/>explicit PidNumaInfoCollectParam(std::vector<pid_t> pidList) : pidList(pidList) {} <br/>std::vector<pid_t> pidList{}; <br/> }; |
| PidNumaInfoCollectResult | OUT    | class PidNumaInfoCollectResult : public Serializer { <br/>public: <br/>PidNumaInfoCollectResult() {} <br/>explicit PidNumaInfoCollectResult(std::vector< mempooling::PidInfo> pidInfoList) <br/>: pidInfoList(pidInfoList) {} <br/>std::vector< mempooling::PidInfo> pidInfoList{};  <br/> |

## 返回值 RETURN VALUE

返回 `0` 表示成功，返回其他值表示失败，请见`错误 ERRORS`

## 错误 ERRORS

返回值1：通用错误码

返回值2：socket创建失败

返回值3：与server通信失败

返回值4：未找到对应函数

返回值5：函数执行错误

返回值6：函数返回值不合法

## 约束 CONSTRAINTS

暂无

## 附注 NOTES

暂无

## 样例 EXAMPLES

以下程序完成RMRS容器进程内存采集。

```c
#include <iostream>
#include "turbo_rmrs_interface.h"

const char *const LIBUBTURBO_CLIENT_PATH = "/usr/lib64/libubturbo_client.so";
void *ubturboClientHandle = nullptr;
UBTurboRMRSAgentPidNumaInfoCollect UBTurboRMRSAgentPidNumaInfoCollect = nullptr;

int main()
{
    ubturboClientHandle = dlopen(LIBUBTURBO_CLIENT_PATH, RTLD_LAZY);
	UBTurboRMRSAgentPidNumaInfoCollect =
        reinterpret_cast<UBTurboRMRSAgentPidNumaInfoCollect>(dlsym(ubturboClientHandle, "UBTurboRMRSAgentPidNumaInfoCollect"));
	
    turbo::rmrs::PidNumaInfoCollectParam pidNumaInfoCollectParam;
    turbo::rmrs::PidNumaInfoCollectResult pidNumaInfoCollectResult;
    /*
    填充对应参数
    */
    auto ret = MempoolingMessage::UBTurboRMRSAgentPidNumaInfoCollect(pidNumaInfoCollectParam, pidNumaInfoCollectResult);
    return 0;
}
```

# UBTurboRMRSAgentNumaMemInfoCollect:容器进程内存采集

## 库 LIBRARY

UBTurbo客户端库 (libubturbo_client.so)

## 摘要 SYNOPSIS

```cpp
uint32_t UBTurboRMRSAgentNumaMemInfoCollect(const NumaMemInfoCollectParam &numaMemInfoCollectParam, ResponseInfoSimpo &responseInfoSimpo);
```

## 描述 DESCRIPTION

RMRS提供容器进程内存采集。

## 参数 Parameters

| name                    | IN/OUT | description                                                  |
| ----------------------- | ------ | ------------------------------------------------------------ |
| NumaMemInfoCollectParam | IN     | class NumaMemInfoCollectParam : public Serializer { <br/>public: <br/>NumaMemInfoCollectParam() {} <br/>explicit NumaMemInfoCollectParam(int numaId) : <br/>numaId(numaId) {} <br/>int numaId{}; <br/> |
| ResponseInfoSimpo       | OUT    | class ResponseInfoSimpo { </br>public: ResponseInfoSimpo() = default;<br> explicit ResponseInfoSimpo(ResponseInfo responseInfoInput) : responseInfo_(std::move(responseInfoInput)) {}<br> inline ResponseInfo GetResponseInfo() { <br> return responseInfo_; <br> }<br> inline void SetResponseInfo(const int code, const std::string &message) { </br>responseInfo_.code = code; </br>responseInfo_.message = message;<br>  }<br> std::string ToString() const { </br>return "code=" + std::to_string(responseInfo_.code) + ", message=" + responseInfo_.message; <br>}<br> ResponseInfo responseInfo_{}; </br>}; |

## 返回值 RETURN VALUE

返回 `0` 表示成功，返回其他值表示失败，请见`错误 ERRORS`

## 错误 ERRORS

返回值1：通用错误码

返回值2：socket创建失败

返回值3：与server通信失败

返回值4：未找到对应函数

返回值5：函数执行错误

返回值6：函数返回值不合法

## 约束 CONSTRAINTS

暂无

## 附注 NOTES

暂无

## 样例 EXAMPLES

以下程序完成RMRS容器进程内存采集。

```c
#include <iostream>
#include "turbo_rmrs_interface.h"

const char *const LIBUBTURBO_CLIENT_PATH = "/usr/lib64/libubturbo_client.so";
void *ubturboClientHandle = nullptr;
UBTurboRMRSAgentNumaMemInfoCollect UBTurboRMRSAgentNumaMemInfoCollect = nullptr;

int main()
{
    ubturboClientHandle = dlopen(LIBUBTURBO_CLIENT_PATH, RTLD_LAZY);
	UBTurboRMRSAgentNumaMemInfoCollect =
        reinterpret_cast<UBTurboRMRSAgentNumaMemInfoCollect>(dlsym(ubturboClientHandle, "UBTurboRMRSAgentNumaMemInfoCollect"));
	
   turbo::rmrs::NumaMemInfoCollectParam numaMemInfoCollectParam;
   turbo::rmrs::ResponseInfoSimpo responseInfoSimpo;
    /*
    填充对应参数
    */
    auto ret = MempoolingMessage::UBTurboRMRSAgentNumaMemInfoCollect(numaMemInfoCollectParam, responseInfoSimpo);
    return 0;
}
```

# UBTurboRMRSAgentUCacheMigrateStrategy:pagecache迁移策略执行

## 库 LIBRARY

UBTurbo客户端库 (libubturbo_client.so)

## 摘要 SYNOPSIS

```cpp
#include "turbo_rmrs_interface.h"

uint32_t UBTurboRMRSAgentUCacheMigrateStrategy(const UCacheMigrationStrategyParam &uCacheMigrationStrategy, ResCode &rescode);
```

## 描述 DESCRIPTION

RMRS提供pagecache迁移策略执行。

## 参数 Parameters

| name                    | IN/OUT | description                                                  |
| ----------------------- | ------ | ------------------------------------------------------------ |
| uCacheMigrationStrategy | IN     | struct UCacheMigrationStrategyParam {<br/>    int16_t localNumaId;                 // 执行迁出的本地numa节点。若小于0，代表所有本地numa节点<br/>    std::vector<uint16_t> remoteNumaIds; // 执行迁入的远端内存呈现numa节点列表<br/>    std::vector<pid_t> pids;             // 需要迁移的进程列表<br/>    float ucacheUsageRatio;              // 给Pagecache分配使用的内存比例<br/>}; |
| resCode                 | OUT    | struct ResCode {<br/>    uint32_t resCode;<br/>};            |

## 返回值 RETURN VALUE

返回 `0` 表示成功，返回其他值表示失败，请见`错误 ERRORS`

## 错误 ERRORS

返回值1：通用错误码

返回值2：socket创建失败

返回值3：与server通信失败

返回值4：未找到对应函数

返回值5：函数执行错误

返回值6：函数返回值不合法

## 约束 CONSTRAINTS

暂无

## 附注 NOTES

暂无

## 样例 EXAMPLES

以下程序完成内存迁出策略调用。

```c
#include <iostream>
#include "turbo_rmrs_interface.h"

const char *const LIBUBTURBO_CLIENT_PATH = "/usr/lib64/libubturbo_client.so";
void *ubturboClientHandle = nullptr;
UBTurboRMRSAgentUCacheMigrateStrategy UBTurboRMRSAgentUCacheMigrateStrategy = nullptr;

int main()
{
    ubturboClientHandle = dlopen(LIBUBTURBO_CLIENT_PATH, RTLD_LAZY);
	UBTurboRMRSAgentUCacheMigrateStrategy = reinterpret_cast<UBTurboRMRSAgentUCacheMigrateStrategy>(dlsym(ubturboClientHandle, "UBTurboRMRSAgentUCacheMigrateStrategy"));
	
    UCacheMigrationStrategyParam param;
    ResCode result;
    /*
    填充对应参数
    */
    auto ret = UBTurboRMRSAgentUCacheMigrateStrategy(param, result);
    return 0;
}
```

# UBTurboRMRSAgentUCacheMigrateStop:停止pagecache迁移

## 库 LIBRARY

UBTurbo客户端库 (libubturbo_client.so)

## 摘要 SYNOPSIS

```cpp
#include "turbo_rmrs_interface.h"

uint32_t UBTurboRMRSAgentUCacheMigrateStop(ResCode &rescode);
```

## 描述 DESCRIPTION

RMRS提供停止pagecache迁移。

## 参数 Parameters

| name    | IN/OUT | description                                       |
| ------- | ------ | ------------------------------------------------- |
| ResCode | OUT    | struct ResCode {<br/>    uint32_t resCode;<br/>}; |

## 返回值 RETURN VALUE

返回 `0` 表示成功，返回其他值表示失败，请见`错误 ERRORS`

## 错误 ERRORS

返回值1：通用错误码

返回值2：socket创建失败

返回值3：与server通信失败

返回值4：未找到对应函数

返回值5：函数执行错误

返回值6：函数返回值不合法

## 约束 CONSTRAINTS

暂无

## 附注 NOTES

暂无

## 样例 EXAMPLES

以下程序完成停止pagecache迁移。

```c
#include <iostream>
#include "turbo_rmrs_interface.h"

const char *const LIBUBTURBO_CLIENT_PATH = "/usr/lib64/libubturbo_client.so";
void *ubturboClientHandle = nullptr;
UBTurboRMRSAgentUCacheMigrateStop UBTurboRMRSAgentUCacheMigrateStop = nullptr;

int main()
{
    ubturboClientHandle = dlopen(LIBUBTURBO_CLIENT_PATH, RTLD_LAZY);
	UBTurboRMRSAgentUCacheMigrateStop = reinterpret_cast<UBTurboRMRSAgentUCacheMigrateStop>(dlsym(ubturboClientHandle, "UBTurboRMRSAgentUCacheMigrateStop"));
	
    ResCode result;
    auto ret = UBTurboRMRSAgentUCacheMigrateStop(result);
    return 0;
}
```





# UBTurboRMRSAgentUpdateUCacheRatio:计算Pagecache远端内存使用比例

## 库 LIBRARY

UBTurbo客户端库 (libubturbo_client.so)

## 摘要 SYNOPSIS

```cpp
#include "turbo_rmrs_interface.h"

uint32_t UBTurboRMRSAgentUpdateUCacheRatio(const MigrationInfoParam &migrationInfoParam, UCacheRatioRes &uCacheRatioRes);
```

## 描述 DESCRIPTION

RMRS提供计算Pagecache远端内存使用比例。

## 参数 Parameters

| name               | IN/OUT | description                                                  |
| ------------------ | ------ | ------------------------------------------------------------ |
| MigrationInfoParam | IN     | struct MigrationInfoParam {<br/>    uint64_t borrowMemKB;    // 借用内存总大小，单位KB<br/>    std::vector<pid_t> pids; // 需要迁移的进程列表<br/>}; |
| UCacheRatioRes     | OUT    | struct UCacheRatioRes {<br/>    float ucacheUsageRatio; // ucache借用比例<br/>    uint32_t resCode;       // 是否执行成功<br/>}; |

## 返回值 RETURN VALUE

返回 `0` 表示成功，返回其他值表示失败，请见`错误 ERRORS`

## 错误 ERRORS

返回值1：通用错误码

返回值2：socket创建失败

返回值3：与server通信失败

返回值4：未找到对应函数

返回值5：函数执行错误

返回值6：函数返回值不合法

## 约束 CONSTRAINTS

暂无

## 附注 NOTES

暂无

## 样例 EXAMPLES

以下程序完成计算Pagecache远端内存使用比例。

```c
#include <iostream>
#include "turbo_rmrs_interface.h"

const char *const LIBUBTURBO_CLIENT_PATH = "/usr/lib64/libubturbo_client.so";
void *ubturboClientHandle = nullptr;
UBTurboRMRSAgentUpdateUCacheRatio UBTurboRMRSAgentUpdateUCacheRatio = nullptr;

int main()
{
    ubturboClientHandle = dlopen(LIBUBTURBO_CLIENT_PATH, RTLD_LAZY);
	UBTurboRMRSAgentUpdateUCacheRatio = reinterpret_cast<UBTurboRMRSAgentUpdateUCacheRatio>(dlsym(ubturboClientHandle, "UBTurboRMRSAgentUpdateUCacheRatio"));
	
    MigrationInfoParam param;
    UCacheRatioRes result;
    /*
    填充对应参数
    */
    auto ret = UBTurboRMRSAgentUpdateUCacheRatio(param, result);
    return 0;
}
```

