/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_NUMA_H
#define _LINUX_NUMA_H
#include <linux/pfn.h>
#include <linux/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NODES_SHIFT     CONFIG_NODES_SHIFT

#define MAX_NUMNODES    (1 << NODES_SHIFT)

#define	NUMA_NO_NODE	(-1)

#ifdef __cplusplus
}
#endif

#endif
