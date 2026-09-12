# UBTurbo API 参考

## 使用范围

UBTurbo 公开头文件使用 `std::string`、`std::function` 和引用类型，因此接口面向 C++17 调用方。
`extern "C"` 只固定部分导出符号名称，不会把这些参数转换为纯 C ABI。

| 头文件 | 主要调用方 | 接口 |
| --- | --- | --- |
| `turbo_ipc_client.h` | 外部进程 | IPC 调用与超时设置 |
| `turbo_ipc_server.h` | 守护进程插件 | handler 注册/注销 |
| `turbo_conf.h` | 守护进程插件 | 类型化配置读取 |
| `turbo_logger.h` | 框架和插件 | 分级日志 |
| `turbo_def.h` | 双方 | 缓冲区、handler 类型和 IPC 错误码 |

## 公共数据类型

```cpp
using TurboByteBufferFreeFunc = std::function<void(uint8_t *data)>;

struct TurboByteBuffer {
    uint8_t *data = nullptr;
    size_t len = 0;
    TurboByteBufferFreeFunc freeFunc;
};

using IpcHandlerFunc =
    std::function<uint32_t(const TurboByteBuffer &inputBuffer,
                           TurboByteBuffer &outputBuffer)>;
```

`freeFunc` 非空表示接收方需要调用它释放 `data`；为空表示该字段没有提供释放方法。结构体本身没有
析构函数，复制它不会转移所有权，也不会自动释放缓冲区。特别地，当前客户端 SDK 使用 `new[]` 分配
成功响应，但未填写 `result.freeFunc`，调用方读取后必须对 `result.data` 执行 `delete[]`。

## IPC 错误码

| 常量 | 值 | 含义 |
| --- | ---: | --- |
| `IPC_OK` | 0 | 成功 |
| `IPC_ERROR` | 1 | 通用错误 |
| `IPC_BAD_SOCKET` | 2 | socket 创建失败 |
| `IPC_BAD_CONNECT` | 3 | 连接失败 |
| `IPC_NO_FUNC` | 4 | 服务名未注册 |
| `IPC_FUNC_ERROR` | 5 | handler 返回失败 |
| `IPC_INVALID_RESULT` | 6 | 响应格式或返回结果无效 |

## 客户端接口

### UBTurboFunctionCaller

```cpp
uint32_t UBTurboFunctionCaller(const std::string &function,
                               const TurboByteBuffer &params,
                               TurboByteBuffer &result);
```

通过本机 UDS 调用 `function` 对应的服务。`params` 在调用返回前必须保持有效；成功响应由当前实现以
`new[]` 分配，读取后应 `delete[] result.data`。返回非 `IPC_OK` 时不要解析未验证的输出。

### SetIpcTimeLimit

```cpp
uint32_t SetIpcTimeLimit(uint32_t timeLimit);
```

设置客户端接收等待时间，单位为秒，默认值为 60。当前实现不限制输入范围；`0` 传给
`SO_RCVTIMEO` 表示不设置接收超时，可能无限阻塞，并非立即超时。调用方应检查返回值。超时表示本次
等待失败，不证明服务端没有开始执行业务操作。

## 服务端接口

### UBTurboRegIpcService

```cpp
uint32_t UBTurboRegIpcService(const std::string &name,
                              IpcHandlerFunc function);
```

注册唯一服务名和 handler。handler 必须验证输入长度和业务字段，并为输出设置一致的 `data`、`len`
和 `freeFunc`。

### UBTurboUnRegIpcService

```cpp
uint32_t UBTurboUnRegIpcService(const std::string &name);
```

注销服务。卸载插件前必须停止新增调用并注销其全部服务，避免 IPC server 保留失效函数对象。

## 配置接口

```cpp
uint32_t UBTurboGetUInt32(const std::string &section,
                          const std::string &configKey,
                          uint32_t &configValue);
uint32_t UBTurboGetUInt64(const std::string &section,
                          const std::string &configKey,
                          uint64_t &configValue);
uint32_t UBTurboGetFloat(const std::string &section,
                         const std::string &configKey,
                         float &configValue);
uint32_t UBTurboGetBool(const std::string &section,
                        const std::string &configKey,
                        bool &configValue);
uint32_t UBTurboGetStr(const std::string &section,
                       const std::string &configKey,
                       std::string &configValue);
```

成功返回 `0` 并写入输出；失败返回非零值，调用方不得使用输出变量的旧值或未初始化值。插件 section
通常来自配置文件名，例如 `plugin_rmrs.conf` 对应 `plugin_rmrs`。

## 日志接口

插件使用 `RACK_DEFINE_THIS_MODULE` 声明模块，并使用 `UBTURBO_LOG_DEBUG/INFO/WARN/ERROR/CRIT`
写日志。`TurboLogOutput` 用于将外部库日志接入 UBTurbo。

```cpp
RACK_DEFINE_THIS_MODULE("example", 779);
UBTURBO_LOG_INFO("example", 779) << "initialized";
```

不要记录凭证、完整 IPC 数据、页面内容或敏感地址。

## 最小 IPC 示例

```cpp
#include <cstring>
#include "turbo_ipc_client.h"

using turbo::ipc::client::UBTurboFunctionCaller;

const char payload[] = "request";
TurboByteBuffer input{
    reinterpret_cast<uint8_t *>(const_cast<char *>(payload)),
    std::strlen(payload),
    nullptr,
};
TurboByteBuffer output;

const uint32_t ret = UBTurboFunctionCaller("ExampleService", input, output);
if (ret == IPC_OK) {
    // 读取 output.data/output.len。
    delete[] output.data;
}
```

实际服务的数据格式由对应插件 API 定义。
