# SMAP 用户指南

## 前提条件

- aarch64 openEuler 目标机。
- 与 `uname -r` 匹配的内核开发包。
- `libboundscheck` 及目标平台要求的内存/互联依赖。
- 用于运行 UBTurbo 的专用用户和用户组。

## 构建

用户态库：

```bash
./build.sh -t release
```

扫描模块：

```bash
make -C src/drivers KERNEL_VERSION=openeuler -j"$(nproc)"
cp -f src/drivers/Module.symvers src/tiering/depends/
```

迁移与 UCache 模块：

```bash
make -C src/tiering KERNEL_VERSION=openeuler -j"$(nproc)"
make -C src/ucache -j"$(nproc)"
```

`KERNEL_VERSION` 选择的是源码适配分支，不会替代内核开发包版本检查。

## 加载模块

```bash
insmod src/drivers/smap_tracking_core.ko
insmod src/drivers/smap_histogram_tracking.ko
insmod src/drivers/smap_access_tracking.ko
insmod src/tiering/smap_tiering.ko
```

检查：

```bash
lsmod | grep -E 'smap|tracking'
dmesg | tail -n 100
```

页面类型不通过 `insmod` 参数指定，而是在 `ubturbo_smap_start(pageType, ...)` 时选择：

- `0`：4K 普通进程页面。
- `1`：2M 静态大页虚机。

同一 SMAP 实例不能混用两种页面类型。

## 配置

策略配置路径为 `/opt/ubturbo/conf/smap/period.config`。键值、默认值和范围见
[UBTurbo 配置参考](../../../docs/configuration.md)。修改扫描周期、迁移周期、CPU 范围或迁移模式可能
造成业务抖动，应先在隔离环境评估。

## 使用流程

1. 加载所需内核模块并准备 `libsmap.so`。
2. 启动 SMAP 并选择页面类型。
3. 设置远端 NUMA 可用容量。
4. 添加进程跟踪或配置迁出目标。
5. 通过查询接口观察频率、进程配置和迁移结果。
6. 移除 PID 管理并停止 SMAP。

完整结构体和接口约束见 [API 参考](api_reference.md)。

## 卸载

先停止 UBTurbo 和其他 `libsmap.so` 使用者，再执行：

```bash
sudo rmmod smap_tiering
sudo rmmod smap_access_tracking
sudo rmmod smap_histogram_tracking
sudo rmmod smap_tracking_core
```


## 故障排查

| 现象         | 检查项                                      |
| ------------ | ------------------------------------------- |
| 模块格式错误 | 运行内核与`kernel-devel` 是否一致         |
| 设备打开失败 | 模块加载顺序、udev 规则、运行组权限         |
| 页面类型冲突 | 共享状态文件和已有 SMAP 实例的`pageType`  |
| 扫描无数据   | PID 是否存在、页型、扫描方式和平台能力      |
| 迁移量不足   | 页面是否可迁、远端容量、NUMA 状态和策略阈值 |
| 同步迁移超时 | 系统压力、任务规模、内核日志和后续查询结果  |

清理 `/dev/shm/smap_config` 或 `/dev/shm/ubturbo_page_type.dat` 前，必须停止所有 SMAP 使用者并确认
没有其他实例需要这些状态。
