# 🔥 SMAP 性能测试报告

## 测试目标

本测试使用 Redis 7.0.15 和 memtier_benchmark 2.1.4，评估 4K 页面模式下将 Redis 25% 内存迁移到
远端 NUMA 节点后的RPS吞吐量收益。测试包含全本地内存、启用 SMAP 并迁出25%内存的数据，每组独立运行 5 次，结果取中位数。

## 测试环境

| 类别            | 配置                                       |
| --------------- | ------------------------------------------ |
| 服务器          | TaiShan 200（Model 2280）                  |
| CPU             | 2 × Kunpeng 950，96 核/CPU，主频 2.3 GHz |
| 本地内存        | 3072 GiB DDR5-6400，每个NUMA各 768 GiB    |
| 远端内存        | 2 GiB，映射为 NUMA 5                      |
| 互联            | UB 128 Gbit/s，单链路                     |
| 操作系统        | openEuler 24.03 LTS SP4，aarch64           |
| 内核            | 6.6.0-159.4.2.153.oe2403sp4.aarch64        |
| GCC / CMake     | GCC 12.3.1，CMake 3.22.1                   |
| Redis / memtier | Redis 7.0.15，memtier_benchmark 2.1.4      |
| 页面模式        | 4K；透明大页关闭                           |
| Redis 绑定      | CPU 16，NUMA 0                             |
| 压测端绑定      | CPU 10,12，NUMA 0                          |
| 远端内存节点    | NUMA 5                                     |

测试前使用以下设置关闭会改变页面位置的内核机制：

```bash
echo 0 | sudo tee /proc/sys/kernel/numa_balancing
echo never | sudo tee /sys/kernel/mm/transparent_hugepage/defrag
echo never | sudo tee /sys/kernel/mm/transparent_hugepage/enabled
```

## Redis 配置

Redis 关闭 RDB、AOF 和淘汰策略，服务端监听本机回环地址：

```bash
numactl --physcpubind=16 --membind=0 \
    redis-server --bind 0.0.0.0 --protected-mode no \
    --port 6379 --save '' --appendonly no
```

## 启动 UBTurbo 和 SMAP

```bash
sudo systemctl restart ubturbo

for i in $(seq 1 30); do
    test -S /opt/ubturbo/ubturbo_ipc && break
    sleep 1
done
test -S /opt/ubturbo/ubturbo_ipc

IPC_CLIENT=tools/ubturbo_ipc/ubturbo_ipc.py
python3 "$IPC_CLIENT" start 0
python3 "$IPC_CLIENT" is_running
```

`start 0` 启动 4K 普通进程模式。整个测试过程保持该页面模式不变。

## 装载数据和预热

测试装载 1150 万个 key，每个 value 为 512 Byte。压测端使用 1000 个连接、2 个线程和 pipeline 100：

```bash
KEY_MAXIMUM=11500000
CLIENTS=1000
THREADS=2
PIPELINE=100
DATA_SIZE=512

numactl --physcpubind=10,12 \
    memtier_benchmark -s 192.168.124.2 -p 6379 \
    --clients="$CLIENTS" --threads="$THREADS" \
    --pipeline="$PIPELINE" --ratio=1:0 --key-pattern=P:P \
    --key-minimum=1 --key-maximum="$KEY_MAXIMUM" \
    --data-size="$DATA_SIZE" --requests="$KEY_MAXIMUM"
```

装载完成后的Redis的内存占用约为8 GiB。

## 测量全本地基线

随机分布和 Gauss 分布分别使用全新 Redis 进程执行，不连续复用同一个实例：

随机分布命令，`R:R` 表示读写 key 均为随机分布，改为`G:G` 表示读写 key 均为 Gauss 分布：

```bash
numactl --physcpubind=10,12 \
    memtier_benchmark -s 192.168.124.2 -p 6379 \
    --clients="$CLIENTS" --threads="$THREADS" \
    --pipeline="$PIPELINE" --ratio=1:1 --key-pattern=R:R \
    --key-minimum=1 --key-maximum="$KEY_MAXIMUM" --distinct-client-seed \
    --data-size="$DATA_SIZE" --test-time=300
```

## 迁出 25% 内存

执行全本地基线相同的memtier_benchmark命令，执行后，执行以下控制命令将迁移Redis-server的25%内存到远端NUMA 5。
远端 NUMA 5 为 Redis 分配 2048 MiB 容量，用于容纳目标迁出页面。

```bash
REMOTE_MIB=2048

python3 "$IPC_CLIENT" remote_numa_info \
    "$LOCAL_NID" "$REMOTE_NID" "$REMOTE_MIB"
python3 "$IPC_CLIENT" migrate \
    "$REDIS_PID" "$REMOTE_NID" 25 0
sleep 60
numastat -p "$REDIS_PID"
```

迁移完成后，NUMA 0 驻留 6 GiB，NUMA 5 驻留 2 GiB。

## 测试结果

| 数据访问分布 | 测试组      | 吞吐量（QPS） | 吞吐劣化 |
| ------------ | ----------- | ------------: | -------: |
| 随机分布SET  | 全本地基线  |        601161 |       — |
| 随机分布SET  | SMAP 实验组 |        551411 |    8.28% |
| 随机分布GET  | 全本地基线  |        674749 |       — |
| 随机分布GET  | SMAP 实验组 |        623717 |    7.56% |

## 清理环境

```bash
redis-cli -p "$REDIS_PORT" shutdown nosave
python3 "$IPC_CLIENT" stop
sudo systemctl stop ubturbo
```
