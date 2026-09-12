<div align="center">

<h1><img src="docs/images/ubturbo-logo.png" alt="UBTurbo" width="50%" /></h1>

In-node resource management and tiered-memory scheduling for openEuler

[![License](https://img.shields.io/badge/license-MulanPSL--2.0-orange.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-openEuler%20aarch64-blue.svg)](#software-and-hardware-compatibility)
[![Language](https://img.shields.io/badge/language-C%20%7C%20C%2B%2B-lightgrey.svg)](CMakeLists.txt)

[简体中文](README.md) | [English](README_EN.md)

</div>

UBTurbo is an in-node resource-management framework for openEuler. It provides configuration loading, dynamic
plugin management, asynchronous logging, and Unix Domain Socket (UDS) IPC. Its SMAP integration adds page scanning
and migration for tiered memory, while services such as RMRS and UCache run as plugins in the UBTurbo daemon.

> [!IMPORTANT]
> UBTurbo currently supports **aarch64 only**. SMAP, UCache, and UBDMA contain kernel modules; the build environment
> must use a kernel compatible with the runtime environment.

## Project updates

- The current package version is `1.1.1`; `ubturbo.spec` and the CPack configuration are the authoritative sources.
- The documentation is organized by audience and task. Start from the [documentation center](docs/README.md).
- See the [release notes](docs/release_notes.md) for current capabilities, known limitations, and release-maintenance
  requirements.

## Overview

UBTurbo uses a daemon as its runtime, plugins as capability extensions, and UDS as the local invocation channel.
The framework manages configuration, logging, lifecycle, and service registration. RMRS, SMAP, UCache, and UBDMA
provide resource decisions, hot/cold page identification, and page migration.

In a typical deployment, a cluster resource manager sends migration requests to RMRS through the client SDK. RMRS
uses SMAP to scan and migrate pages, while UBTurbo provides shared configuration and logging throughout the process.

## Core capabilities

- Ordered lifecycle management for configuration, logging, SMAP, plugins, and IPC.
- Runtime plugin loading through a common initialization and deinitialization contract.
- A client SDK that invokes daemon and plugin services over local UDS IPC.
- Hot/cold page identification and page migration across local and remote NUMA nodes through SMAP.
- RMRS migration decisions and execution for virtual-machine and container scenarios.

## Architecture

![UBTurbo components and data flow](docs/images/ubturbo-architecture.svg)

External processes link `libubturbo_client.so` and invoke registered services over UDS. The daemon loads RMRS,
UCache, and other admitted plugins, while its SMAP adapter loads `libsmap.so` and connects to the SMAP user-space
policy and kernel modules. See the [architecture document](docs/architecture.md) for the authoritative description.

## Repository layout

```text
├── 3rdparty/          # Third-party dependencies
├── conf/              # Daemon and plugin configuration
├── docs/              # Project documentation
├── include/           # Public headers
├── plugins/           # RMRS, SMAP, UBDMA, and UCache plugins
├── src/               # Framework, client SDK, and shared module sources
├── test/              # UBTurbo and RMRS unit tests
├── tools/             # Operations and capability invocation tools
├── CMakeLists.txt
└── build.sh
```

## Software and hardware compatibility

| Category | Supported scope |
| -------- | --------------- |
| CPU architecture | aarch64 |
| Operating system | openEuler 24.03 LTS releases |
| Build toolchain | CMake 3.22+ and a GCC toolchain with C++17/C11 support |
| RMRS dependency | `libvirt-devel` |
| SMAP dependencies | Matching `kernel-devel`, user-space library, and kernel modules |

> [!NOTE]
> CMake reads `/etc/openEuler-release`; direct builds on x86 or other Linux distributions are not currently
> supported. See [Installing SMAP](docs/installation.md#安装-smap) for its modules, load order, and runtime settings.

## Quick start

Requirements:

- An aarch64 server running an openEuler 24.03 LTS release.
- CMake 3.22 or later and a GCC toolchain with C++17/C11 support.
- `libvirt-devel` for RMRS. Running SMAP additionally requires matching kernel development files and platform
  dependencies.

```bash
sudo dnf install -y make gcc gcc-c++ cmake ninja-build dos2unix chrpath \
    patchelf libboundscheck libvirt-devel findutils git
git submodule update --init --recursive
dos2unix build.sh
./build.sh
```

Default outputs are `dist/release/bin/ub_turbo_exec`, `dist/release/lib/libubturbo_client.so`, and the configuration
files under `dist/release/conf/`. Install the SMAP user-space library and kernel modules before starting the complete
service. See the [installation guide](docs/installation.md).

### Start the service and invoke IPC capabilities

After installation, start the service and confirm that its default UDS file exists:

```bash
sudo systemctl start ubturbo
test -S /opt/ubturbo/ubturbo_ipc
```

The Python client under `tools/ubturbo_ipc/` invokes SMAP capabilities through UBTurbo without loading `libsmap.so`
directly into the caller. For example, start 4K (container) mode and query its status:

```bash
python3 tools/ubturbo_ipc/ubturbo_ipc.py start 0
python3 tools/ubturbo_ipc/ubturbo_ipc.py is_running
```

A `pageType` of `0` selects 4K mode, while `1` selects 2M (virtual-machine) mode. See the
[IPC client guide](tools/ubturbo_ipc/README.md) for all commands, argument formats, and security notes.

Run UBTurbo and RMRS unit tests with:

```bash
./build.sh -t test
```

Run SMAP tests separately:

```bash
cd plugins/smap/test
sh run_dt.sh
```

## Documentation

- [Documentation center](docs/README.md)
- [Installation](docs/installation.md)
- [Operations and usage](docs/user_guide.md)
- [Configuration reference](docs/configuration.md)
- [Architecture](docs/architecture.md)
- [Developer guide](docs/developer_guide.md)
- [API reference](docs/api_reference.md)
- [Security](docs/security.md)
- [Release notes](docs/release_notes.md)
- [RMRS](docs/rmrs/README_EN.md)
- [SMAP](docs/smap/README_EN.md)
- [UBDMA](docs/ubdma/README.md)
- [UCache](docs/ucache/README.md)

The detailed documents are maintained primarily in Chinese. The Chinese README also includes the project roadmap
and links to the performance-testing methodology.

## FAQ

**Why does the build fail on x86 or a non-openEuler distribution?**

The current build supports aarch64 openEuler only and reads `/etc/openEuler-release` during CMake configuration.

**Why does the daemon fail after a plugin is enabled?**

Make sure the plugin library is installed and its admission entry and module code are valid. See the
[user guide](docs/user_guide.md#常见故障).

**Why does an SMAP module fail to load?**

The kernel modules must match the running kernel and be loaded in dependency order. See the
[installation guide](docs/installation.md#安装-smap).

## Contributing

Before submitting a change, run the relevant build, tests, clang-format, and clang-tidy checks, and add tests for
behavior changes. Follow the repository's existing commit prefixes, such as `Feature:`, `fix:`, `doc:`, or a
component name.

## Security

UBTurbo does not validate the business ownership or validity of a PID supplied by a caller. PIDs and source or
destination NUMA identifiers must be delivered by a trusted cluster resource manager. Do not place credentials,
private keys, sensitive addresses, or personal information in configuration files or logs.

## License

See [LICENSE](LICENSE). Individual kernel components and third-party dependencies may use different licenses; refer
to their component-level notices and the [third-party component list](docs/third_party_components.md).

## Support

This open-source project is not a commercial product and provides community support only.
