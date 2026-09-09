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

常用变体：

```bash
./build.sh -D
./build.sh -T RelWithDebInfo
./build.sh --asan
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

不建议使用 `--force`，因为它会掩盖文件冲突或包依赖问题。

## 安装 SMAP

SMAP 包含 `libsmap.so` 及扫描、迁移内核模块。推荐通过配套 RPM 安装。需要从源码构建时，参见
[SMAP 用户指南](../plugins/smap/docs/user_guide.md)。

加载顺序为：

```bash
sudo insmod smap_tracking_core.ko
sudo insmod smap_histogram_tracking.ko   # 仅在平台支持并启用 HIST 时需要
sudo insmod smap_access_tracking.ko
sudo insmod smap_tiering.ko
```

软件 AF 扫描不依赖 HIST 硬件，但当前模块依赖关系和打包策略可能仍要求安装相应文件；以目标平台
发布包说明为准。页面类型由 `ubturbo_smap_start(pageType)` 在运行时指定，不是 `insmod` 参数。

卸载前停止所有 SMAP 使用者，再按相反依赖顺序卸载：

```bash
sudo rmmod smap_tiering
sudo rmmod smap_access_tracking
sudo rmmod smap_histogram_tracking
sudo rmmod smap_tracking_core
```

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
