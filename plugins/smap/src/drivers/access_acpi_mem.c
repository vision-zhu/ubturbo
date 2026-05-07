// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2024. All rights reserved.
 * Description: SMAP ACPI memory module
 */

#include <linux/types.h>

#include "check.h"
#include "access_tracking.h"
#include "access_acpi_helper.h"
#include "access_acpi_mem.h"

#undef pr_fmt
#define pr_fmt(fmt) "smap_drv_acpi: " fmt

int nr_local_numa = 0;
static int max_node_id = -1; /* Track maximum node ID from all SRAT entries */
struct mem_info acpi_mem = {
	.len = 0,
	.mem = LIST_HEAD_INIT(acpi_mem.mem),
};

bool is_paddr_local(u64 pa)
{
	struct acpi_mem_segment *mem;
	list_for_each_entry(mem, &acpi_mem.mem, segment) {
		if (mem->start <= pa && pa <= mem->end)
			return true;
	}
	return false;
}

static int acpi_table_build_mem(struct acpi_subtable_header *header)
{
	int node;
	u32 hotpluggable;
	struct acpi_mem_segment *mem, *tmp;
	struct acpi_srat_mem_affinity *p =
		(struct acpi_srat_mem_affinity *)header;

	if ((p->flags & ACPI_SRAT_MEM_ENABLED) == 0)
		return 0;
	hotpluggable = p->flags & ACPI_SRAT_MEM_HOT_PLUGGABLE;
	if (hotpluggable && !IS_ENABLED(CONFIG_MEMORY_HOTPLUG))
		return 0;

	mem = kmalloc(sizeof(*mem), GFP_KERNEL);
	if (!mem)
		return -ENOMEM;

	mem->start = p->base_address;
	mem->end = mem->start + p->length - 1;
	mem->pxm = p->proximity_domain;
	node = pxm_to_node(mem->pxm);
	if (node == NUMA_NO_NODE) {
		kfree(mem);
		pr_err("unable to trans PXM id to NUMA node, ret: %d\n", node);
		return -EINVAL;
	}
	mem->node = node;
	/* Update max_node_id for nr_local_numa calculation */
	if (node > max_node_id)
		max_node_id = node;

	/* Add to list and ensure the ascending order of acpi_mem.mem */
	if (list_empty(&acpi_mem.mem)) {
		list_add_tail(&mem->segment, &acpi_mem.mem);
	} else {
		list_for_each_entry_reverse(tmp, &acpi_mem.mem, segment) {
			if (tmp->start < mem->start)
				break;
		}
		if (list_entry_is_head(tmp, &acpi_mem.mem, segment))
			list_add(&mem->segment, &acpi_mem.mem);
		else
			list_add(&mem->segment, &tmp->segment);
	}
	acpi_mem.len++;

	return 0;
}

static int acpi_parse_memory_affinity(union acpi_subtable_headers *header,
				      const unsigned long end)
{
	struct acpi_srat_mem_affinity *memory_affinity;

	memory_affinity = (struct acpi_srat_mem_affinity *)header;
	return acpi_table_build_mem(&header->common);
}

#ifdef CONFIG_ARM64
/* Parse GICC (Generic Interrupt Controller CPU) affinity for ARM64 */
static int acpi_parse_gicc_affinity(union acpi_subtable_headers *header,
				    const unsigned long end)
{
	struct acpi_srat_gicc_affinity *gicc;
	int node;

	gicc = (struct acpi_srat_gicc_affinity *)header;

	if (!(gicc->flags & ACPI_SRAT_GICC_ENABLED))
		return 0;

	node = pxm_to_node(gicc->proximity_domain);
	if (node == NUMA_NO_NODE) {
		pr_warn("GICC PXM %d has no valid NUMA node\n",
			gicc->proximity_domain);
		return 0;
	}

	if (node > max_node_id)
		max_node_id = node;

	return 0;
}
#else
/* Parse CPU affinity for x86 architecture */
static int acpi_parse_cpu_affinity(union acpi_subtable_headers *header,
				   const unsigned long end)
{
	struct acpi_srat_cpu_affinity *cpu;
	int node;
	u32 pxm;

	cpu = (struct acpi_srat_cpu_affinity *)header;

	if (!(cpu->flags & ACPI_SRAT_CPU_ENABLED))
		return 0;

	/* Proximity domain is split into lo (8 bits) and hi (24 bits) */
	pxm = cpu->proximity_domain_lo;
	pxm |= ((u32)cpu->proximity_domain_hi[0] << 8);
	pxm |= ((u32)cpu->proximity_domain_hi[1] << 16);
	pxm |= ((u32)cpu->proximity_domain_hi[2] << 24);

	node = pxm_to_node(pxm);
	if (node == NUMA_NO_NODE) {
		pr_warn("CPU PXM %u has no valid NUMA node\n", pxm);
		return 0;
	}

	if (node > max_node_id)
		max_node_id = node;

	return 0;
}

