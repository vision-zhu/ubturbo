/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MIGRATE_MODE_H_INCLUDED
#define MIGRATE_MODE_H_INCLUDED

enum migrate_reason {
    MR_COMPACTION = 0,
    MR_MEMORY_FAILURE = 1,
    MR_MEMORY_HOTPLUG = 2,
    MR_SYSCALL = 3,
    MR_MEMPOLICY_MBIND = 4,
    MR_NUMA_MISPLACED = 5,
    MR_CONTIG_RANGE = 6,
    MR_LONGTERM_PIN = 7,
    MR_DEMOTION = 8,
    MR_TYPES = 9
};

enum migrate_mode {
    MIGRATE_ASYNC = 0,
    MIGRATE_SYNC_LIGHT = 1,
    MIGRATE_SYNC = 2,
    MIGRATE_SYNC_NO_COPY = 3,
};
#if defined (CONFIG_FLATMEM)

#define __pfn_to_page(pfn) (vmemmap + (pfn))

#elif defined (CONFIG_SPARSEMEM)

#define __pfn_to_page(pfn) \
    ({ \
        unsigned long __pfn = (pfn); \
        struct mem_section *__sec = __pfn_to_section(__pfn); \
        __section_mem_map_addr(__sec) + __pfn; \
    })
#endif
#define pfn_to_page __pfn_to_page
#endif
