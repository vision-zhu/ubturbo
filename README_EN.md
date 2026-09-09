# UBTurbo

[简体中文](README.md) | [English](README_EN.md)

UBTurbo is an in-node resource-management framework for openEuler. It provides configuration loading, dynamic
plugin management, asynchronous logging, and Unix Domain Socket (UDS) IPC. Its SMAP integration adds page scanning
and migration for tiered memory, while services such as RMRS and UCache run as plugins in the UBTurbo daemon.

> [!IMPORTANT]
> UBTurbo currently supports **aarch64 openEuler only**. SMAP contains kernel modules that must match the running
> kernel and the platform capabilities.

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

- [Installation](docs/installation.md)
- [Operations and usage](docs/user_guide.md)
- [Configuration reference](docs/configuration.md)
- [Architecture](docs/architecture.md)
- [Developer guide](docs/developer_guide.md)
- [API reference](docs/api_reference.md)
- [Security](docs/security.md)
- [Release notes](docs/release_notes.md)
- [RMRS](plugins/rmrs/README_EN.md)
- [SMAP](plugins/smap/README_EN.md)

The detailed documents are maintained in Chinese; this README provides the synchronized English project entry.

## Security

UBTurbo does not validate the business ownership or validity of a PID supplied by a caller. PIDs and source or
destination NUMA identifiers must be delivered by a trusted cluster resource manager. Do not place credentials,
private keys, sensitive addresses, or personal information in configuration files or logs.

## License

See [LICENSE](LICENSE). Individual kernel components and third-party dependencies may use different licenses; refer
to their component-level notices and the [third-party component list](docs/third_party_components.md).
