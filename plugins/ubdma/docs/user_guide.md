# UBDMA 用户指南

## 前提条件

- 目标平台提供受支持的 UB/URMA 能力。
- 运行内核与 `kernel-devel` 匹配，并包含构建所需的 URMA 头文件。
- 使用与目标系统、内核和硬件配套的 UBDMA RPM。

## 安装

先检查实际包文件，再安装或升级：

```bash
rpm -qpi <UBDMA_RPM>
sudo rpm -Uvh <UBDMA_RPM>
```

不要将示例中的版本号当作固定版本，也不建议使用 `--force` 绕过依赖或文件冲突。

安装后确认模块位置：

```bash
find /lib/modules -path '*/ub_dma/ub_dma.ko' -print
```

## 加载与验证

```bash
sudo insmod /lib/modules/ub_dma/ub_dma.ko
lsmod | grep '^ub_dma'
dmesg | tail -n 100
```

实际安装目录可能包含内核版本层级，应使用 RPM 文件列表或 `find` 的结果，不要盲目复制固定路径。

模块成功出现在 `lsmod` 中只表明初始化完成；还需通过目标平台提供的功能测试验证 URMA 链路、segment
注册、DMA 完成回调和数据一致性。

## 卸载

停止所有 DMA 使用者并等待在途请求完成，再执行：

```bash
sudo rmmod ub_dma
```

如果返回 busy，不要强制卸载；应定位仍持有通道、segment 或模块引用的调用方。

## 常见问题

| 现象 | 检查项 |
| --- | --- |
| 编译找不到 URMA 头文件 | 内核开发包和平台 SDK 是否配套 |
| `invalid module format` | 模块是否为当前运行内核构建 |
| segment 注册失败 | 内存状态、地址范围、权限和重复注册 |
| DMA 请求不完成 | Jetty、工作队列、中断及硬件链路 |
| 卸载 busy | 在途任务、DMA 通道和 segment 引用 |

架构和安全边界见[架构设计](architecture.md)。