/* Parse X2APIC CPU affinity for modern x86 systems */
static int acpi_parse_x2apic_affinity(union acpi_subtable_headers *header,
				      const unsigned long end)
{
	struct acpi_srat_x2apic_cpu_affinity *x2apic;
	int node;

	x2apic = (struct acpi_srat_x2apic_cpu_affinity *)header;

	if (!(x2apic->flags & ACPI_SRAT_CPU_ENABLED))
		return 0;

	node = pxm_to_node(x2apic->proximity_domain);
	if (node == NUMA_NO_NODE) {
		pr_warn("X2APIC PXM %d has no valid NUMA node\n",
			x2apic->proximity_domain);
		return 0;
	}

	if (node > max_node_id)
		max_node_id = node;

	return 0;
}
#endif

int init_acpi_mem(void)
{
	int count;
	acpi_status status;
	struct acpi_table_header *table_header = NULL;
	unsigned long table_size = sizeof(struct acpi_table_srat);
#ifdef CONFIG_ARM64
	/* ARM64: parse memory and GICC affinity entries */
	struct acpi_subtable_proc proc[2] = {
		{
			.id = ACPI_SRAT_TYPE_MEMORY_AFFINITY,
			.handler = acpi_parse_memory_affinity,
		},
		{
			.id = ACPI_SRAT_TYPE_GICC_AFFINITY,
			.handler = acpi_parse_gicc_affinity,
		},
	};
#define PROC_NUM 2
#else
	/* x86: parse memory, CPU and X2APIC affinity entries */
	struct acpi_subtable_proc proc[3] = {
		{
			.id = ACPI_SRAT_TYPE_MEMORY_AFFINITY,
			.handler = acpi_parse_memory_affinity,
		},
		{
			.id = ACPI_SRAT_TYPE_CPU_AFFINITY,
			.handler = acpi_parse_cpu_affinity,
		},
		{
			.id = ACPI_SRAT_TYPE_X2APIC_CPU_AFFINITY,
			.handler = acpi_parse_x2apic_affinity,
		},
	};
#define PROC_NUM 3
#endif

	if (acpi_disabled) {
		pr_warn("acpi disabled\n");
		return -ENODEV;
	}

	status = acpi_get_table(ACPI_SIG_SRAT, 0, &table_header);
	if (ACPI_FAILURE(status) || !table_header) {
		pr_warn("%4.4s not present\n", ACPI_SIG_SRAT);
		return -ENODEV;
	}

	count = acpi_parse_entries_array(ACPI_SIG_SRAT, table_size,
					 table_header, proc, PROC_NUM, 0);
	if (count < 0) {
		pr_err("failed to parse ACPI entries, ret: %d\n", count);
		acpi_put_table(table_header);
		return -EINVAL;
	}

	acpi_put_table(table_header);

	/* Calculate nr_local_numa from max_node_id */
	if (max_node_id >= 0) {
		nr_local_numa = max_node_id + 1;
		pr_info("nr_local_numa calculated from SRAT: %d\n", nr_local_numa);
	} else {
		pr_warn("no valid NUMA node found in SRAT\n");
	}

	return 0;
}

void reset_acpi_mem(void)
{
	struct acpi_mem_segment *mem, *tmp;
	list_for_each_entry_safe(mem, tmp, &acpi_mem.mem, segment) {
		list_del(&mem->segment);
		kfree(mem);
	}
	acpi_mem.len = 0;
	max_node_id = -1;
	nr_local_numa = 0;
}

u64 get_node_actc_len(int node_id, int page_size)
{
	u64 page_cnt = 0;
	struct acpi_mem_segment *mem;

	list_for_each_entry(mem, &acpi_mem.mem, segment) {
		if (mem->node != node_id)
			continue;
		if (page_size == g_pagesize_huge)
			page_cnt += calc_huge_count(mem->end - mem->start + 1);
		else if (page_size == PAGE_SIZE)
			page_cnt += calc_normal_count(mem->end - mem->start + 1);
	}
	return page_cnt;
}

int calc_paddr_acidx_acpi(u64 paddr, int *nid, u64 *index, int page_size)
{
	struct acpi_mem_segment *mem;
	bool found = false;
	u64 offset = 0;
	u64 acidx;
	int last_nid = NUMA_NO_NODE;
	int shift = __builtin_ctz(page_size);

	list_for_each_entry(mem, &acpi_mem.mem, segment) {
		if (last_nid != mem->node) {
			offset = 0;
			last_nid = mem->node;
		}
		if (paddr > mem->end) {
			offset += mem->end - mem->start + 1;
			continue;
		} else if (unlikely(paddr < mem->start))
			break;
		offset += paddr - mem->start;
		acidx = offset >> shift;
		found = true;
		break;
	}
	if (!found)
		return -ERANGE;
	*nid = mem->node;
	*index = acidx;
	return 0;
}

int calc_acidx_paddr_acpi(int nid, u64 acidx, u64 *paddr, int page_size)
{
	struct acpi_mem_segment *mem;
	u64 range;
	int shift = __builtin_ctz(page_size);
	u64 offset = acidx << shift;

	list_for_each_entry(mem, &acpi_mem.mem, segment) {
		if (mem->node != nid)
			continue;
		range = mem->end - mem->start + 1;
		if (offset >= range) {
			offset -= range;
			continue;
		}
		*paddr = mem->start + offset;
		return 0;
	}
	return -ERANGE;
}
