/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 * Description: Per-NUMA PFN queue for cold page swap-out
 *
 * Lock-free MPSC ring buffer.  During the scan phase (producers only),
 * enqueue claims a slot via atomic_fetch_add(&tail) and commits via
 * atomic_inc(&count) after the PFN is written.  During the drain phase
 * (single consumer, after all producers have quiesced), the consumer
 * reads count and walks head..head+count.  No global lock — per-node
 * tail and count are the only shared state.
 *
 * IMPORTANT: The consumer MUST NOT run concurrently with producers.
 * In the current architecture this is guaranteed by userspace: the
 * drain ioctl (SMAP_MIG_DRAIN_COLD_QUEUE) is issued only after all
 * scan ioctls have returned.
 */

#ifndef _SRC_SMAP_COLD_QUEUE_H
#define _SRC_SMAP_COLD_QUEUE_H

#include <linux/types.h>
#include <linux/atomic.h>

#ifndef SMAP_MAX_NUMNODES
#define SMAP_MAX_NUMNODES 26
#endif

/*
 * Power-of-two so ring index wraps with a mask instead of a division-modulo
 * (modulo by a non-power-of-two compiles to a hardware divide, ~20x slower
 * than a bitmask on this hot per-page enqueue path).
 */
#define SMAP_COLD_QUEUE_PID_MAX_SIZE (262144 * 16) /* 2^22 */
#define SMAP_COLD_QUEUE_PID_MAX_MASK (SMAP_COLD_QUEUE_PID_MAX_SIZE - 1)

struct smap_cold_queue {
	u64 *pfn;
	unsigned int head;	/* consumer only — no concurrency with drain */
	atomic_t tail;		/* producers: fetch_add to claim a slot */
	atomic_t count;		/* committed items: producers inc, consumer dec */
};

/* One ring per NUMA node; indexed by nid in [0, SMAP_MAX_NUMNODES). */
extern struct smap_cold_queue smap_cold_numa_queue[SMAP_MAX_NUMNODES];

void smap_cold_queue_free(void);
int smap_cold_queue_init(void);

/*
 * Enqueue a PFN for swap-out. Returns 0 on success, -1 if queue is full.
 * Must be called only during the scan phase (producers only).
 */
int smap_cold_queue_enqueue(int nid, u64 pfn);

#endif /* _SRC_SMAP_COLD_QUEUE_H */
