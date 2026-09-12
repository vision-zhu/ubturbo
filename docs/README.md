# 📑 UBTurbo 文档中心

本目录集中维护 UBTurbo 框架及所有插件文档。源码目录只保存实现、构建和必要的许可证材料；新增或
修改插件文档时，应在对应的 `docs/<plugin>/` 子目录中完成，并从本页和仓库根 README 建立入口。

## 框架文档

| 文档 | 内容 |
| ---- | ---- |
| [安装指南](installation.md) | 构建、RPM、SMAP 安装和验证 |
| [用户指南](user_guide.md) | 配置、服务管理、运行检查和排障 |
| [配置参考](configuration.md) | 框架及插件配置项 |
| [架构设计](architecture.md) | 系统边界、生命周期和数据流 |
| [开发者指南](developer_guide.md) | SDK 调用、插件开发和测试 |
| [API 参考](api_reference.md) | UBTurbo 公共接口 |
| [实践教程](tutorial.md) | IPC、RMRS 和 SMAP 集成示例 |
| [IPC Python 客户端](../tools/ubturbo_ipc/README.md) | 服务启动后通过 UDS 调用 SMAP 能力 |
| [安全说明](security.md) | 信任边界、权限模型和部署检查 |
| [版本说明](release_notes.md) | 当前能力、限制和维护要求 |
| [Roadmap](roadmap.md) | 当前版本、规划方向和维护原则 |
| [FAQ](FAQ.md) | 构建、插件、SMAP、IPC 和测试常见问题 |

## 插件文档

| 插件 | 文档入口 | 主要内容 |
| ---- | -------- | -------- |
| RMRS | [docs/rmrs](rmrs/README.md) | 架构、使用、排障和 IPC API |
| SMAP | [docs/smap](smap/README.md) | 架构、使用、算法、接口和性能测试 |
| UBDMA | [docs/ubdma](ubdma/README.md) | UB/URMA DMA 架构、安装和排障 |
| UCache | [docs/ucache](ucache/README.md) | 页面缓存迁移插件的能力、启用和约束 |

## 维护约定

- 插件文档统一放在 `docs/<plugin>/`，插件图片放在 `docs/<plugin>/images/`。
- 文件和目录使用小写蛇形命名；组件首页统一命名为 `README.md`，顶层常见问题保留 `FAQ.md`。
- 文档使用相对链接，不引用旧的 `doc/` 或 `plugins/<plugin>/docs/` 路径。
- 架构图优先使用可审查的 SVG，连线应从节点边界出入，不得穿过文字或无关框体。
- 修改图片后应重新渲染，检查文字截断、线框重叠、箭头遮挡和画布裁切。
