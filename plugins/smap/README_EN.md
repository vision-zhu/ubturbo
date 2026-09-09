# SMAP

[简体中文](README.md) | [English](README_EN.md)

SMAP is the tiered-memory scanning and page-migration component integrated by UBTurbo. It consists of the
`libsmap.so` user-space library and kernel modules for access tracking and migration. It supports ordinary 4 KiB
process pages and virtual machines backed by static 2 MiB huge pages.

## Capabilities

- Software access tracking based on Linux page-table Access Flags.
- Optional HIST-based remote-page access statistics on supported hardware.
- Selection and exchange of local cold pages and remote hot pages.
- Migrate-out targets, migrate-back, urgent migration, and remote-to-remote migration.
- Multiple local and remote NUMA nodes and grouped migration policy.

Software AF scanning is the baseline path. HIST is an optional platform-specific enhancement and is not a required
part of every deployment.

## Architecture

![SMAP layered architecture](docs/images/smap-architecture.svg)

See the [architecture document](docs/architecture.md) for details.

## Build

```bash
./build.sh -t release
make -C src/drivers KERNEL_VERSION=openeuler -j"$(nproc)"
cp -f src/drivers/Module.symvers src/tiering/depends/
make -C src/tiering KERNEL_VERSION=openeuler -j"$(nproc)"
make -C src/ucache -j"$(nproc)"
```

Kernel development files must match the running kernel. See the [user guide](docs/user_guide.md) for installation.

## Documentation

- [Architecture](docs/architecture.md)
- [User guide](docs/user_guide.md)
- [Developer guide](docs/developer_guide.md)
- [API reference](docs/api_reference.md)
- [Algorithm design](docs/performance_algorithm.md)
- [Performance testing](docs/performance_testing.md)
- [Release notes](RELEASE-NOTES.md)

Detailed documents are maintained in Chinese; this README is the synchronized English entry.
