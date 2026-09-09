# SMAP

[简体中文](README.md) | [English](README_EN.md)

SMAP 是 UBTurbo 集成的多级内存扫描与页面迁移组件。它由 `libsmap.so` 用户态库以及扫描、迁移内核
模块组成，支持 4K 普通进程和 2M 静态大页虚机场景。

## 核心能力

- 使用 Linux 页表 Access Flag 进行软件访问扫描。
- 在配套硬件上选择性使用 HIST 进行远端页面访问统计。
- 根据本地冷页和远端热页选择迁移对象。
- 配置进程迁出比例/容量、迁回、紧急迁出和远端到远端迁移。
- 支持多个本地、远端 NUMA 目标和组级迁移策略。

软件 AF 扫描是可用的基础路径；HIST 是依赖特定平台的可选增强，不能将两者描述为所有环境都存在
的“软硬件协同必选流程”。

## 架构

![SMAP 分层架构](docs/images/smap-architecture.svg)

详细设计见[架构文档](docs/architecture.md)。

## 构建

用户态库：

```bash
./build.sh -t release
```

内核模块：

```bash
make -C src/drivers KERNEL_VERSION=openeuler -j"$(nproc)"
cp -f src/drivers/Module.symvers src/tiering/depends/
make -C src/tiering KERNEL_VERSION=openeuler -j"$(nproc)"
make -C src/ucache -j"$(nproc)"
```

内核开发包必须与运行内核匹配。安装和加载步骤见[用户指南](docs/user_guide.md)。

## 文档

- [架构设计](docs/architecture.md)
- [用户指南](docs/user_guide.md)
- [开发者指南](docs/developer_guide.md)
- [API 参考](docs/api_reference.md)
- [算法设计](docs/performance_algorithm.md)
- [性能测试指南](docs/performance_testing.md)
- [实践教程](docs/tutorial.md)
- [发布说明](RELEASE-NOTES.md)

## 约束

- 仅支持 aarch64 Linux/openEuler 目标环境。
- 内核模块、内核开发包、用户态库和运行内核必须配套。
- `pageType` 在 `ubturbo_smap_start` 时确定；同一实例中的后续调用必须匹配。
- PID 和 NUMA 信息必须由可信调用方提供，SMAP 不承担租户授权。

## 许可证

用户态与内核态文件的许可条款可能不同，分发时应保留源码头和 [Third Party Notice](<License/Third%20Party%20Open%20Source%20Software%20Notice.md>)。
