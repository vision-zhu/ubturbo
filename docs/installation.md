# UBTurbo 安装指南

## 适用范围

本文说明从源码构建 UBTurbo、生成 RPM、安装运行文件以及验证部署。运行完整的内存迁移能力还必须
安装 SMAP；仅编译 UBTurbo 单元测试不要求加载 SMAP 内核模块。

## 环境要求

| 项目       | 要求                           |
| ---------- | ------------------------------ |
| CPU 架构   | aarch64                        |
| 操作系统   | openEuler；推荐 24.03 LTS 系列 |
| 构建系统   | CMake 3.22 或更高版本          |
| 语言标准   | C++17 / C11                    |
| 内核开发包 | 构建 SMAP 时与运行内核严格匹配 |

安装基础依赖：

```bash
sudo dnf install -y make gcc gcc-c++ cmake ninja-build dos2unix chrpath \
    patchelf libboundscheck libvirt-devel kernel-devel findutils git
```

生成 RPM 还需要：

```bash
sudo dnf install -y rpm-build
```

## 获取源码

```bash
git clone https://gitcode.com/openeuler/ubturbo.git
cd ubturbo
git submodule update --init --recursive
dos2unix build.sh
```

## 源码构建

Release 构建：

```bash
./build.sh
```

默认 Release 产物位于：

- `dist/release/bin/ub_turbo_exec`
- `dist/release/lib/libubturbo_client.so`
- `dist/release/conf/ubturbo.conf`
- `dist/release/conf/ubturbo_plugin_admission.conf`

## 构建 RPM

```bash
./build.sh package
```

CPack 使用 `ubturbo-rmrs` 组件名，软件版本和 openEuler 发行标识由 CMake 配置生成。不要在自动化脚本
中硬编码完整 RPM 文件名；使用构建输出目录中的实际文件。

安装前检查包信息：

```bash
rpm -qpi <RPM_FILE>
sudo rpm -Uvh <RPM_FILE>
```

## 安装 SMAP

SMAP 包含 `libsmap.so` 及扫描、迁移内核模块。推荐通过配套 RPM 安装。需要从源码构建时，参见
[SMAP 文档](smap/README.md)。

加载顺序为：

```bash
sudo insmod smap_tracking_core.ko
sudo insmod smap_histogram_tracking.ko
sudo insmod smap_access_tracking.ko enable_hist=0
sudo insmod smap_tiering.ko
```

`enable_hist=0`（默认值）使用软件 AF 扫描，不依赖 HIST 硬件；`enable_hist=1` 启用 HIST 硬件扫描，
仅适用于具备配套 HIST 能力且已完成平台适配的环境。尽管软件扫描不使用 HIST 硬件，
`smap_access_tracking.ko` 对 `smap_histogram_tracking.ko` 存在模块符号依赖，因此后者仍需先行加载。
扫描方式是 access 模块的加载参数，切换时需要停止使用者并重新加载相关模块。页面类型则由
`ubturbo_smap_start(pageType)` 在运行时指定。

卸载前停止所有 SMAP 使用者，再按相反依赖顺序卸载：

```bash
sudo rmmod smap_tiering
sudo rmmod smap_access_tracking
sudo rmmod smap_histogram_tracking
sudo rmmod smap_tracking_core
```

## 关闭冲突的内存管理机制

启动 SMAP 前必须关闭 Linux 的 NUMA Balance：

```bash
sudo sysctl -w kernel.numa_balancing=0
sysctl kernel.numa_balancing
```

NUMA Balance 会根据页面访问情况自动调整页面所在的 NUMA 节点。若保持开启，它可能将 SMAP 已经
迁移到远端 NUMA 的页面再次迁回本地，导致迁移比例无法稳定，并干扰冷热识别和性能测试结果。生产
环境应通过系统配置持久化该设置，确保服务重启后仍为 `kernel.numa_balancing = 0`。

同时必须确保 Linux DAMON（Data Access MONitor）没有运行。DAMON 与 SMAP 都会采集内存访问特征，
同时扫描同一进程或地址空间时会产生额外开销，并可能相互干扰扫描周期及冷热判断。先检查 DAMON
管理接口和 DAMON reclaim 状态：

```bash
grep -H . /sys/kernel/mm/damon/admin/kdamonds/*/state 2>/dev/null || true
cat /sys/module/damon_reclaim/parameters/enabled 2>/dev/null || true
```

若存在运行中的 kdamond，停止所有 DAMON 使用者后将其关闭：

```bash
for state_file in /sys/kernel/mm/damon/admin/kdamonds/*/state; do
    test -e "$state_file" || continue
    echo off | sudo tee "$state_file"
done

if test -e /sys/module/damon_reclaim/parameters/enabled; then
    echo N | sudo tee /sys/module/damon_reclaim/parameters/enabled
fi
```

不同内核版本的 DAMON sysfs 路径可能不同；若系统通过其他服务或工具启动 DAMON，还应停止对应服务，
并确认测试期间没有新的 DAMON 监控上下文被创建。内核未启用 DAMON 时相关路径不存在，检查命令会
直接跳过。

## 配置并启动

默认不启用任何插件。先按[配置参考](configuration.md)检查插件共享库和配置，再修改
`/opt/ubturbo/conf/ubturbo_plugin_admission.conf`。

RPM 安装场景使用 systemd：

```bash
sudo systemctl start ubturbo
systemctl status ubturbo
```

源码构建场景可在保持 `bin/`、`lib/`、`conf/` 相对布局的前提下运行：

```bash
cd dist/release/bin
./ub_turbo_exec
```

## 验证

```bash
rpm -qa | grep '^ubturbo'
lsmod | grep -E 'smap|tracking'
systemctl status ubturbo
```

同时检查日志中是否出现配置解析、SMAP 加载、插件加载或 UDS 监听失败。服务启动成功只表明进程
完成初始化，不代表业务 PID、远端 NUMA 容量或外部资源管理链路已经可用。

## 容器说明

`docker/ubturbo.Dockerfile` 和 `docker/run_verify.sh` 用于构建验证环境。SMAP 涉及宿主机内核模块、设备
节点和特权操作，容器部署不能隔离这些宿主机依赖。生产环境不得直接照搬验证脚本中的宽权限设置。

## 卸载

先停止服务和所有 SMAP 调用者，再卸载软件包和内核模块：

```bash
sudo systemctl stop ubturbo
sudo rpm -e ubturbo-rmrs
```

是否删除 `/opt/ubturbo/conf`、日志和 `/dev/shm` 状态文件应由数据保留策略决定，不应在卸载脚本外
直接递归删除整个目录。
